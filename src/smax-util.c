/**
 * \file
 *
 * \date Jun 25, 2019
 * \author Attila Kovacs
 *
 * \brief
 *      A collection of commonly used functions for the SMA-X library.
 */

#define _GNU_SOURCE               ///< for strcasecmp()
#define _POSIX_C_SOURCE 199309    ///< for clock_gettime()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // sleep()
#include <pthread.h>
#include <math.h>
#include <float.h>
#include <errno.h>
#include <ctype.h>

#include "smax-private.h"

#if SMAX_LEGACY
#  include "smax-legacy.h"
#endif


// Local prototypes ----------------------------------->
static void *SMAXReconnectThread(void *arg);

// Local variables ------------------------------------>

/// A lock for ensuring exlusive access for pipeline configuraton changes...
/// and the variables that it controls, e.g. via lockConfig()
static pthread_mutex_t configLock = PTHREAD_MUTEX_INITIALIZER;

static boolean isDisabled = FALSE;

/// \cond PROTECTED
/**
 * Obtain an exclusive lock for accessing or changing SMA-X configuration.
 *
 * \return      The result of pthread_mutex_lock().
 */
int smaxLockConfig() {
  int status = pthread_mutex_lock(&configLock);
  if(status) fprintf(stderr, "WARNING! SMA-X : smaxLockConfig() failed with code: %d.\n", status);
  return status;
}

/**
 * Release the exclusive lock to SMA-X configuration, so that others may access/update them also.
 *
 * \return      The result of pthread_mutex_unlock().
 */
int smaxUnlockConfig() {
  int status = pthread_mutex_unlock(&configLock);
  if(status) fprintf(stderr, "WARNING! SMA-X : smaxUnlockConfig() failed with code: %d.\n", status);
  return status;
}
/// \endcond

/**
 * Creates a new SMA-X metadata object with defaults. Effectively the same as calling
 * calloc() followed by xResetMeta().
 *
 * @return              Pointer to a new metadata object initialized to defaults.
 *
 * @sa X_META_INIT
 */
XMeta *smaxCreateMeta() {
  XMeta *m = (XMeta *) malloc(sizeof(XMeta));
  x_check_alloc(m);
  smaxResetMeta(m);
  return m;
}

/**
 * Set metadata to their default values. After resetting the supplied metadata
 * will have exactly the same content as if it were initialized with the
 * X_META_INIT macro.
 *
 * \param m     Pointer to the metadata that is to be cleared.
 *
 * @sa X_META_INIT
 */
void smaxResetMeta(XMeta *m) {
  XMeta def = X_META_INIT;

  if(!m) return;
  memcpy(m, &def, sizeof(*m));
}

/**
 * Returns the number of elements stored from a metadata.
 *
 * @param m     pointer to metadata that defines the dimension and shape of elements.
 * @return      the total number of elements represented by the metadata
 */
int smaxGetMetaCount(const XMeta *m) {
  int n = xGetElementCount(m->storeDim, m->storeSizes);
  prop_error("smaxGetMetaCount", n);
  return n;
}

/**
 * Sets the 'origin' field of an SMA-X metadata to the specified value,
 * truncating as necessary to fit into the allotted fixed storage.
 *
 * @param origin    The origination information, usually as hostname:progname
 * @param m         Pointer to metadata to set.
 */
void smaxSetOrigin(XMeta *m, const char *origin) {
  if(!m) return;
  if(!origin) *m->origin = '\0';
  else {
    strncpy(m->origin, origin, SMAX_ORIGIN_LENGTH - 1);
    m->origin[SMAX_ORIGIN_LENGTH - 1] = '\0';
  }
}

/**
 * The SMA-X error handler for Redis transmit (send or receive) errors. It prints a message to stderr, then
 * depending on whether SMA-X is in resilient mode, it will try to reconnect to SMA-X in the background, or else
 * exits the program with X_NO_SERVICE.
 *
 * @param redis     The Redis instance in which the error occurred. In case of SMA-X this will always
 *                  be the Redis instance used by SMA-X.
 * @param channel   The Redis channel index on which the error occured, such as REDIS_INTERAVTIVE_CHANNEL
 * @param op        The operation during which the error occurred, e.g. 'send' or 'read'.
 *
 * @sa smaxSetResilient()
 * @sa redisxSetTrasmitErrorHandler()
 */
// cppcheck-suppress constParameterPointer
// cppcheck-suppress constParameter
void smaxSocketErrorHandler(Redis *redis, enum redisx_channel channel, const char *op) {
  pthread_t tid;

  if(redis != smaxGetRedis()) {
    fprintf(stderr, "WARNING! SMA-X transmit error handling called with non-SMA-X Redis instance. Contact maintainer.\n");
    return;
  }

  // Get exclusive access for accessing 'isReconnecting' state variable.
  smaxLockConfig();

  if(isDisabled) {
    smaxUnlockConfig();
    return;
  }

  isDisabled = TRUE;
  smaxUnlockConfig();

  fprintf(stderr, "WARNING! SMA-X %s error on channel %d: %s.\n", op, channel, strerror(errno));

  if(!smaxIsResilient()) {
    fprintf(stderr, "ERROR! exiting program on SMA-X connection error.\n");
    exit(X_NO_SERVICE);
  }

  fprintf(stderr, "         (Further messages will be suppressed...)\n");

  // Force shutdown all existing clients.
  redisxShutdownClients(smaxGetRedis());

  if (pthread_create(&tid, NULL, SMAXReconnectThread, NULL) == -1) {
    perror("ERROR! SMA-X : pthread_create SMAXReconnectThread. Exiting.");
    exit(X_FAILURE);
  }
}

