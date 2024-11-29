/**
 * \file
 *
 * \date Jun 24, 2019
 * \author Attila Kovacs
 *
 * \brief
 *      A set of functions to support the efficient retrieval of lazy variables, that is variables that change
 *      infrequently, from the SMA-X database. Rather than querying the database on every call, the first lazy pull of
 *      a variable initiates monitoring for updates. The pull requests will return the current state of the variable at all times,
 *      but it generates minimal network traffic only when the underlying value in the database is changed.
 */


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <errno.h>
#include <math.h>

#include "smax-private.h"

/// \cond PRIVATE

#define MAX_UNPULLED_LAZY_UPDATES       10      ///< Number of unprocessed updates, before unsubscribing from notifications...

typedef struct LazyMonitor {
  boolean isLinked;         ///< If the monitor is linked in list and not to be destroyed as such.
  int users;                ///< Number of callers currently using this monitor point -- update with monitorLock only!
  char *table;              ///< The Redis hash table name in which the data is stored
  char *key;                ///< The hash field name, or NULL if the monitor is for the structure represented by the table.
  char *channel;            ///< The pub/sub channel, e.g. "smax:<group>:<key>"
  char *data;               ///< The serialized data, as stored in Redis, or a pointer to an XStructure
  XMeta *meta;              ///< (optional) metadata
  boolean isCached;         ///< Whether the variable is continuously caching 'current' data.
  boolean isCurrent;        ///< If the locally stored data is current.
  time_t updateTime;        ///< Time of last update.
  int updateCount;          ///< Number of times the variable was updated.
  int unpulledCount;        ///< Number of updates since last pull...
  struct LazyMonitor *prev;
  struct LazyMonitor *next;
} LazyMonitor;

/// \endcond

static int nMonitors;                                           ///< Number of lazy variables monitored.

static LazyMonitor *monitorTable[SMAX_LOOKUP_SIZE];             ///< hashed monitor tables
static pthread_mutex_t monitorLock = PTHREAD_MUTEX_INITIALIZER; ///< Mutex for accessing monitor tables
static pthread_mutex_t dataLock = PTHREAD_MUTEX_INITIALIZER;    ///< mutex for accessing monitor data/metadata

static LazyMonitor *CreateMonitorAsync(const char *table, const char *key, XType type, boolean withMeta);
static boolean DestroyMonitorAsync(LazyMonitor *m);
static int GetChannelLookupIndex(const char *channel);
static __inline__ int GetTableIndex(const LazyMonitor *m);
static LazyMonitor *GetMonitorAsync(const char *table, const char *key);
static LazyMonitor *GetSpecificMonitorAsync(const char *table, const char *key);
static void ProcessLazyUpdates(const char *pattern, const char *channel, const char *msg, long length);

/**
 * Decrements the number of concurrent user calls that currently need access to the specific
 * lazy monitor data. If the monitor has no users left and is not currently monitored
 * (that is unlinked from the active monitor tables) it will be destroyed. The table mutex
 * should be locked when calling this routine.
 *
 * @param m     Pointer to lazy monitor
 *
 * @sa Release()
 */
static void ReleaseAsync(LazyMonitor *m) {
  int isActive = (--m->users > 0) || m->isLinked;
  if(!isActive) DestroyMonitorAsync(m);
}

/**
 * Decrements the number of concurrent user calls that currently need access to the specific
 * lazy monitor data. If the monitor has no users left and is not currently monitored
 * (that is unlinked from the active monitor tables) it will be destroyed. It will
 * acquire an exclusive lock to the table monitor and call ReleaseAsync()
 *
 * @param m     Pointer to lazy monitor
 * @return      X_SUCCESS (0)
 *
 * @sa ReleaseAsync()
 */
static int Release(LazyMonitor *m) {
  if(!m) return x_error(X_NULL, EINVAL, "Release", "NULL argument");

  pthread_mutex_lock(&monitorLock);
  ReleaseAsync(m);
  pthread_mutex_unlock(&monitorLock);
  return X_SUCCESS;
}

