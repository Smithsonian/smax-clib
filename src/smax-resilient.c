/**
 * \file
 *
 * \date Aug 14, 2019
 * \author Attila Kovacs
 *
 * \brief
 *      This module adds trusty push delivery to SMA-X. If the server cannot be reached,
 *      push requests are stored and updated locally until the server connection is
 *      restored, at which point they are delivered.
 *
 *      This way, push requests are guaranteed to make it to the database sooner or later
 *      as long as the calling program keeps running.
 *
 *      It's mainly useful for daemons that generate infrequent data for the database.
 *      It's not especially meaningful for simple executables, which are run for limited
 *      time without persistence.
 *
 *
 * ## Lazy pulling (high-frequency queries)
 *
 * What happens if you need the data frequently? Do you pound on the database at some high-frequency? No, you probably
 * no not want to do that, especially if the data you need is not necessaily changing fast. There is no point on wasting
 * network bandwidth only to return the same values again and again. This is where 'lazy' pulling excels.
 *
 * From the caller's perspective lazy pulling works just like regular SMA-X pulls, e.g.:
 *
 * ```c
 *   int data[10][4][2];
 *   int sizes[] = { 10, 4, 2 };
 *   XMeta meta;
 *
 *   int status = smaxLazyPull("some_table", "some_data", X_INT, 3, sizes, data, &meta);
 * ```
 *
 * or
 *
 * ```c
 *   int status = smaxLazyPullDouble("some_table", "some_var");
 * ```
 *
 * But, under the hood, it does something different. The first time a new variable is lazy pulled it is fetched from the
 * Redis database just like a regular pull. But, it also will cache the value, and watch for update notifications from
 * the SMA-X server. Thus, as long as no update notification is received, successive calls will simply return the locally
 * cached value. This can save big on network usage, and also provides orders of magnitude faster access so long as the
 * variable remains unchanged.
 *
 * When the vatiable is updated in SMA-X, our client library will be notified, and one of two things can happen:
 *
 * 1. it invalidates the cache, so that the next lazy pull will again work just like a regular pull, fetching the
 *    updated value from SMA-X on demand. And again the library will cache that value and watch for notifications for
 *    the next update. Or,
 *
 * 2. it will trigger a background process to update the cached value in the background with a pipelined
 *    (high-throughput) pull. However, until the new value is actually fetched, it will return the previously cached
 *    value promptly.
 *
 * The choice between the two is yours, and you can control which suits your need best. The default behavior for lazy pulls
 * is (1), but you may call `smaxLazyCache()` after the first pull of a variable, to indicate that you want to enable
 * background cache updates (2) for it. The advantage of (1) is that it will never serve you outdated data even if there
 * are significant network latencies -- but you may have to wait a little to fetch updates. On the other hand (2) will
 * always provide a recent value with effectively no latency, but this value may be outdated if there are delays on the
 * network updating the cache. The difference is typically at the micro-seconds level on a local LAN. However, (2) may
 * be preferable when you need to access SMA-X data from timing critical code blocks, where it is more important to ensure
 * that the value is returned quickly, rather than whether it is a millisecond too old or not.
 *
 * In either case, when you are done using lazy variables, you should let the library know that it no longer needs to watch
 * updates for these, by calling either `smaxLazyEnd()` on specific variables, or else `smaxLazyFlush()` to stop watching
 * updates for all lazy variables. (A successive lazy pull will automatically start watching for updates again, in case you
 * wish to re-enable).
 *
 * ```c
 *   // Lazy pull a bunch of data (typically in a loop).
 *   for(...) {
 *     smaxLazyPull("some_table", "some_var", ...);
 *     smaxLaxyPull(...);
 *     ...
 *   }
 *
 *   // Once we do not need "some_table:some_var" any more:
 *   smaxLazyEnd("some_table", "some_var");
 *
 *   ...
 *
 *   // And to stop lazy accessing all
 *   smaxLazyFlush();
 * ```
 *
 *      \sa smaxSetResilient()
 *      \sa smaxIsResilient()
 */


#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#include "smax-private.h"

typedef struct PushRequest {
  char *group;
  XField *field;
  struct PushRequest *next;
} PushRequest;

static PushRequest *table[SMAX_LOOKUP_SIZE];
static pthread_mutex_t tableLock = PTHREAD_MUTEX_INITIALIZER;

static void SendStoredPushRequests();
static void UpdatePushRequest(const char *table, const XField *field);
static void DestroyPushRequest(PushRequest *req);

// Always initialize with FALSE...
static boolean resilient;
static int nPending;
static boolean exitAfterSync = TRUE;

/**
 * Enables the resiliency feature of the library, which keeps track of local changes destined to the
 * database when the database is not reachable, and sending all locally stored updates once the
 * database comes online again. However, after sending all pending updates to the remote server,
 * the program may exit (default behavior), unless smaxSetResilientExit() is set to FALSE (0),
 * so that it can be restarted  in a fresh state, setting up subscriptions and scripts again as necessary.
 *
 * \param value     TRUE (non-zero) to enable, or FALSE (0) to disable resiliency.
 *
 * @sa smaxIsResilient()
 * @sa smaxSetResilientExit()
 */
void smaxSetResilient(boolean value) {
  pthread_mutex_lock(&tableLock);

  if(value && !resilient) {
    xvprintf("SMA-X: Activating resilient mode.\n");
    smaxAddConnectHook(SendStoredPushRequests);
  }
  else if(!value && resilient) {
    xvprintf("SMA-X: De-activating resilient mode.\n");
    smaxRemoveConnectHook(SendStoredPushRequests);
  }

  resilient = value ? TRUE : FALSE;

  pthread_mutex_unlock(&tableLock);
}