/**
 * Same as smaxScriptError(), but can be used after smaxConfigLock().
 *
 * @param name          The name of the calling function or name of script (whichever is more informative).
 * @param status        An approprioate error code from xchange.h to indicate the type of error.
 *
 * @sa smaxScriptError()
 * @sa smaxSetResilient()
 */
int smaxScriptErrorAsync(const char *name, int status) {
  pthread_t tid;
  const char *desc;

  if(!smaxIsConnected() || isDisabled) {
    return status;
  }

  switch(status) {
    case X_NULL: desc = "No such script, or script SHA not loaded."; break;
    case X_NO_SERVICE: desc = "Not in Redis."; break;
    default: desc = (char *) smaxErrorDescription(status);
  }

  fprintf(stderr, "WARNING! SMA-X LUA script error for %s: %s\n", name, desc);
  if(!smaxIsResilient()) {
    fprintf(stderr, "ERROR! exiting program on SMA-X / LUA script error.\n");
    exit(X_NO_SERVICE);
  }
  fprintf(stderr, "         (Further messages will be suppressed...)\n");

  if(!isDisabled) {
    isDisabled = TRUE;
    if (pthread_create(&tid, NULL, SMAXReconnectThread, NULL) == -1) {
      perror("ERROR! SMA-X : pthread_create SMAXReconnectThread. Exiting.");
      exit(X_FAILURE);
    }
  }

  return status;
}

/**
 * SMA-X error handler for when the LUA scripts do not execute. It prints a message to stderr, then
 * depending on whether SMA-X is in resilient mode, it will try to reconnect to SMA-X in the background, or else
 * exits the program with X_NO_SERVICE. You must not call this function with a locked config mutex (via
 * smaxConfigLock()). Instead use the async version of this function after smaxConfigLock().
 *
 * @param name          The name of the calling function or name of script (whichever is more informative).
 * @param status        An approprioate error code from xchange.h to indicate the type of error.
 *
 * @sa smaxScriptErrorAsync()
 * @sa smaxSetResilient()
 */
int smaxScriptError(const char *name, int status) {
  smaxLockConfig();
  status = smaxScriptErrorAsync(name, status);
  smaxUnlockConfig();
  return status;
}

/**
 * Prints a descriptive error message to stderr, and returns the error code.
 *
 * \param func      String that describes the function or location where the error occurred.
 * \param errorCode Error code that describes the failure.
 *
 * \return          Same error code as specified on input.
 */
int smaxError(const char *func, int errorCode) {
#if SMAX_LEGACY
  if(errorCode <= X_LEGACY_CODES) {
    if(xDebug) fprintf(stderr, "DEBUG-X> %4d (%s) in %s.\n", errorCode, smaxErrorDescription(errorCode), func);
    return errorCode;
  }
#endif

  // If in the process of reconnecting, we don't want to spam about transmit errors here, so just return the error code...
  if(errorCode == X_NO_SERVICE) if(isDisabled) return errorCode;

  return redisxError(func, errorCode);
}

/**
 * Returns a string description for one of the RM error codes.
 *
 * \param code      One of the error codes defined in 'xchange.h' or in 'smax.h' (e.g. X_NO_PIPELINE)
 *
 */
const char *smaxErrorDescription(int code) {
#if SMAX_LEGACY
  switch(code) {
    case X_MON_LIST_EMPTY: return "no variables are monitored";
    case X_IN_MON_LIST: return "variable is already monitored";
    case X_NOT_IN_MON_LIST: return "variable not monitored";
  }
#endif
  return redisxErrorDescription(code);
}

/**
 * \cond PROTECTED
 *
 * Destroys a pending pull request, freeing up its private resources. The caller should usually set the
 * referencing pointer to NULL after destruction to avoid problems later...
 *
 * \pram p      Pointer to the pull request structure.
 */
void smaxDestroyPullRequest(PullRequest *p) {
  if(p->group != NULL) free(p->group);
  if(p->key != NULL) free(p->key);
  free(p);
}

/**
 *
 * Returns a hash table lookup index for the given table (group) name and
 * Redis field (key) name.
 *
 * \param table     Hash table name
 * \param lTab      Number of characters to process from table, or 0 to use full string.
 * \param key       Key/field name
 * \param lKey      Number of characters to process from the key, or 0 to use full string.
 *
 * \return          An integer hash value (0-255).
 *
 */
unsigned char smaxGetHashLookupIndex(const char *table, int lTab, const char *key, int lKey) {
  unsigned long hash = 0;

  if(table)
    hash += smaxGetHash(table, lTab);

  if(key)
    hash += smaxGetHash(key, lKey);

  return (char) (hash & 0xff);
}
/// \endcond

/**
 * \cond PROTECTED
 *
 * A quick 32-bit integer hashing algorithm. It uses a combination of 32-bit XOR products and summing to
 * obtain something reasonably robust at detecting changes.
 *
 * \param buf       Pointer to the byte buffer to calculate a hash on
 * \param size      (bytes) Number of bytes from buf to calculate sum on, or &lt;=0 to
 *                  do it until string termination.
 *
 * \return          An integer hash value.
 */