/**
 * Applies an update to a cached lazy monitor. It swaps the contents of the update with that
 * of the specified monitor, s.t. the previous monitor content is available in the update
 * after the call. The caller can use it and/or destroy it if it's not longer important.
 *
 * @param update    Pointer to the update, usually created via CreateStaging().
 * @param m         Pointer to the cached monitor point.
 *
 * @sa CreateStaging()
 */
static void ApplyUpdateAsync(LazyMonitor *update, LazyMonitor *m) {
  char *oldData;
  XMeta *oldMeta;

  if(!m) return;

  // Update the stored data 'atomically'
  pthread_mutex_lock(&dataLock);
  oldData = m->data;
  oldMeta = m->meta;
  m->data = update->data;
  m->meta = update->meta;
  m->updateTime = time(NULL);
  m->isCurrent = TRUE;
  pthread_mutex_unlock(&dataLock);

  // we'll destroy the old data / meta with the update!
  update->data = oldData;
  update->meta = oldMeta;
}

/**
 * Callback routine for pipelined updates.
 *
 * @param arg       Pointer to the update, usually created by CreateStaging().
 *
 */
static void ApplyUpdate(void *arg) {
  LazyMonitor *update = (LazyMonitor *) arg;
  LazyMonitor *m;

  if(!update) return;

  pthread_mutex_lock(&monitorLock);
  m = GetSpecificMonitorAsync(update->table, update->key);
  pthread_mutex_unlock(&monitorLock);

  if(m) {
    ApplyUpdateAsync(update, m);
    Release(m);
  }

  DestroyMonitorAsync(update);
}

/**
 * Creates a lazy monitor that can be used to stage SMA-X updates into without altering
 * data of the original monitor.
 *
 * @param m     the lazy monitor for which we want a pending update
 * @return      a new independent lazy monitor (unlinked to currently active monitors)
 *              with the same table/key (copies thereof) as the original.
 */
static LazyMonitor *CreateStaging(const LazyMonitor *m) {
  LazyMonitor *s;

  // If we update in the background, create temporary storage in which
  // we can stage the update.
  s = (LazyMonitor *) calloc(1, sizeof(*s));
  x_check_alloc(s);

  // Copy table/key
  s->table = xStringCopyOf(m->table);
  s->key = xStringCopyOf(m->key);

  // 'pending' needs a data container where it can store the data.
  if(!m->key) s->data = (char *) xCreateStruct();

  // If we need metadata, pending needs an independent one.
  if(m->meta) s->meta = smaxCreateMeta();

  return s;
}

/**
 * Updates the monitored data in the cache, by pulling from SMA-X. The update is essentially atomic
 * as it happens with a single reassignment of a pointer.
 *
 * @param m     Pointer to a lazy monitor datum.
 * @return      X_SUCCESS (0) if successfull or else an error (&lt;0) from smaxPull().
 */
static int UpdateCachedAsync(LazyMonitor *m, boolean background) {
  static const char *fn = "UpdateCachedAsync";

  LazyMonitor *staging;
  XType type;
  int status = X_SUCCESS;
  void *ptr;

  if(!m) return x_error(X_NULL, EINVAL, fn, "input parameter 'm' is NULL");

  staging = CreateStaging(m);
  if(!staging) return x_trace(fn, NULL, X_NULL);

  if(m->key) {
    type = X_RAW;
    ptr = &staging->data;
  }
  else {
    type = X_STRUCT;
    ptr = staging->data;
  }

  if(background && smaxIsPipelined()) {
    status = smaxQueue(m->table, m->key, type, 1, ptr, staging->meta);
    if(!status) {
      m->users++;
      smaxQueueCallback(ApplyUpdate, staging);
    }
  }
  else {
    status = smaxPull(m->table, m->key, type, 1, ptr, staging->meta);
    if(!status) ApplyUpdateAsync(staging, m);
    DestroyMonitorAsync(staging);
  }

  prop_error(fn, status);
  return X_SUCCESS;
}

/**
 * Returns the currently cached value of a lazy monitor point into the supplied buffer as
 * the requested type and element count.
 *
 *
 * @param[in]  m        Pointer to a lazy monitor datum.
 * @param[in]  type     SMA-X type requested
 * @param[in]  count    Number of elements requested
 * @param[out] value    Buffer to fill with the requested data type/count.
 *
 * @return      X_SUCCESS (0) if successfull, or else an error code from smaxStringToValues()
 */
