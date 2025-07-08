/**
 * @file
 *
 * @date Created  on Jun 11, 2025
 * @author Attila Kovacs
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "smax-private.h"

/// \cond PRIVATE
#define SMAX_SUBSCRIPTION_LOOKUP_SIZE   1024      ///< Hash lookup slots for tracking subscriptions
/// \endcond


// A lock for ensuring exlusive access for the monitor list...
// and the variables that it controls, e.g. via lockNotify()
static pthread_mutex_t notifyLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notifyBlock = PTHREAD_COND_INITIALIZER;

// The most recent key update notification info, to be used by smaxWait() exclusively...
static char *notifyID;
static int notifySize;

static XLookupTable *lookup;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


/// \cond PRIVATE

void ProcessUpdateNotificationAsync(const char *pattern, const char *channel, const char *msg, long length) {
  (void) pattern;

  xdprintf("{message} %s %s\n", channel, msg);

  if(strncmp(channel, SMAX_UPDATES, SMAX_UPDATES_LENGTH)) return; // Wrong message prefix

  smaxLockNotify();

  // Resize the notify values as needed.
  if(length > 0) if(notifySize < length+1) {
    char *oldid = notifyID;
    notifyID = realloc(notifyID, length+1);
    if(!notifyID) {
      perror("WARNING! realloc notifyID");
      free(oldid);
    }
    notifySize = notifyID ? length+1 : 0;
  }

  if(notifySize > 0) notifyID[0] = '\0';  // Reset the notify values to empty strings (if not NULL)
  strcpy(notifyID, channel + SMAX_UPDATES_LENGTH);

  // Send notification to all blocking threads...
  pthread_cond_broadcast(&notifyBlock);

  smaxUnlockNotify();
}

void smaxInitNotify() {
  // Initial sotrage for update notifications
  notifySize = 80;
  notifyID = (char *) calloc(1, notifySize);
  x_check_alloc(notifyID);

  smaxAddSubscriber(NULL, ProcessUpdateNotificationAsync);
}

/// \endcond

static void DiscardLookup() {
  pthread_mutex_lock(&mutex);
  xDestroyLookupAndData(lookup);
  lookup = NULL;
  pthread_mutex_unlock(&mutex);
}

/**
 * Subscribes to a specific key(s) in specific group(s). Both the group and key names may contain Redis
 * subscription patterns, e.g. '*' or '?', or bound characters in square-brackets, e.g. '[ab]'. The
 * subscription only enables receiving update notifications from Redis for the specified variable or
 * variables. After subscribing, you can either wait on the subscribed variables to change, or add
 * callback functions to process subscribed variables changes, via smaxAddSubscriber().
 *
 * \param table         Variable group pattern, i.e. hash-table names. (NULL is the same as '*').
 * \param key           Variable name pattern. (if NULL then subscribes only to the table stem).
 *
 * \return      X_SUCCESS       if successfully subscribed to the Redis distribution channel.
 *              X_NO_SERVICE    if there is no active connection to the Redis server.
 *              X_NULL          if the channel argument is NULL
 *              X_NO_INIT       if the SMA-X library was not initialized.
 *
 * \sa smaxUnsubscribe()
 * @sa smaxWaitOnSubscribed()
 * @sa smaxWaitOnSubscribedGroup()
 * @sa smaxWaitOnSubscribedVar()
 * @sa smaxWaitOnAnySubscribed()
 * @sa smaxAddSubscriber()
 */
int smaxSubscribe(const char *table, const char *key) {
  static const char *fn = "smaxSubscribe";
  Redis *r = smaxGetRedis();
  XField *f;
  char *p;
  int status = X_SUCCESS;

  if(!r) return smaxError(fn, X_NO_INIT);

  p = smaxGetUpdateChannelPattern(table, key);

  pthread_mutex_lock(&mutex);

  // manage subscriber lists with call counter...
  //  - Redis subscribe only if new
  //  - Redis unsubscribe only when last user unsubscribes
  if(!lookup) {
    // Create lookup table to track the number of active subscribers to each pattern
    lookup = xAllocLookup(SMAX_SUBSCRIPTION_LOOKUP_SIZE);
    x_check_alloc(lookup);

    // Disconnect from the Redis server will discard all active subscriptions.
    smaxAddDisconnectHook(DiscardLookup);
  }

  f = xLookupField(lookup, p);
  if(f) (*(int *) f->value)++; // Increment the number of subscribers...
  else {
    // We are the first subscriber to this pattern so subscribe on Redis....
    status = redisxSubscribe(r, p);
    if(status == X_SUCCESS) xLookupPut(lookup, p, xCreateIntField(p, 1), NULL);
  }

  pthread_mutex_unlock(&mutex);

  free(p);
  prop_error(fn, status);
  return X_SUCCESS;
}