long smaxGetHash(const char *buf, int size) {
  int i;
  long sum = 0;

  if(!buf) return SMAX_DEFAULT_HASH;
  if(size <= 0) size = strlen(buf);

  for(i = 0; i < size; i++)
    sum += buf[i] ^ i;  // Calculate a simple sum of all relevant bytes

  return sum;
}
/// \endcond

/**
 * Gets the SHA1 script ID for the currently loaded script with the specified name.
 *
 * \param scriptName    Case-sensitive name of the script, e.g. "GetStruct".
 * \param status        Pointer int which to return status, which is
 *                      X_SUCCESS if the SHA1 id was successfully obtained, or else an appropriate error code.
 *
 * \return              String buffer with the SHA1 key or NULL if it could not be retrieved.
 *                      (The caller is responsible freeing the buffer after use.)
 *
 */
char *smaxGetScriptSHA1(const char *scriptName, int *status) {
  static const char *fn = "smaxGetScriptSHA1";

  Redis *redis = smaxGetRedis();
  RESP *reply;
  char *sha1;

  if(!status) {
    x_error(0, EINVAL, fn, "output status is NULL");
    return NULL;
  }

  if(scriptName == NULL) {
    *status = x_error(X_NAME_INVALID, EINVAL, fn, "script name is NULL");
    return NULL;
  }

  if(!scriptName[0]) {
    *status = x_error(X_NAME_INVALID, EINVAL, fn, "script name is empty");
    return NULL;
  }

  if(!redis) {
    smaxError(fn, X_NO_INIT);
    return NULL;
  }

  reply = redisxRequest(redis, "HGET", SMAX_SCRIPTS, scriptName, NULL, status);
  if(*status) return x_trace_null(fn, NULL);

  *status = redisxCheckDestroyRESP(reply, RESP_BULK_STRING, 0);
  if(*status) return x_trace_null(fn, NULL);

  sha1 = (char *) reply->value;
  reply->value = NULL;

  redisxDestroyRESP(reply);

  return sha1;
}

/// \cond PROTECTED

/**
 *
 * \return <code>TRUE</code> (non-zero) if SMA-X is currently diabled (e.g. to reconnect), or else
 *         <code>FALSE</code> (zero).
 */
boolean smaxIsDisabled() {
  return isDisabled;
}

static void *SMAXReconnectThread(void *arg) {
  (void) arg;

  // Detach this thread (i.e. never to be joined...)
  pthread_detach(pthread_self());

  fprintf(stderr, "INFO: SMA-X will attempt to reconnect...\n");

  // Keep trying until successful
  if(smaxReconnect() == X_SUCCESS) fprintf(stderr, "INFO: SMA-X reconnected!\n");
  else {
    perror("ERROR! SMA-X reconnection failed");
    fprintf(stderr, "Good-bye.\n");
    exit(1);
  }

  // Wait for prior connection errors to clear up, before we exit 'reconnecting' state...
  sleep(SMAX_RECONNECT_RETRY_SECONDS);

  // Reset the reconnection status...
  smaxLockConfig();
  isDisabled = FALSE;
  smaxUnlockConfig();

  return NULL;
}

/// \endcond

/**
 * Prints the given UNIX time into the supplied buffer with subsecond precision.
 *
 * \param[in]   time    Pointer to time value.
 * \param[out]  buf     Pointer to string buffer, must be at least X_TIMESTAMP_LENGTH in size.
 *
 * \return      Number of characters printed, not including the terminating '\\0', or else
 *              an error code (&lt;0) if the `buf` argument is NULL.
 *
 */
__inline__ int smaxTimeToString(const struct timespec *time, char *buf) {
  if(!buf) return x_error(X_NULL, EINVAL, "smaxTimeToString", "output buffer is NULL");
  return sprintf(buf, "%lld.%06ld", (long long) time->tv_sec, (time->tv_nsec / 1000));
}

/**
 * Prints the current time into the supplied buffer with subsecond precision.
 *
 * \param[out]  buf     Pointer to string buffer, must be at least X_TIMESTAMP_LENGTH in size.
 *
 * \return      Number of characters printed, not including the terminating '\\0', or else
 *              an error code (&lt;0) if the `buf` argument is NULL.
 *
 */
int smaxTimestamp(char *buf) {
  struct timespec ts;
  int n;

  clock_gettime(CLOCK_REALTIME, &ts);
  n = smaxTimeToString(&ts, buf);
  prop_error("smaxTimestamp", n);
  return n;
}

/**
 * Parses a timestamp into broken-down UNIX time.
 *
 * \param[in]   timestamp     Timestamp string as returned in redis queries;
 * \param[out]  secs          Pointer to the returned UNIX time (seconds).
 * \param[out]  nanosecs      Pointer to the retuned sub-second remainder as nanoseconds, or NULL if nor requested.
 *
 * \return              X_SUCCESS(0)    if the timestamp was successfully parsed.
 *                      X_NULL          if there was no timestamp (empty or invalid string), or the `secs` argument is NULL.
 *                      X_PARSE_ERROR   if the seconds could not be parsed.
 *                      1               if there was an error parsing the nanosec part.
 *                      X_NULL          if the secs arhument is NULL
 */