static int GetCachedAsync(const LazyMonitor *m, XType type, int count, void *value) {
  static const char *fn = "GetCachedAsync";

  int n, status;

  if(!m) return x_error(X_NULL, EINVAL, fn, "monitor point 'm' is NULL");

  if(!m->data) {
    if(m->meta) return m->meta->status;
    return x_error(X_NULL, EINVAL, fn, "m->data is NULL");
  }

  switch(type) {
    case X_STRUCT: {
      XStructure *tmp = xCopyOfStruct((XStructure *) m->data);
      XStructure *dst = (XStructure *) value;

      if(!m->table) return x_error(X_TYPE_INVALID, EINVAL, fn, "m->table is NULL");
      xClearStruct(dst);
      dst->firstField = tmp->firstField;
      free(tmp);
      break;
    }

    case X_RAW: {
      char *str;
      if(!m->meta) return x_error(X_NULL, EINVAL, fn, "m->meta is NULL");
      if(!m->meta->storeBytes) return X_SUCCESS;

      str = (char *) malloc(m->meta->storeBytes);
      if(!str) return x_error(X_NULL, errno, fn, "malloc() error (%d bytes)", m->meta->storeBytes);

      memcpy(str, m->data, m->meta->storeBytes);
      *(char **) value = str;
      break;
    }

    case X_STRING:
      if(!m->meta) return x_error(X_NULL, EINVAL, fn, "m->meta is NULL");
      if(m->meta->storeType != X_STRING) return x_error(X_TYPE_INVALID, EINVAL, fn, "wring m->type (not X_STRING): %d", m->meta->storeType);
      smaxUnpackStrings(m->data, m->meta->storeBytes, count, (char **) value);
      break;

    default:
      status = smaxStringToValues(m->data, value, type, count, &n);
      if(status <= 0) return x_trace(fn, NULL, status);
  }

  return X_SUCCESS;
}

static LazyMonitor *GetCreateMonitor(const char *table, const char *key, XType type, boolean withMeta) {
  static const char *fn = "GetCreateMonitor";

  LazyMonitor *m;
  char *lazytab = (char *) table;

  if(type == X_STRUCT) {
    lazytab = xGetAggregateID(table, key);
    key = NULL;
  }

  if(!lazytab) return x_trace_null(fn, NULL);

  pthread_mutex_lock(&monitorLock);

  m = GetSpecificMonitorAsync(lazytab, key);
  if(!m) m = CreateMonitorAsync(lazytab, key, type, withMeta);

  pthread_mutex_unlock(&monitorLock);

  if(!m) return x_trace_null(fn, NULL);
  if(withMeta && !m->meta) m->meta = smaxCreateMeta();

  if(lazytab != table) free(lazytab);

  return m;
}

static int FetchData(LazyMonitor *m, XType type, int count, void *value, XMeta *meta) {
  static const char *fn = "FetchData";

  int status = X_SUCCESS;

  if(meta && !m->meta) {
    // Update monitor to include metadata and pull to set it.
    m->meta = (XMeta *) calloc(1, sizeof(XMeta));
    x_check_alloc(m->meta);
    status = UpdateCachedAsync(m, FALSE);
  }
  else if(!m->isCurrent && !m->isCached) status = UpdateCachedAsync(m, FALSE);

  xvprintf("SMA-X: Lazy pull %s:%s (status=%d)\n", m->table, m->key ? m->key : "", status);

  if(status) {
    xZero(value, type, count);
    if(meta) if(meta != m->meta) smaxResetMeta(meta);
  }
  else {
    m->unpulledCount = 0;   // Reset the unread updates counter

    // Copy/parse the cached data into the requested destination.
    pthread_mutex_lock(&dataLock);
    status = GetCachedAsync(m, type, count, value);
    if(meta) if(meta != m->meta) *meta = *m->meta;
    pthread_mutex_unlock(&dataLock);
  }

  Release(m);

  prop_error(fn, status);
  return X_SUCCESS;
}