/**
 * Unsubscribes from a specific key(s) in specific group(s). Both the group and key names may contain Redis
 * subscription patterns, e.g. '*' or '?', or bound characters in square-brackets, e.g. '[ab]'. Unsubscribing
 * will only stops the delivery of update notifications for the affected varuiables, but does not deactivate
 * the associated callbacks for these added via smaxAddSubscriber(). Therefore you should also call
 * smaxRemovesubscribers() as appropriate to deactivate actions that can no longer get triggered by
 * updates.
 *
 * \param table         Variable group pattern, i.e. structure or hash-table name(s) (NULL is the same as '*').
 * \param key           Variable name pattern. (if NULL then unsubscribes only from the table stem).
 *
 * \return      X_SUCCESS       if successfully unsubscribed to the Redis distribution channel.
 *              X_NO_SERVICE    if there is no active connection to the Redis server.
 *              X_NULL          if the channel argument is NULL
 *              X_NO_INIT       if the SMA-X library was not initialized.
 *
 * \sa smaxSubscribe()
 * @sa smaxRemoveSubscribers()
 */
int smaxUnsubscribe(const char *table, const char *key) {
  static const char *fn = "smaxUnsubscribe";
  Redis *r = smaxGetRedis();

  char *p;
  int status = X_SUCCESS;

  if(!r) return smaxError(fn, X_NO_INIT);
  p = smaxGetUpdateChannelPattern(table, key);

  pthread_mutex_lock(&mutex);

  if(lookup) {
    XField *f = xLookupField(lookup, p);
    if(f != NULL) {
      // Descrement the number of subscribers to the pattern,
      // and unsubscribe from Redis of no subsciber reamins for
      // the pattern.
      int *count = (int *) f->value;
      if(--(*count) <= 0) {
        status = redisxUnsubscribe(r, p);
        if(status == X_SUCCESS) xDestroyField(xLookupRemove(lookup, p));
      }
    }
  }

  pthread_mutex_unlock(&mutex);
  free(p);

  prop_error(fn, status);
  return X_SUCCESS;
}

/**
 * Add a subcriber (callback) function to process incoming PUB/SUB messages for a given SMA-X table (or id). The
 * function should itself check that the channel receiving notification is indeed what it expectes before
 * acting on it, as the callback routine will be invoked for any update inside the specified table, unless the
 * table argument refers to a specific aggregate ID of a single variable. This call only registers the callback
 * routine for SMA-X update notifications for variables that begin with the specified stem. You will still have
 * to subscrive to any relevant variables with smaxSubscribe() to enable delivering update notifications for the
 * variables of your choice.
 *
 * @param idStem    Table name or ID stem for which the supplied callback function will be invoked as long
 *                  as the beginning of the PUB/SUB update channel matches the given stem.
 *                  Alternatively, it can be a fully qualified SMA-X ID (of the form table:key) f a single
 *                  variable.
 * @param f         The function to call when there is an incoming PUB/SUB update to a channel starting with
 *                  stem.
 *
 * @return          X_SUCCESS if successful, or else an approriate error code by redisxAddSubscriber()
 *
 * @sa smaxSubscribe()
 */
int smaxAddSubscriber(const char *idStem, RedisSubscriberCall f) {
  static const char *fn = "smaxAddSubscriber";
  Redis *r = smaxGetRedis();
  char *stem;
  int status;

  if(!r) return smaxError(fn, X_NO_INIT);

  stem = xGetAggregateID(SMAX_UPDATES_ROOT, idStem);
  status = redisxAddSubscriber(r, stem, f);
  free(stem);
  prop_error(fn, status);
  return X_SUCCESS;
}

/**
 * Remove all instances of a subscriber callback function from the current list of functions processing PUB/SUB messages.
 * This call only deactivates the callback routine, but does not stop the delivery of update notifications from
 * the Redis server. You should therefore also call smaxUnsubscribe() as appropriate to stop notifications
 * for variables that no longer have associated callbacks.
 *
 * @param f     Function to remove
 * @return      X_SUCCESS (0) if successful, or else an error (&lt;0) returned by redisxRemoveSubscriber().
 *
 * @sa smaxUnsubscribe()
 */