int smaxParseTime(const char *timestamp, time_t *secs, long *nanosecs) {
  static const char *fn = "smaxParseTime";

  char *next;

  if(!timestamp) return x_error(X_NULL, EINVAL, fn, "input timestamp is NULL");
  if(!secs) return x_error(X_NULL, EINVAL, fn, "output seconds is NULL");

  errno = 0;
  *secs = (time_t) strtoll(timestamp, &next, 10);
  if(errno) {
    *nanosecs = 0;
    return x_error(X_PARSE_ERROR, ENOMSG, fn, "cannot parse seconds: '%s'", timestamp);
  }

  if(*next == '.') {
    char *end;
    double d;

    errno = 0;
    d = strtod(next, &end);
    if(errno) {
      *nanosecs = 0;
      return 1;
    }
    *nanosecs = (int) (1e9 * d);
  }
  else *nanosecs = 0;

  return X_SUCCESS;
}

/**
 * Returns the a sub-second precision UNIX time value for the given SMA-X timestamp
 *
 * \param timestamp     The string timestamp returned by SMA-X
 *
 * \return      Corresponding UNIX time with sub-second precision, or NAN if the input could not be parsed.
 */
double smaxGetTime(const char *timestamp) {
  static const char *fn = "smaxGetTime";

  time_t sec;
  long nsec;

  if(timestamp == NULL) {
    x_error(X_NULL, EINVAL, fn, "input timestamp is NULL");
    return NAN;
  }

  if(smaxParseTime(timestamp, &sec, &nsec) < 0) {
    x_trace(fn, NULL, 0);
    return NAN;
  }

  return sec + 1e-9 * nsec;
}

/**
 * Creates a generic field of a given name and type and dimensions using the specified native values.
 * It is like `xCreateField()` except that the field is created in serialized form for SMA-X.
 *
 * \param name      Field name
 * \param type      Storage type, e.g. X_INT.
 * \param ndim      Number of dimensionas (1:20). If ndim < 1, it will be reinterpreted as ndim=1, size[0]=1;
 * \param sizes     Array of sizes along each dimensions, with at least ndim elements, or NULL with ndim<1.
 * \param value     Pointer to the native data location in memory. Unless it is of type X_STRUCT,
 *                  the data stored in the field is a copy (for type X_RAW) or serialized string (otherwise).
 *
 * \return          A newly created field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreateField(const char *name, XType type, int ndim, const int *sizes, const void *value) {
  static const char *fn = "smaxCreateField";
  int n;
  XField *f;

  if(type != X_RAW && type != X_STRING) if(xStringElementSizeOf(type) < 1) return x_trace_null(fn, NULL);

  if(type == X_RAW || type == X_STRUCT) {
    f = xCreateField(name, type, ndim, sizes, value);
    return f ? f : x_trace_null(fn, NULL);
  }

  n = xGetElementCount(ndim, sizes);
  if(n < 1) return x_trace_null(fn, NULL);

  f = xCreateField(name, type, ndim, sizes, NULL);
  if(!f) return x_trace_null(fn, NULL);

  f->value = smaxValuesToString(value, type, n, NULL, 0);
  f->isSerialized = TRUE;

  return f;
}

/**
 * Converts a standard xchange field (with a native value storage) to an SMA-X field with
 * serialized string value storage.
 *
 * @param[in, out] f     Pointer to field to convert
 * @return      X_SUCCESS (0) if successful, or
 *              X_NULL if the input field or the serialized value is NULL.
 *
 * @sa smax2xField()
 * @sa x2smaxStruct()
 */
int x2smaxField(XField *f) {
  static const char *fn = "x2smaxField";

  void *value;

  if(!f) return x_error(X_NULL, EINVAL, fn, "field is NULL");
  if(!f->value) return X_SUCCESS;
  if(f->type == X_RAW) return X_SUCCESS;
  if(f->type == X_STRUCT) {
    f->isSerialized = TRUE;
    prop_error(fn, x2smaxStruct((XStructure *) f->value));
    return X_SUCCESS;
  }
  if(f->type == X_FIELD) {
    // Convert an array of fields into a structure, with fields whose
    // names are '.' + 1-based index, i.e. '.1', '.2'...
    const XField *array = (XField *) f->value;
    XStructure *s = xCreateStruct();
    int i;

    for(i = xGetFieldCount(f); --i >= 0;) {
      char fname[20];

      XField *e = (XField *) calloc(1, sizeof(XField));
      x_check_alloc(e);

      *e = array[i];    // shallow copy the array field
      prop_error(fn, x2smaxField(e));   // then convert the copy to smax

      sprintf(fname, ".%d", (i + 1)); // Label each field with the array index
      e->name = xStringCopyOf(fname);
      x_check_alloc(e->name);

      e->next = s->firstField; // As to the structure...
      s->firstField = e;
    }

    // clear the original field
    xClearField(f);

    // Set the converted structure as the new data
    f->type = X_STRUCT;
    f->value = (char *) s;

    return X_SUCCESS;
  }
  if(f->isSerialized) return X_SUCCESS;

  value = f->value;
  f->value = smaxValuesToString(value, f->type, xGetFieldCount(f), NULL, 0);
  free(value);

  f->isSerialized = TRUE;

  if(!f->value) return x_trace(fn, NULL, X_NULL);

  return X_SUCCESS;
}