/**
 * Specify that a specific variable should be cached for minimum overhead lazy access. When a variable is lazy cached
 * its local copy is automatically updated in the background so that accessing it is always nearly instantaneous.
 * Lazy caching is a good choice for variables that change less frequently than they are polled typically. For
 * variables that change frequently (ans used less frequently), lazy caching is not a great choice since it consumes
 * network bandwidth even when the variable is not being accessed.
 *
 * Once a variable is lazy cached, it can be accessed instantaneously via smaxGetLazyCached() without any blocking
 * network operations.
 *
 * @param table   The hash table name.
 * @param key     The variable name under which the data is stored.
 * @param type    The SMA-X variable type, e.g. X_FLOAT or X_CHARS(40), of the buffer.
 * @return        X_SUCCESS (0) or X_NO_SERVICE.
 *
 * @sa smaxGetLazyCached()
 */
int smaxLazyCache(const char *table, const char *key, XType type) {
  LazyMonitor *m;

  m = GetCreateMonitor(table, key, type, TRUE);
  if(!m) return x_trace("smaxLazyCache", NULL, X_NO_SERVICE);

  m->isCached = TRUE;
  UpdateCachedAsync(m, FALSE);
  Release(m);

  return X_SUCCESS;
}

/**
 * Retrieve a variable from the local cache (if available), or else pull from the SMA-X database. If local caching was not
 * previously eanbled, it will be enabled with this call, so that subsequent calls will always return data from the locally
 * updated cache with minimal overhead and effectively no latency.
 *
 * @param table   The hash table name.
 * @param key     The variable name under which the data is stored.
 * @param type    The SMA-X variable type, e.g. X_FLOAT or X_CHARS(40), of the buffer.
 * @param count   The number of elements to retrieve
 * @param value   Pointer to the native data buffer in which to restore values
 * @param meta    Optional metadata pointer, or NULL if metadata is not required.
 * @return        X_SUCCESS (0), or X_NO_SERVICE is SMA-X is not accessible, or another error (&lt;0)
 *                from smax.h or xchange.h.
 *
 * @sa sa smaxLazyCache()
 * @sa sa smaxLaxyPull()
 */
int smaxGetLazyCached(const char *table, const char *key, XType type, int count, void *value, XMeta *meta) {
  static const char *fn = "smaxGetLazyCached";

  LazyMonitor *m;
  int status;

  m = GetCreateMonitor(table, key, type, meta != NULL);
  if(!m) return x_trace(fn, NULL, X_NO_SERVICE);

  status = FetchData(m, type, count, value, meta);
  m->isCached = TRUE; // Set after the first non-mirrored fetch...

  prop_error(fn, status);
  return X_SUCCESS;
}

/**
 * Poll an infrequently changing variable without stressing out the network
 * or the SMA-X database. The first lazy pull for a variable will fetch its value from SMA-X and
 * subscribe to update notifications. Subsequent smaxLazyPull() calls to the same variable will
 * retrieve its value from a local cache (without contacting SMA-X) as long as it is unchanged.
 *
 * Note, after you are done using a variable that has been lazy pulled, you should call smaxLazyEnd() to
 * signal that it no longer requires to be cached and updated in the background, or call
 * smaxLazyFlush() to flush all lazy caches for all lazy variables (if that is what you want).
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param type      The SMA-X variable type, e.g. X_FLOAT or X_CHARS(40), of the buffer.
 * \param count     The number of points to retrieve into the buffer.
 * \param value     Pointer to the buffer to which the data is to be retrieved.
 * \param meta      Pointer to metadata or NULL if no metadata is needed.
 *
 * \return          X_SUCCESS (0) on success, or else an error code (&lt;0) of smaxPull().
 *
 * \sa smaxLazyEnd()
 * \sa smaxLazyFlush()
 * @sa smaxPull()
 * @sa smaxQueue()
 *
 */
int smaxLazyPull(const char *table, const char *key, XType type, int count, void *value, XMeta *meta) {
  static const char *fn = "smaxLazyPull";

  LazyMonitor *m;

  if(!value) return x_error(X_NULL, EINVAL, fn, "value is NULL");

  m = GetCreateMonitor(table, key, type, meta != NULL);
  if(!m) return x_trace(fn, NULL, X_NO_SERVICE);

  prop_error(fn, FetchData(m, type, count, value, meta));
  return X_SUCCESS;
}