int smaxRemoveSubscribers(RedisSubscriberCall f) {
  Redis *r = smaxGetRedis();
  if(!r) return smaxError("smaxRemoveSubscribers", X_NO_INIT);
  prop_error("smaxRemoveSubscribers", redisxRemoveSubscribers(r, f));
  return X_SUCCESS;
}

/**
 * \cond PROTECTED
 *
 * Gets a standard SMA-X designator for a Redis PUB/SUB channel, composed of the group and key names/patterns.
 *
 * \param table         Variable group pattern, i.e. structure or hash-table name(s). (NULL is the same as '*').
 * \param key           Variable name pattern. If NULL, then subscribe only to the table stem...
 *
 * \return              The corresponding update notification channel, i.e., 'smax:' + table + ':' + key.
 *
 * @sa smaxSubscribe()
 *
 */
char *smaxGetUpdateChannelPattern(const char *table, const char *key) {
  char *p;
  if(table == NULL) table = "*";
  if(key == NULL) {
    p = (char *) malloc(sizeof(SMAX_UPDATES) + strlen(table));
    x_check_alloc(p);
    sprintf(p, SMAX_UPDATES "%s", table);
  }
  else {
    p = (char *) malloc(sizeof(SMAX_UPDATES) + strlen(table) + X_SEP_LENGTH + strlen(key));
    x_check_alloc(p);
    sprintf(p, SMAX_UPDATES "%s" X_SEP "%s", table, key);
  }
  return p;
}
/// \endcond

/**
 * Waits until any variable was pushed on any host, returning both the host and variable name for the updated value.
 * The variable must be already subscribed to with smaxSubscribe(), or else the wait will not receive update
 * notifications.
 *
 * \param[out] changedTable     Pointer to the variable that points to the string buffer for the returned table name or NULL.
 *                              The lease of the buffer is for the call only.
 * \param[out] changedKey       Pointer to the variable that points to the string buffer for the returned variable name or NULL.
 *                              The lease of the buffer is for the call only.
 * \param[in] timeout           (s) Timeout value. 0 or negative values result in an indefinite wait.
 * \param[in,out] gating        Optional semaphore to post after thuis wait call gains exclusive access to the notification
 *                              mutex. Another thread may wait on that semaphore before it too tries to get exclusive access
 *                              to SMA-X notifications via some other library call, to ensure that the wait is entered (or
 *                              else fails) in a timely manner, without unwittingly being blocked by the other thread.
 *                              Typically, you can set it to NULL if such cross-thread gating is not required.
 *
 * \return      X_SUCCESS (0)       if a variable was pushed on a host.
 *              X_NO_INIT           if the SMA-X sharing was not initialized via smaxConnect().
 *              X_NO_SERVICE        if the connection was broken
 *              X_GROUP_INVALID     if the buffer for the returned table name is NULL.
 *              X_NAME_INVALID      if the buffer for the returned variable name is NULL.
 *              X_INTERRUPTED       if smaxReleaseWaits() was called.
 *              X_INCOMPLETE        if the wait timed out.
 *
 * @sa smaxSubscribe()
 * \sa smaxWaitOnSubscribed()
 * \sa smaxWaitOnSubscribedGroup()
 * \sa smaxReleaseWaits()
 *
 */