/**
 * Converts SMA-X field with serialized string value storage to a standard xchange field
 * with a native value storage.
 *
 * @param f     Pointer to field to convert
 * @return      X_SUCCESS (0) if successful, or
 *              X_NULL if the input field or the deserialized value is NULL,
 *              X_TYPE_INVALID if the field is of a type that cannot be deserialized,
 *              or else an error code returned by smaxStringToValues().
 *
 * @sa x2smaxField()
 * @sa smax2xStruct()
 */
int smax2xField(XField *f) {
  static const char *fn = "smax2xField";

  void *str;
  int pos = 0, result, count, eSize;

  if(!f) return x_error(X_NULL, EINVAL, fn, "field is NULL");
  if(!f->value) return X_SUCCESS;
  if(f->type == X_RAW) return X_SUCCESS;
  if(f->type == X_STRUCT) {
    f->isSerialized = FALSE;
    prop_error(fn, smax2xStruct((XStructure *) f->value));
    return X_SUCCESS;
  }
  if(!f->isSerialized) return X_SUCCESS;

  eSize = xElementSizeOf(f->type);
  if(eSize <= 0) return x_trace(fn, NULL, X_TYPE_INVALID);

  count = xGetFieldCount(f);
  if(count <= 0) return x_trace(fn, NULL, X_SIZE_INVALID);

  str = f->value;
  f->value = calloc(count, eSize);
  if(!f->value) {
    free(str);
    return x_error(X_NULL, errno, fn, "calloc() error (%d x %d)", count, eSize);
  }

  result = smaxStringToValues(str, f->value, f->type, count, &pos);
  free(str);

  f->isSerialized = FALSE;

  prop_error(fn, result);
  return X_SUCCESS;
}

/**
 * Converts a standard xchange structure (with a native value storage) to an SMA-X structure with
 * serialized string value storage.
 *
 * @param s     Pointer to structure to convert
 * @return      X_SUCCESS (0) if successful, or
 *              X_STRUCT_INVALID if the structure is NULL, or had a NULL substructure.
 *              X_NULL if there was a field that could not be converted.
 *
 * @sa smax2xStruct()
 * @sa x2smaxField()
 */
int x2smaxStruct(XStructure *s) {
  static const char *fn = "x2smaxStruct";

  XField *f;
  int status = X_SUCCESS;

  if(!s) return x_error(X_STRUCT_INVALID, EINVAL, fn, "input structure is NULL");

  for(f = s->firstField; f; f = f->next) {
    int res = x2smaxField(f);
    if(!status) status = res;
  }

  prop_error(fn, status);
  return X_SUCCESS;
}

/**
 * Converts an SMA-X structure with serialized string value storage to a standard xchange structure
 * with a native value storage.
 *
 * @param s     Pointer to structure to convert
 * @return      X_SUCCESS (0) if successful, or
 *              X_STRUCT_INVALID if the structure is NULL, or had a NULL substructure,
 *              or else an error code returned by smax2xField().
 *
 * @sa x2smaxStruct()
 * @sa smax2xField()
 */
int smax2xStruct(XStructure *s) {
  static const char *fn = "smax2xStruct";

  XField *f;
  int status = X_SUCCESS;

  if(!s) return x_error(X_STRUCT_INVALID, EINVAL, fn, "input structure is NULL");

  for(f = s->firstField; f; f = f->next) {
    int res = smax2xField(f);
    if(!status) status = res;
  }

  prop_error(fn, status);
  return status;
}

/**
 * Returns the current time on the Redis server instance.
 *
 * @param t         Pointer to a timespec structure in which to return the server time.
 * @return          X_SUCCESS (0) if successful, or X_NO_INIT if not connected to SMA-X, or X_NULL
 *                  if either argument is NULL, or X_PARSE_ERROR if could not parse the response,
 *                  or another error returned by redisxCheckRESP().
 */
int smaxGetServerTime(struct timespec *t) {
  static const char *fn = "smaxGetServerTime";
  Redis *r = smaxGetRedis();

  if(!r) return smaxError(fn, X_NO_INIT);

  prop_error(fn, redisxGetTime(r, t));
  return X_SUCCESS;
}

/**
 * Serializes binary values into a string representation (for Redis).
 *
 * \param[in]       value         Pointer to an array of values, or NULL to produce all zeroes.
 *                                If type is X_STRING value should be a pointer to a char** (array of string
 *                                pointers), as opposed to X_CHAR(n), which expects a contiguous char* buffer with
 *                                [n * eCount] length (Note, a char[eCount][n] is equivalent to such a char* buffer).
 *
 * \param[in]       type          Share type, e.g. X_DOUBLE. All type except X_STRUCT are supported.
 *
 * \param[in]       eCount        Number of elements (ignored for X_RAW).
 *
 * \param[in,out]   trybuf        (optional) An optional pointer to a buffer that will be used if sufficient (can be NULL).
 *
 * \param[in]       trylength     (optional) Size of the optional buffer.
 *
 *
 * \return              The pointer to the string buffer holding the ASCII values. It may be
 *                      the supplied buffer (if sufficient), the input value (if type is X_RAW)
 *                      or else a dynamically allocated buffer, or NULL if the key is malformed.
 *                      If the returned value is neither the input value nor trybuf, then the caller
 *                      is responsible for calling free() on the dynamically allocated buffer after
 *                      use.
 */