/**
 * Returns a single integer value for a given SMA-X variable, or a default value if the
 * value could not be retrieved.
 *
 * \param table           The hash table name.
 * \param key             The variable name under which the data is stored.
 * \param defaultValue    The value to return in case of an error.
 *
 * \return      The long integer value stored in SMA-X, or the specified default if the value could not be retrieved.
 *
 * @sa smaxPullLong()
 */
long long smaxLazyPullLong(const char *table, const char *key, long long defaultValue) {
  long long l;
  int s;

  s = smaxLazyPull(table, key, X_LONG, 1, &l, NULL);
  if(s) return defaultValue;

  return l;
}

/**
 * Returns a single double-precision value for a given SMA-X variable, or NAN if the
 * value could not be retrieved.
 *
 * \param table           The hash table name.
 * \param key             The variable name under which the data is stored.
 *
 * \return      The floating-point value stored in SMA-X, or NaN if the value could not be retrieved.
 *
 * @sa smaxLazyPullDoubleDefault()
 * @sa smaxPullDouble()
 */
double smaxLazyPullDouble(const char *table, const char *key) {
  return smaxLazyPullDoubleDefault(table, key, NAN);
}

/**
 * Returns a single double-precision value for a given SMA-X variable, or a default value if the
 * value could not be retrieved.
 *
 * \param table           The hash table name.
 * \param key             The variable name under which the data is stored.
 * \param defaultValue    The value to return in case of an error.
 *
 * \return      The floating-point value stored in SMA-X, or the specified default if the value could not be retrieved.
 *
 * @sa smaxLazyPullDouble()
 * @sa smaxPullDoubleDefault()
 */
double smaxLazyPullDoubleDefault(const char *table, const char *key, double defaultValue) {
  double d;
  int s = smaxLazyPull(table, key, X_DOUBLE, 1, &d, NULL);
  return s ? defaultValue : d;
}

/**
 * Lazy pulls a string value into the specified string buffer.
 *
 * @param table         The hash table name.
 * @param key           The variable name under which the data is stored.
 * @param buf           Buffer to fill with stored data
 * @param n             Number of bytes to fill in buffer. The retrieved data will be truncated as necessary.
 * @return              X_SUCCESS (0) if successful, or the error code (&lt;0) returned by smaxLazyPull().
 */
int smaxLazyPullChars(const char *table, const char *key, char *buf, int n) {
  prop_error("smaxLazyPullChars", smaxLazyPull(table, key, X_CHARS(n), 1, buf, NULL));
  return X_SUCCESS;
}

/**
 * Returns a single string value for a given SMA-X variable, or a NULL if the
 * value could not be retrieved.
 *
 * \param table           Hash table name.
 * \param key             Variable name under which the data is stored.
 *
 * \return      Pointer to the string value stored in SMA-X, or NULL if the value could not be retrieved.
 *
 * @sa smaxPullString()
 */
char *smaxLazyPullString(const char *table, const char *key) {
  char *str = NULL;
  int status;

  status = smaxLazyPull(table, key, X_STRING, 1, &str, NULL);
  if(status) {
    if(str) free(str);
    return x_trace_null("smaxLazyPullString", NULL);
  }

  return str;
}

/**
 * Lazy pulls data into a structure, discarding any prior data that the structure might contain.
 *
 * @param[in]  id       Aggregate structure ID.
 * @param[out] s        Destination structure to populate with the retrieved fields
 * @return              X_SUCCESS (0) if successful, or the error code (&lt;0) returned by smaxLazyPull().
 *
 * @sa smaxPullStruct()
 * @sa xCreateStruct()
 */
int smaxLazyPullStruct(const char *id, XStructure *s) {
  prop_error("smaxLazyPullStruct", smaxLazyPull(id, NULL, X_STRUCT, 1, s, NULL));
  return X_SUCCESS;
}

/**
 * Stops processing updates in the background for a specific variable.
 *
 * \param m     Pointer to the variable'structure monitor point structure.
 *
 */