/**
 * Checks whether the resiliency feature has been enabled.
 *
 * \return      TRUE if enabled, otherwise FALSE.
 *
 * @sa smaxSetResilient()
 */
boolean smaxIsResilient() {
  return resilient;
}

/**
 * Sets whether the program should exit in resilient mode, after having pushed all local updates.
 * The default is to exit since the reconnecting in resilient mode does not by itself re-establish
 * existing subscriptions. However, when subscriptions aren't used, or if they are set up
 * as a connect hook, the user may want the program to simply continue. This is possible by passing
 * FALSE (0) as the argument to this call. This setting only takes effect when resilient mode
 * is enabled. Otherwise, the exit policy is set by the RedisX library.
 *
 * @param value     Whether to exit the program after all local updates have been pushed to SMA-X
 *                  after a recovering from an outage.
 *
 * @sa smaxSetResilient()
 * @sa smaxAddConnectHook()
 */
void smaxSetResilientExit(boolean value) {
  exitAfterSync = value ? TRUE : FALSE;
}

/**
 * \cond PROTECTED
 *
 * Stores a push requests for sending later, e.g. because of failure to send immediately. If a
 * there is an existing stored value for the given table/field, it is updated with the new value.
 * If and when SMA-X is successfully reconnected, the accumulated locally stored push requests
 * will be sent to the SMA-X server. And, after all locally stored changes have been successfully
 * sent to the remote, the program will exit with X_FAILURE (-1), both to indicate an error, and
 * to proivide a chance for the program to restart in a clean state with the nexessary subscriptions
 * or local LUA scripts it may need to reload into the database.
 *
 * \param group     The SMA-X group, i.e. Redis table, name.
 * \param field     The field data to share.
 *
 * \return          X_SUCCESS       on success
 *                  X_NULL          if one of the arguments is null
 *                  X_NAME_INVALID  if the field's name is NULL or empty string.
 */
int smaxStorePush(const char *group, const XField *field) {
  static const char *fn = "smaxStorePush";

  if(field == NULL) return x_error(X_NULL, EINVAL, fn, "field is NULL");

  if(field->type == X_STRUCT) {

    const XStructure *s = (XStructure *) field->value;
    const XField *f;
    char *id;
    int status = X_SUCCESS;

    id = xGetAggregateID(group, field->name);
    if(!id) return x_trace(fn, NULL, X_NULL);

    for(f = s->firstField; f != NULL; f = f->next) if(smaxStorePush(id, f) != X_SUCCESS) {
      status = X_INCOMPLETE;
      break;
    }

    free(id);

    prop_error(fn, status);
  }
  else UpdatePushRequest(group, field);

  return X_SUCCESS;
}
/// \endcond

/**
 * Sends all previously undelivered and stored push requests to Redis, and exists the program with X_FAILURE (-1),
 * unless smaxSetResilientExit(FALSE) was previously set, after all penbding updates were successfully propagated.
 * Otherwise, if the connection breaks again while synching the local updates, it will try again after the next reconnection.
 *
 * @sa smaxStorePush()
 * @sa smaxSetResilientExit()
 */
static void SendStoredPushRequests() {
  int i;

  pthread_mutex_lock(&tableLock);

  if(nPending <= 0) {
    pthread_mutex_unlock(&tableLock);
    return;
  }

  fprintf(stderr, "SMA-X> Resending accumulated unsent shares.\n");

  // Don't push failed writes back to the store...
  resilient = FALSE;

  for(i=SMAX_LOOKUP_SIZE; --i >= 0; ) while(table[i]) {
    PushRequest *req = table[i];

    int status = smaxWrite(req->group, req->field);
    if(status) {
      resilient = TRUE;
      pthread_mutex_unlock(&tableLock);
      fprintf(stderr, "SMA-X> WARNING! Not all accumulated shares were sent. Will try again...\n");
      return;
    }

    nPending--;

    table[i] = req->next;
    DestroyPushRequest(req);
  }

  if(exitAfterSync) {
    fprintf(stderr, "SMA-X> WARNING! Exiting because of prior connection error(s). All local updates were propagated to SMA-X.\n");
    exit(X_FAILURE);
  }

  nPending = 0;
  resilient = TRUE;

  pthread_mutex_unlock(&tableLock);
}

/**
 * IMPORTANT: Do not call with structure...
 *
 */
static void UpdatePushRequest(const char *group, const XField *field) {
  PushRequest *req;
  int idx = smaxGetHashLookupIndex(group, 0, field->name, 0);

  pthread_mutex_lock(&tableLock);

  for(req = table[idx]; req != NULL; req = req->next)
  if(!strcmp(req->group, group)) if(!strcmp(req->field->name, field->name)) break;

  if(req == NULL) {
    req = (PushRequest *) calloc(1, sizeof(PushRequest));
    x_check_alloc(req);

    req->group = xStringCopyOf(group);
    req->field = (XField *) calloc(1, sizeof(XField));
    x_check_alloc(req->field);

    if(table[idx] == NULL) table[idx] = req;
    else {
      req->next = table[idx];
      table[idx] = req;
    }

    nPending++;
  }

  memcpy(req->field, field, sizeof(XField));
  req->field->value = xStringCopyOf(field->value);

  pthread_mutex_unlock(&tableLock);
}

static void DestroyPushRequest(PushRequest *req) {
  if(req == NULL) return;
  if(req->group != NULL) free(req->group);
  if(req->field != NULL) free(req->field);
  free(req);
  return;
}