char *smaxValuesToString(const void *value, XType type, int eCount, char *trybuf, int trylength) {
  static const char *fn = "smaxValuedToString";

  int eSize = 1, k, stringSize;
  char *sValue, *next;

  if(value == NULL) type = X_UNKNOWN;                   // Print zero(es) for null value.
  if(type == X_STRUCT) {
    x_error(0, EINVAL, fn, "structures not allowed");
    return NULL;                     // structs are not serialized by this function.
  }
  if(type == X_RAW) if(value) return *(char **) value;

  // Figure out how big the serialized string might be...
  if(type == X_UNKNOWN) stringSize = 2 * eCount;
  else if(type == X_STRING) {
    char **S = (char **) value;
    stringSize = 1;
    for(k = 0; k < eCount; k++) stringSize += (S[k] ? strlen(S[k]) : 0) + 1;
  }
  else {
    eSize = xElementSizeOf(type);
    if(eSize <= 0) return x_trace_null(fn, NULL);       // Unsupported element type...
    stringSize = eCount * xStringElementSizeOf(type);
  }

  if(stringSize <= 0) stringSize = 1;                   // empty string

  // Use the supplied buffer if large enough, or dynamically allocate one.
  if(trybuf != NULL && stringSize <= trylength) sValue = trybuf;
  else {
    sValue = (char *) malloc(stringSize);
    x_check_alloc(sValue);
  }

  // If we got this far with raw type, it's because it was a null value, so return an empty string...
  if(type == X_RAW) {
    *sValue = '\0';
    return sValue;
  }

  next = sValue;

  if(!value) {
    if(type == X_STRING || xIsCharSequence(type)) for(k=0; k<eCount; k++) *(next++) = '\r';
    else for(k=0; k<eCount; k++) next += sprintf(next, "0 ");
  }

  else if(xIsCharSequence(type)) {
    char *c = (char *) value;

    *sValue = '\0';         // Default empty string...

    for(k=0; k<eCount; k++) {
      int L;

      // Copy at most eSize characters from c[] to next[]...
      for(L = 0; L < eSize; L++) {
        if(c[L] == '\0') break;
        *(next++) = c[L];
      }
      *(next++) = '\r';     // Add a separator...
      c = c + eSize;        // point to the next element.
    }
  }

  // For all the types...
  else switch(type) {
    case X_BOOLEAN: {
      const boolean *b = (boolean *) value;
      for(k=0; k<eCount; k++) next += sprintf(next, "%c ", (b[k] != 0 ? '1' : '0'));
      break;
    }

    case X_BYTE: {
      const char *c = (const char *) value;
      for(k=0; k<eCount; k++) next += sprintf(next, "%hhd ", c[k]);
      break;
    }

    case X_FLOAT: {
      const float *f = (const float *) value;
      for(k=0; k<eCount; k++) {
        next += xPrintFloat(next, f[k]);
        *(next++) = ' ';
      }
      break;
    }

    case X_DOUBLE: {
      const double *d = (const double *) value;
      for(k=0; k<eCount; k++) {
        next += xPrintDouble(next, d[k]);
        *(next++) = ' ';
      }
      break;
    }

    case X_STRING: {
      char **S = (char **) value;
      for(k=0; k<eCount; k++) next += sprintf(next, "%s\r", S[k] ? S[k] : "");
      break;
    }

    default:
      // Check for possibly overlapping types
      if(type == X_SHORT) {
        const short *s = (const short *) value;
        for(k=0; k<eCount; k++) next += sprintf(next, "%hd ", s[k]);
      }
      else if(type == X_INT) {
        const int *i = (const int *) value;
        for(k=0; k<eCount; k++) next += sprintf(next, "%d ", i[k]);
      }
      else if(type == X_LONG) {
        const long *l = (const long *) value;
        for(k=0; k<eCount; k++) next += sprintf(next, "%ld ", l[k]);
      }
      else if(type == X_LLONG) {
        const long long *ll = (const long long *) value;
        for(k=0; k<eCount; k++) next += sprintf(next, "%lld ", ll[k]);
      }
      else
        for(k=0; k<eCount; k++) next += sprintf(next, "0 ");
  }

  // Replace trailing item separator with string termination.
  if(next > sValue) *(next-1) = '\0';

  return sValue;
}

/**
 * Returns a pointer to the start of the next (space-separated) token.
 *
 * @param str   Starting parse position.
 * @return      Pointer to the start of the next token.
 */
static char *NextToken(char *str) {
  for(; *str; str++) if(isspace(*str)) break;   // Advance to the next empty space (end of current token)
  for(; *str; str++) if(!isspace(*str)) break;  // Advance to the next non-empty space (start of next token)
  return str;
}

static __inline__ void CheckParseError(char **next, int *status) {
  if(errno) {
    *next = NextToken(*next);
    *status = X_PARSE_ERROR;
  }
}

/**
 * Deserializes a string to binary values.
 *
 * \param[in]   str           Serialized ASCII representation of the data (as stored by Redis).
 *
 * \param[out]  value         Pointer to the buffer that will hold the binary values. The caller is responsible
 *                            for ensuring the buffer is sufficiently sized for holding the data for the
 *                            given variable.
 *
 * \param[in]   type          Share type, e.g. X_INT. The types X_RAW, X_STRUCT are not supported
 *                            by this function.
 *
 * \param[in]   eCount        Number of elements to retrieve. Ignored for X_STRUCT.
 *
 * \param[out]  pos           Parse position, i.e. the number of characters parsed from the input string...
 *
 * \return                    Number of elements successfully parsed, or a negative error code:
 *
 *                             X_NULL               If the value or str argument is NULL.
 *                             X_TYPE_INVALID       If the type is not supported.
 *                             X_SIZE_INVALID       If size is invalid (e.g. X_RAW, X_STRUCT)
 *                             X_PARSE_ERROR        If the tokens could not be parsed in the format expected
 */