static void RemoveMonitorAsync(LazyMonitor *m) {
  if(!m->isLinked) return;

  smaxUnsubscribe(m->table, m->key);
  if(!m->key) smaxUnsubscribe(m->table, "*");

  // Unlink the existing monitor point.
  if(m->prev != NULL) m->prev->next = m->next;
  else monitorTable[GetTableIndex(m)] = m->next;

  if(m->next != NULL) m->next->prev = m->prev;

  m->isLinked = FALSE;

  // Stop the subscriber if this was the last monitored point.
  if(--nMonitors == 0) smaxRemoveSubscribers(ProcessLazyUpdates);
}

/**
 * Stops processing lazy updates in the background for a given variable.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \return          X_SUCCESS (0)
 *
 * \sa smaxLazyFlush()
 * @sa smaxLazyPull()
 */
int smaxLazyEnd(const char *table, const char *key) {
  LazyMonitor *m;

  pthread_mutex_lock(&monitorLock);

  m = GetMonitorAsync(table, key);
  if(m) {
    RemoveMonitorAsync(m);
    ReleaseAsync(m);
  }

  pthread_mutex_unlock(&monitorLock);

  return X_SUCCESS;
}

/**
 * Stops all background processing of lazy updates in a given monitor table (linked list of
 * monitor points.
 *
 * \param m     Pointer to the variable's monitor point structure.
 *
 * \return      Number of monitor points flushed.
 *
 */
static int FlushTableAsync(LazyMonitor *m) {
  int n;

  for(n=0; m != NULL; n++) {
    LazyMonitor *next = m->next;
    m->isLinked = FALSE;               // Important so we can destroy it...
    smaxUnsubscribe(m->table, m->key);
    DestroyMonitorAsync(m);
    m = next;
  }

  return n;
}

/**
 * Discards caches for all lazy variables (i.e. stops all subscriptions to variable updates, at least until
 * the next smaxLazyPull() call). Generally speaking, it's a good idea to call this routine when one is done
 * using a set of lazy variables for the time being, but want to avoid the tedium of calling smaxLazyEnd()
 * individually for each of them. Note however, that after flushing the lazy caches, the fist lazy call
 * following for each variable will inevitably result in a real SMA-X pull. So use it carefully!
 *
 * \return      Number of monitor points flushed.
 *
 * @sa smaxLazyPull()
 * @sa smaxLazyEnd()
 */
int smaxLazyFlush() {
  int i, n = 0;

  pthread_mutex_lock(&monitorLock);

  for(i=SMAX_LOOKUP_SIZE; --i >= 0; ) {
    LazyMonitor *list;
    list = monitorTable[i];
    monitorTable[i] = NULL;
    n += FlushTableAsync(list);
  }

  nMonitors = 0;
  smaxRemoveSubscribers(ProcessLazyUpdates);

  pthread_mutex_unlock(&monitorLock);

  return n;
}

/**
 * Returns the actual number of times a variable has been updated from SMA-X. It may be useful
 * information when deciding if lazy pulling is appropriate (it is if the number of pull requests
 * exceeds the actual number of transfers significantly).
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 *
 * \return          The number of times a variable has been updated, or -1 if the variable
 *                  is not being monitored, or if the arguments are invalid.
 *
 */
int smaxGetLazyUpdateCount(const char *table, const char *key) {
  LazyMonitor *m;
  int n;

  if(!table) return -1;
  if(!key) return -1;

  pthread_mutex_lock(&monitorLock);

  m = GetMonitorAsync(table, key);
  n = m->updateCount;
  ReleaseAsync(m);

  pthread_mutex_unlock(&monitorLock);

  return n;
}

/**
 * Creates a new monitor point for the specified variable, and add it to the monitor table. It should be called
 * with exclusive access to the monitor table. You must also call Release() after done using the newly created
 * monitor.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param type      The expected data type
 * \param withMeta  If lazy pull with metadata.
 *
 * \return          Pointer to the variable's newly allocated monitor point structure or NULL if could not
 *                  subscribe to updates for this variable.
 *
 * \sa Release()
 */