int smaxWaitOnAnySubscribed(char **changedTable, char **changedKey, int timeout, sem_t *gating) {
  static const char *fn = "smaxWaitOnAnySubscribed";
  int status = X_SUCCESS;
  struct timespec endTime = {};


  if(changedTable == NULL) return x_error(X_GROUP_INVALID, EINVAL, fn, "'changedTable' parameter is NULL");
  if(changedKey == NULL) return x_error(X_NAME_INVALID, EINVAL, fn, "'changedKey' parameter is NULL");
  if(!smaxGetRedis()) return smaxError(fn, X_NO_INIT);
  if(!smaxIsConnected()) return x_error(X_NO_SERVICE, ENOTCONN, fn, "not connected to SMA-X server.");

  xvprintf("SMA-X> waiting for notification...\n");

  *changedTable = NULL;
  *changedKey = NULL;


  smaxLockNotify();
  if(gating) sem_post(gating);

  if(timeout > 0) {
    clock_gettime(CLOCK_REALTIME, &endTime);
    endTime.tv_sec += timeout;
  }

  // Waits for a notification...
  while(*changedTable == NULL) {
    const char *sep;

    status = timeout > 0 ? pthread_cond_timedwait(&notifyBlock, &notifyLock, &endTime) : pthread_cond_wait(&notifyBlock, &notifyLock);
    if(status) {
      // If the wait returns with an error, the mutex is uncloked.
      if(status == ETIMEDOUT) {
        smaxUnlockNotify();
        return x_error(X_INCOMPLETE, status, fn, "wait timed out");
      }
      return x_error(X_INCOMPLETE, status, fn, "pthread_cond_wait() error: %s", strerror(status));
    }

    if(!smaxIsConnected()) {
      smaxUnlockNotify();
      return x_error(X_NO_SERVICE, EPIPE, fn, "wait aborted due to broken connection");
    }

    // Check for premature release...
    if(!strcmp(notifyID, RELEASEID)) {
      smaxUnlockNotify();
      return x_error(X_INTERRUPTED, EINTR, fn, "wait interrupted");
    }

    if(notifyID[0] == '\0') {
      xdprintf("WARNING! SMA-X : published message contained NULL. Ignored.\n");
      continue;
    }

    xvprintf("SMA-X> %s: got %s.\n", fn, notifyID);

    // Find the last separator
    sep = xLastSeparator(notifyID);

    if(sep != NULL) {
      *changedKey = xStringCopyOf(sep + X_SEP_LENGTH);
      *changedTable = (char *) malloc(sep - notifyID);
      memcpy(*changedTable, notifyID, sep - notifyID);
      (*changedTable)[sep - notifyID] = '\0';
    }
    else {
      *changedKey = NULL;
      *changedTable = xStringCopyOf(notifyID);
    }
  }

  smaxUnlockNotify();

  prop_error(fn, status);

  return X_SUCCESS;
}

/**
 * Unblocks all smax_wait*() calls, which will return X_REL_PREMATURE, as a result.
 *
 * \return  X_SUCCESS (0)
 *
 * \sa smaxWaitOnAnySubscribed()
 *
 */
int smaxReleaseWaits() {
  xvprintf("SMA-X> release read.\n");

  smaxLockNotify();

  if(notifySize < (int) sizeof(RELEASEID)) {
    char *oldid = notifyID;
    notifyID = realloc(notifyID, sizeof(RELEASEID));
    if(!notifyID) {
      perror("ERROR! alloc error");
      free(oldid);
      exit(errno);
    }
    notifySize = sizeof(RELEASEID);
  }

  if(notifyID) {
    strcpy(notifyID, RELEASEID);
    pthread_cond_broadcast(&notifyBlock);
  }

  smaxUnlockNotify();

  return X_SUCCESS;
}


/// \cond PROTECTED

/**
 * Get exclusive access for accessing or updating notifications.
 *
 * \return      The result of pthread_mutex_lock().
 *
 * \sa smaxUnlockNotify()
 */
int smaxLockNotify() {
  int status = pthread_mutex_lock(&notifyLock);
  if(status) fprintf(stderr, "WARNING! SMA-X : smaxLockNotify() failed with code: %d.\n", status);
  return status;
}

/**
 * Relinquish exclusive access notifications.
 *
 * \return      The result of pthread_mutex_unlock().
 *
 * \sa smaxLockNotify()
 */
int smaxUnlockNotify() {
  int status = pthread_mutex_unlock(&notifyLock);
  if(status) fprintf(stderr, "WARNING! SMA-X : smaxUnockNotify() failed with code: %d.\n", status);
  return status;
}

/**
 *
 * Process responses to pipelined HSET calls (integer RESP).
 *
 * \param reply     The RESP reply received from Redis on its pipeline channel.
 *
 */
// cppcheck-suppress constParameterCallback
// cppcheck-suppress constParameterPointer
void smaxProcessPipedWritesAsync(RESP *reply) {
  if(reply->type == RESP_INT) {
    xvprintf("pipe RESP: %d\n", reply->n);
    //if(reply->n > 0) xvprintf("SMA-X : new variable was added...\n");
  }
  else if(reply->type == RESP_ERROR) {
    if(strstr("NOSCRIPT", (char *) reply->value)) smaxScriptError("smaxProcessPipedWritesAsync()", X_NULL);
    else fprintf(stderr, "WARNING! SMA-X: error reply: %s\n", (char *) reply->value);
  }
  else {
    fprintf(stderr, "WARNING! SMA-X: unexpected pipeline response type: '%c'.\n", reply->type);
    return;
  }
}
/// \endcond