int smaxStringToValues(const char *str, void *value, XType type, int eCount, int *pos) {
  static const char *fn = "smaxStringToValues";

  char *next, *c = (char *) value;
  int status = 0, eSize, k;


  if(value == NULL) return x_error(X_NULL, EINVAL, fn, "value is NULL");
  if(eCount <= 0) return x_error(X_SIZE_INVALID, EINVAL, fn, "invalid count: %d", eCount);

  if(type == X_RAW || type == X_STRUCT) return x_error(X_TYPE_INVALID, EINVAL, fn, "X_RAW or X_STRUCT not allowed");

  if(type == X_STRING) {
    int n = smaxUnpackStrings(str, strlen(str), eCount, (char **) value);
    prop_error(fn, n);
    return n;
  }

  eSize = xElementSizeOf(type);
  if(eSize <= 0) return x_trace(fn, NULL, X_SIZE_INVALID);

  if(str == NULL) {
    xZero(value, type, eCount);
    return x_error(X_NULL, EINVAL, fn, "input string is NULL");
  }

  next = (char *) str;

  *pos = 0;

  if(xIsCharSequence(type)) {
    for(k=0; k<eCount; k++) {
      int j;

      // Copy at most eSize characters from each '\r'-separated sequence in the string input...
      for(j = 0; *next; next++) {
        if(*next == '\r') { next++; break; }     // Advance to the start of the next string element...
        if(j < eSize) c[j++] = *next;            // Store characters up to the requested size, including '\0'. Discard beyond...
      }

      if(j < eSize) c[j] = '\0';   // Terminate the current element, if there is room.
      c += eSize;    // point to the next element.
    }
  }

  else {
    // Parse numerical type
    switch(type) {
      case X_BOOLEAN: {
        boolean *b = (boolean *) value;
        for(k=0; k<eCount && *next; k++) {
          b[k] = xParseBoolean(next, &next);
          CheckParseError(&next, &status);
        }
        break;
      }

      case X_BYTE:
        for(k=0; k<eCount && *next; k++) {
          errno = 0;
          c[k] = (char) strtol(next, &next, 0);
          CheckParseError(&next, &status);
        }
        break;

      case X_FLOAT: {
        float *f = (float *) value;
        for(k=0; k<eCount && *next; k++) {
          f[k] = xParseFloat(next, &next);
          CheckParseError(&next, &status);
        }
        break;
      }

      case X_DOUBLE: {
        double *d = (double *) value;
        for(k=0; k<eCount && *next; k++) {
          d[k] = xParseDouble(next, &next);
          CheckParseError(&next, &status);
        }
        break;
      }

      default:
        // Check for possibly overlapping types...
        if(type == X_SHORT) {
          short *s = (short *) value;
          for(k=0; k<eCount && *next; k++) {
            errno = 0;
            s[k] = (short) strtol(next, &next, 0);
            CheckParseError(&next, &status);
          }
        }
        else if(type == X_INT) {
          int *i = (int *) value;
          for(k=0; k<eCount && *next; k++) {
            errno = 0;
            i[k] = (int) strtol(next, &next, 0);
            CheckParseError(&next, &status);
          }
        }
        else if(type == X_LONG) {
          long *l = (long *) value;
          for(k=0; k<eCount && *next; k++) {
            errno = 0;
            l[k] = strtol(next, &next, 0);
            CheckParseError(&next, &status);
          }
        }
        else if(type == X_LLONG) {
          long long *ll = (long long *) value;
          for(k=0; k<eCount && *next; k++) {
            errno = 0;
            ll[k] = (int) strtoll(next, &next, 0);
            CheckParseError(&next, &status);
          }
        }
        else
          return x_error(X_TYPE_INVALID, EINVAL, fn, "unsupported type: %d", type);         // Unknown type...
    }

    // Zero out the remaining elements...
    if(k < eCount) xZero(&c[k], type, eCount - k);
  }

  *pos = next - str;

  prop_error(fn, status);

  return k;
}

static char *smaxStringForIntSize(int n) {
  switch(n) {
    case 1: return "int8";
    case 2: return "int16";
    case 4: return "int32";
    case 8: return "int64";
    default:
      x_error(0, EINVAL, "smaxStringForIntSize", "invalid SMA-X int type: %d", (8 * n));
      return "unknown";
  }
}

/**
 * Returns the string type for a given XType argument as a constant expression. For examples X_LONG -> "int64".
 *
 * \param type      SMA-X type, e.g. X_FLOAT
 *
 * \return          Corresponding string type, e.g. "float". (Default is "string" -- since typically
 *                  anything can be represented as strings.)
 *
 * \sa smaxTypeForString()
 */
char *smaxStringType(XType type) {
  if(type < 0) return "string";         // X_CHAR(n), legacy fixed size strings.

  switch(type) {
    case X_BOOLEAN: return "boolean";
    case X_BYTE: return smaxStringForIntSize(sizeof(char));
    case X_INT16: return smaxStringForIntSize(sizeof(int16_t));
    case X_INT32: return smaxStringForIntSize(sizeof(int32_t));
    case X_INT64: return smaxStringForIntSize(sizeof(int64_t));
    case X_FLOAT: return "float";
    case X_DOUBLE: return "double";
    case X_STRING: return "string";
    case X_RAW: return "raw";
    case X_STRUCT: return "struct";
    case X_UNKNOWN:
    default:
      x_error(0, EINVAL, "smaxStringType", "invalid SMA-X type: %d", type);
      return "unknown";
  }
}