static LazyMonitor *CreateMonitorAsync(const char *table, const char *key, XType type, boolean withMeta) {
  static const char *fn = "CreateMonitorAsync";

  LazyMonitor *m;
  char *id;
  int i;

  // To create copies of variable length types, we'll need their actual sizes, and
  // so we need metadata for these no matter what...
  if(type == X_STRING || type == X_RAW) withMeta = TRUE;

  if(smaxSubscribe(table, key) != X_SUCCESS) return x_trace_null(fn, NULL);

  // For structs subscribe to leaf updates also
  if(!key) if(smaxSubscribe(table, "*") != X_SUCCESS) return x_trace_null(fn, NULL);

  m = (LazyMonitor *) calloc(1, sizeof(LazyMonitor));
  x_check_alloc(m);

  m->users = 1;
  m->table = xStringCopyOf(table);
  m->key = xStringCopyOf(key);

  if(withMeta) {
    m->meta = calloc(1, sizeof(XMeta));
    x_check_alloc(m->meta);
    m->meta->storeType = type;
  }

  id = xGetAggregateID(table, key);
  m->channel = calloc(1, sizeof(SMAX_UPDATES) + strlen(id));
  if(!m->channel) {
    free(m);
    x_error(0, errno, fn, "calloc() error (%d bytes)", sizeof(SMAX_UPDATES) + strlen(id));
    free(id);
    return NULL;
  }

  sprintf(m->channel, SMAX_UPDATES "%s", id);
  free(id);

  i = GetTableIndex(m);

  m->prev = NULL;
  m->next = monitorTable[i];
  if(m->next) m->next->prev = m;
  monitorTable[i] = m;
  m->isLinked = TRUE;

  // If this is our first lazy variable let's get the infrastructure in place (or refresh it)...
  if(nMonitors <= 0) {
    smaxAddSubscriber(NULL, ProcessLazyUpdates);    // Add/refresh the Redis subscriber to process lazy updates...
    nMonitors = 0;
  }

  nMonitors++;

  return m;
}

/**
 * Attempts to destroy (deallocate) a monitor point structure. It should be called only if the monitor point
 * is not in the monitor list, and assumed that the monitor'structure mutex is unlocked. If the monitor still has
 * active users, the call will return with FALSE, and let Release() handle the destruction when the monitor
 * point is no longer in use.
 *
 * \param m     Pointer to the variable's monitor point structure.
 *
 * \return      TRUE (non-zero) if the given monitor is destroyed, otherwise FALSE (0).
 *
 */
static boolean DestroyMonitorAsync(LazyMonitor *m) {
  if(m == NULL) return TRUE;

  if(m->isLinked) {
    fprintf(stderr, "WARNING! smax-lazy: Blocked attempt to destroy linked monitor point.\n");
    return FALSE;
  }

  if(m->users > 0) return FALSE;                        // We'll destroy it when last user releases it.

  // Monitor is idle, go on destroy it...
  // Free up allocations...
  if(m->channel != NULL) free(m->channel);
  if(m->data != NULL) {
    if(!m->key) xDestroyStruct((XStructure *) m->data);
    else free(m->data);
  }
  if(m->table != NULL) free(m->table);
  if(m->key != NULL) free(m->key);
  if(m->meta != NULL) free(m->meta);

  free(m);

  return TRUE;
}

/**
 * Returns the hash lookup index for a given update channel. It is the same index as what xGetHashLookupIndex()
 * would return for the variable that has been updated in the given notification channel
 *
 * \param channel       The redis notification channel (string) for the update.
 *
 * \return              An integer (0-255) just like for xGetHashLookupIndex().
 *
 * \sa xGetHashLookupIndex()
 *
 */
static int GetChannelLookupIndex(const char *channel) {
  int lGroup;
  char *key;

  // Skip the table update prefix for the channel...
  if(!strncmp(channel, SMAX_UPDATES, SMAX_UPDATES_LENGTH)) channel += SMAX_UPDATES_LENGTH;

  key = xLastSeparator(channel);
  if(!key) return 0;

  lGroup = key - channel;
  key += X_SEP_LENGTH;

  return smaxGetHashLookupIndex(channel, lGroup, key, 0);
}

static __inline__ int GetLookupIndex(const char *table, const char *key) {
  return key ? smaxGetHashLookupIndex(table, 0, key, 0) : GetChannelLookupIndex(table);
}