static XType smaxIntTypeForBytes(size_t n) {
  if(n > sizeof(int32_t)) return X_INT64;
  if(n > sizeof(int16_t)) return X_INT32;
  if(n > sizeof(int8_t)) return X_INT16;
  return X_BYTE;
}

/**
 * Returns the XType for a given case-sensitive type string. For example "float" -> X_FLOAT. The value "raw" will
 * return X_RAW.
 *
 * \param type      String type, e.g. "struct".
 *
 * \return          Corresponding XType, e.g. X_STRUCT. (The default return value is X_RAW, since all Redis
 *                  values can be represented as raw strings.)
 *
 * \sa smaxStringType()
 */
XType smaxTypeForString(const char *type) {
  if(!type) return X_RAW;
  if(!strcmp("int", type) || !strcmp("integer", type)) return X_INT;
  if(!strcmp("boolean", type) || !strcmp("bool", type)) return X_BOOLEAN;
  if(!strcmp("int8", type)) return smaxIntTypeForBytes(1);
  if(!strcmp("int16", type)) return smaxIntTypeForBytes(2);
  if(!strcmp("int32", type)) return smaxIntTypeForBytes(4);
  if(!strcmp("int64", type)) return smaxIntTypeForBytes(8);
  if(!strcmp("float", type)) return X_FLOAT;
  if(!strcmp("float32", type)) return X_FLOAT;
  if(!strcmp("float64", type)) return X_DOUBLE;
  if(!strcmp("double", type)) return X_DOUBLE;
  if(!strcmp("string", type) || !strcmp("str", type)) return X_STRING;
  if(!strcmp("struct", type)) return X_STRUCT;
  if(!strcmp("raw", type)) return X_RAW;

  return x_error(X_UNKNOWN, EINVAL, "smaxTypeForString", "invalid SMA-X type: '%s'", type);
}

/**
 * Returns an array of dynamically allocated strings from a packed buffer of consecutive 0-terminated
 * or '\\r'-separated string elements.
 *
 * \param[in]   data      Pointer to the packed string data buffer.
 * \param[in]   len       length of packed string (excl. termination).
 * \param[in]   count     Number of string elements expected. If fewer than that are found
 *                        in the packed data, then the returned array of pointers will be
 *                        padded with NULL.
 * \param[out]  dst       An array of string pointers (of size 'count') which will point to
 *                        dynamically allocated string (char*) elements. The array is assumed
 *                        to be uninitialized, and elements will be allocated as necessary.
 *
 * \return                X_SUCCESS (0) if successful, or X_NULL if one of the argument
 *                        pointers is NULL, or else X_INCOMPLETE if some of the components
 *                        were too large to unpack (alloc error).
 */
int smaxUnpackStrings(const char *data, int len, int count, char **dst) {
  static const char *fn = "smaxUnpackStrings";

  int i, offset = 0;

  if(!data) return x_error(X_NULL, EINVAL, fn, "input packed string 'data' is NULL");
  if(!dst) return x_error(X_NULL, EINVAL, fn, "output unpacked string 'dst' is NULL");

  // Make sure the data has proper string termination at its end, so we don't overrun...
  //data[len] = '\0';

  for(i=0; i<count && offset < len; i++) {
    const char *from = &data[offset];
    int l;

    for(l=0; from[l] && offset + l < len; l++) if(from[l] == '\r') break;

    dst[i] = (char *) malloc(l + 1);
    if(!dst[i]) return x_error(X_INCOMPLETE, errno, fn, "malloc() error (%d bytes)", (l+1));

    if(l) memcpy(dst[i], from, l);
    dst[i][l] = '\0'; // termination...

    offset += l + 1;
  }

  // Pad remaining elements with empty strings...
  for(; i < count; i++) {
    dst[i] = calloc(1, sizeof(char));
    if(!dst[i]) return x_error(X_INCOMPLETE, errno, fn, "calloc() error (1x1 byte)");
  }

  return X_SUCCESS;
}

// The following is not available on prior to the POSIX.1-2001 standard
#if _POSIX_C_SOURCE >= 200112L

/**
 * Deletes variables and metadata from SMA-X.
 *
 * @param pattern   Glob variable name pattern
 * @return          The number of variables deleted from the SQL DB
 */
int smaxDeletePattern(const char *pattern) {
  static const char *fn = "smaxDeletePattern";

  Redis *r = smaxGetRedis();
  char *metaPattern;
  int n;

  if(!r) return smaxError(fn, X_NO_INIT);


  n = redisxDeleteEntries(r, pattern);
  prop_error(fn, n);

  metaPattern = (char *) malloc(strlen(pattern) + 20);
  if(!metaPattern) return x_error(X_NULL, errno, fn, "malloc() error (%ld bytes)", (long) strlen(pattern) + 20);

  sprintf(metaPattern, "<*>" X_SEP "%s", pattern);
  redisxDeleteEntries(r, metaPattern);
  free(metaPattern);

  return n;
}

#endif