static __inline__ int GetTableIndex(const LazyMonitor *m) {
  return GetLookupIndex(m->table, m->key);
}

/**
 * Returns the monitor point for a given variable, or NULL if the variable is not currently monitored.
 * You must call Release() on the monitor point returned after you are done using it.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 *
 * \return          Pointer to the variable'structure monitor point structure, or NULL if it is not (yet)
 *                  being monitored.
 *
 * \sa Release()
 */
static LazyMonitor *GetSpecificMonitorAsync(const char *table, const char *key) {
  LazyMonitor *m;

  m = monitorTable[GetLookupIndex(table, key)];

  for(; m != NULL; m = m->next) if(!strcmp(m->table, table)) {
    if(m->key == NULL) {
      if(key != NULL) continue;
    }
    else if(strcmp(m->key, key)) continue;

    m->users++;
    return m;
  }

  return NULL;
}

/**
 * Returns the monitor point for a given variable, or NULL if the variable is not currently monitored.
 * You must call Release() on the monitor point returned after you are done using it.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 *
 * \return          Pointer to the variable'structure monitor point structure, or NULL if it is not (yet)
 *                  being monitored.
 *
 * \sa Release()
 */
static LazyMonitor *GetMonitorAsync(const char *table, const char *key) {
  LazyMonitor *m = GetSpecificMonitorAsync(table, key);
  if(!m) {
    // Try as struct...
    char *id = xGetAggregateID(table, key);
    if(!id) return NULL;
    m = GetSpecificMonitorAsync(id, NULL);
    free(id);
  }
  return m;
}

// ---------------------------------------------------------------------------
// Handling of PUB/SUB update notifications:
// ---------------------------------------------------------------------------

// TODO Surgical updates for structure fields.

/**
 * Callback function for processing lazy updates, added as a Redis subscriber routine.
 *
 * \sa smaxAddSubscriber()
 *
 */
static void ProcessLazyUpdates(const char *pattern, const char *channel, const char *msg, long length) {
  LazyMonitor *m;
  char *id;
  boolean checkParents = TRUE;

  (void) pattern;
  (void) length;

  if(!channel) return;

  xdprintf("SMA-X: lazy incoming on %s\n", channel);

  id = xStringCopyOf(channel);

  // If the message body has a <hmset> tag, then don't check for parent monitors.
  if(msg) if(strstr(msg, "<hmset>") || strstr(msg, "<nested>")) checkParents = FALSE;

  // Do the processing in single go so we do it in the shortest time possible
  // even if we must hold up others for a bit...
  // Nothing (apart from smaxLazyFlush()) blocks the mutex for prolonged periods,
  // so it's OK to wait just a little...
  pthread_mutex_lock(&monitorLock);

  // Loop to check for possibly monitored parents also...
  while(id) {
    // Find the monitor point for this update, and deal with it quickly!.
    m = monitorTable[GetChannelLookupIndex(id)];

    // Check through the monitors with the sam hash, to finf a match
    for( ; m != NULL; m = m->next) if(!strcmp(id, m->channel)) {
      xdprintf("SMA-X: Found lazy match for %s:%s.\n", m->table, m->key ? m->key : "");
      m->isCurrent = FALSE;
      m->updateCount++;

      if(++m->unpulledCount > MAX_UNPULLED_LAZY_UPDATES) {              // garbage collect...
        xdprintf("SMA-X: Unsubscribing from unused variable %s:%s.\n", m->table, m->key ? m->key : "");
        RemoveMonitorAsync(m);
        DestroyMonitorAsync(m);
      }
      else if(m->isCached) UpdateCachedAsync(m, TRUE);  // queue for a background update.

      // We found the match and dealt with it. Done with this particular ID.
      break;
    }

    // Don't check for parents of grouped updates (whose origin field is tagged with <hmset>)
    // We should (have) received the parent update notification separately.
    if(!checkParents) break;

    // If there is no parent structure, we are done checking.
    if(xSplitID(id, NULL) != X_SUCCESS) break;
  }

  pthread_mutex_unlock(&monitorLock);

  free(id);
}

