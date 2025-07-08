/**
 * @file
 *
 * @date Created  on Jun 11, 2025
 * @author Attila Kovacs
 *
 *  Send and process control 'commands' via SMA-X by setting or monitored designated control variables
 *  Clients will set these and wait for a response in another variable, while servers will monitor the
 *  control variables and execute designated control functions when these variables are updated.
 *  The controlled application is expected to act on the request passed in the control variable, and post
 *  the result (such as the actual value) after the request was processed in the designated 'reply'
 *  variable.
 *
 *  For example, we set `system:subsystem:control_voltage` to 0.123456 to request that the given subsystem
 *  outputs the desired voltage. The program that controls the subsystem, monitors the `control_voltage`
 *  for requests ('commands'), and then sets the output voltage to the nearest possible value, then
 *  posts the actual value that was applied in `system:subsystem:voltage`. Suppose, it's DAC has a
 *  resolution of 0.01 V. In that case, it might 'reply' with `voltage` set to 0.12.
 *
 *  It is important that for every request received, the control program should post exactly one reply in
 *  the designated response variable.
 *
 *  The control calls of this module will simply wait for an update of the reply variable, and return the
 *  value set by the control program after the request is sent.
 *
 * @since 1.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>

#include "smax-private.h"

/// \cond PRIVATE
#define SMAX_CONTROL_TABLE_SIZE   256   ///< Hash table size for control functions.

/**
 * Structure for monitoring a response to a control variable 'command'.
 */
typedef struct {
  const char *table;    ///< Redis hash table name of SMA-X variable to monitor for update
  const char *key;      ///< Redis hash field to monitor for update
  int timeout;          ///< [s] Timeout
  int status;           ///< Return status
  sem_t sem;            ///< Sempahore for when response is ready.
} ControlVar;

/**
 * Call arguments for threaded control calls.
 */
typedef struct {
  char *id;                   ///< Aggregate ID of control variable
  SMAXControlFunction func;   ///< Control function to call for the variable.
  void *parg;                 ///< Additional pointer argument to pass to control function
} ControlSet;


static XLookupTable *controls;      ///< Lookup table of currently configured control functions.
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/// \endcond

static void *MonitorThread(void *arg) {
  ControlVar *control = (ControlVar *) arg;
  control->status = smaxWaitOnSubscribed(control->table, control->key, control->timeout, &control->sem);
  if(control->status != X_SUCCESS) return NULL;
  return (void *) smaxPullRaw(control->table, control->key, NULL, &control->status);
}

/**
 * Sets an SMA-X control variable, and returns the response to the monitored reply value.
 *
 * @param table       SMA-X table name
 * @param key         The command keyword
 * @param value       Pointer to the value to set
 * @param type        The type of the value
 * @param count       Number of elements in value
 * @param replyTable  SMA-X table in which the reply is expected. It may also be NULL if it's the
 *                    same as the table in which the control variable was set.
 * @param replyKey    The keyword to monitor for responses.
 * @param timeout     [s] Maximum time to wait for a response before returning NULL.
 * @return            The raw value of the replyKey after it has changed, or NULL if there was
 *                    an error (errno will indicate the type of error).
 *
 * @sa smaxControlBoolean()
 * @sa smaxControlInt()
 * @sa smaxControlDouble()
 * @sa smaxControlString()
 */
char *smaxControl(const char *table, const char *key, const void *value, XType type, int count, const char *replyTable, const char *replyKey, int timeout) {
  static const char *fn = "smaxControl";

  ControlVar reply = {};
  pthread_t tid;
  char *response = NULL;
  int status;

  if(!replyKey || !replyKey[0]) {
    smaxError(fn, X_NAME_INVALID);
    return NULL;
  }

  reply.table = replyTable ? replyTable : table;
  reply.key = replyKey;
  reply.timeout = timeout;
  sem_init(&reply.sem, FALSE, 0);

  // To catch responses reliably, start monitoring
  if(smaxSubscribe(reply.table, reply.key) != X_SUCCESS) return x_trace_null(fn, NULL);

  // Launch monitoring thread with timeout
  if(pthread_create(&tid, NULL, MonitorThread, &reply) < 0) {
    smaxUnsubscribe(reply.table, reply.key);
    sem_destroy(&reply.sem);
    x_error(0, errno, fn, "could not create monitor thread");
    return NULL;
  }

  // Proceed only when the monitor is in waiting status...
  if(sem_wait(&reply.sem) < 0) {;
    smaxUnsubscribe(reply.table, reply.key);
    sem_destroy(&reply.sem);
    x_error(0, errno, fn, "sem_wait() gating error");
    return NULL;
  }

  // By getting exclusive access to the notifications after the semaphore
  // we ensure that the wait is already active, or else failed to activate.
  smaxLockNotify();
  status = smaxShare(table, key, value, type, count);
  smaxUnlockNotify();

  // Now send the control command
  if(status != X_SUCCESS) {
    pthread_cancel(tid);
    smaxUnsubscribe(reply.table, reply.key);
    sem_destroy(&reply.sem);
    return x_trace_null(fn, NULL);
  }

  // Wait for the response
  pthread_join(tid, (void **) &response);

  smaxUnsubscribe(reply.table, reply.key);
  sem_destroy(&reply.sem);

  if(reply.status) x_warn(fn, "Got no response: %s", strerror(errno));

  return response;
}

/**
 * Sets a boolean type SMA-X control variable, and returns the boolean response to the monitored reply,
 * or the specified default value in case of an error.
 *
 * @param table         SMA-X table name
 * @param key           The command keyword
 * @param value         Pointer to the value to set
 * @param replyTable    SMA-X table in which the reply is expected. It may also be NULL if it's the
 *                      same as the table in which the control variable was set.
 * @param replyKey      The keyword to monitor for responses.
 * @param defaultReply  The value to return in case of an error
 * @param timeout       [s] Maximum time to wait for a response before returning NULL.
 * @return              The boolean value of the monitored keyword after it updated, or
 *                      the specified default value if there was an error (errno will
 *                      indicate the type of error).
 *
 * @sa smaxControlInt()
 * @sa smaxControlDouble()
 * @sa smaxControlString()
 */
boolean smaxControlBoolean(const char *table, const char *key, boolean value, const char *replyTable, const char *replyKey, boolean defaultReply, int timeout) {
  static const char *fn = "smaxControlBoolean";

  char *reply = smaxControl(table, key, &value, X_BOOLEAN, 1, replyTable, replyKey, timeout);
  if(!reply) return x_trace(fn, NULL, defaultReply);

  defaultReply = xParseBoolean(reply, NULL);
  free(reply);

  return errno ? defaultReply : x_trace(fn, NULL, defaultReply);
}

/**
 * Sets a atring type SMA-X control variable, and returns the string response to the monitored reply,
 * or NULL in case of an error.
 *
 * @param table         SMA-X table name
 * @param key           The command keyword
 * @param value         Pointer to the value to set
 * @param replyTable    SMA-X table in which the reply is expected. It may also be NULL if it's the
 *                      same as the table in which the control variable was set.
 * @param replyKey      The keyword to monitor for responses.
 * @param timeout       [s] Maximum time to wait for a response before returning NULL.
 * @return              The raw string value of the monitored keyword after it updated, or
 *                      NULL if there was an error (errno will indicate the type of error).
 *
 * @sa smaxControlBoolean()
 * @sa smaxControlInt()
 * @sa smaxControlDouble()
 *
 * @since 1.1
 */
char *smaxControlString(const char *table, const char *key, const char *value, const char *replyTable, const char *replyKey, int timeout) {
  static const char *fn = "smaxControlBoolean";

  char *reply = smaxControl(table, key, &value, X_STRING, 1, replyTable, replyKey, timeout);
  if(!reply) return x_trace_null(fn, NULL);

  return reply;
}

/**
 * Sets an integer-type SMA-X control variable, and returns the integer response to the monitored
 * reply, or the specified default value in case of an error.
 *
 * @param table         SMA-X table name
 * @param key           The command keyword
 * @param value         Pointer to the value to set
 * @param replyTable    SMA-X table in which the reply is expected. It may also be NULL if it's the
 *                      same as the table in which the control variable was set.
 * @param replyKey      The keyword to monitor for responses.
 * @param defaultReply  The value to return in case of an error
 * @param timeout       [s] Maximum time to wait for a response before returning NULL.
 * @return              The integer value of the monitored keyword after it updated, or
 *                      the specified default value if there was an error (errno will
 *                      indicate the type of error).
 *
 * @sa smaxControlBoolean()
 * @sa smaxControlInt()
 * @sa smaxControlString()
 */
int smaxControlInt(const char *table, const char *key, int value, const char *replyTable, const char *replyKey, int defaultReply, int timeout) {
  static const char *fn = "smaxControlInt";

  char *reply = smaxControl(table, key, &value, X_INT, 1, replyTable, replyKey, timeout);
  if(!reply) return x_trace(fn, NULL, defaultReply);

  if(sscanf(reply, "%d", &defaultReply) < 1) {
    errno = EBADMSG;
    x_trace(fn, NULL, defaultReply);
  }
  free(reply);

  return defaultReply;
}

/**
 * Sets a atring type SMA-X control variable, and returns the string response to the monitored reply,
 * or NULL in case of an error.
 *
 * @param table         SMA-X table name
 * @param key           The command keyword
 * @param value         Pointer to the value to set
 * @param replyTable    SMA-X table in which the reply is expected. It may also be NULL if it's the
 *                      same as the table in which the control variable was set.
 * @param replyKey      The keyword to monitor for responses.
 * @param timeout       [s] Maximum time to wait for a response before returning NAN.
 * @return              The double-precision value of the monitored keyword after it updated, or
 *                      NAN if there was an error (errno will indicate the type of error).
 *
 * @sa smaxControlBoolean()
 * @sa smaxControlInt()
 * @sa smaxControlDouble()
 */
double smaxControlDouble(const char *table, const char *key, double value, const char *replyTable, const char *replyKey, int timeout) {
  static const char *fn = "smaxControlDouble";

  char *reply = smaxControl(table, key, &value, X_DOUBLE, 1, replyTable, replyKey, timeout);
  if(!reply) {
    x_trace_null(fn, NULL);
    return NAN;
  }

  if(sscanf(reply, "%lf", &value) < 1) {
    errno = EBADMSG;
    x_trace_null(fn, NULL);
    return NAN;
  }
  free(reply);

  return value;
}

// -----------------------------------------------------------------------------------------------
// For server side processing of control calls:

static void *ControlThread(void *arg) {
  ControlSet *control = (ControlSet *) arg;
  char *key = NULL;

  // We won't join this thread...
  pthread_detach(pthread_self());

  // Call the control function
  xSplitID(control->id, &key);
  control->func(control->id, key, control->parg);

  // Clean up out copy of the control set
  free(control->id);
  free(control);

  return NULL;
}

static void ProcessControls(const char *pattern, const char *channel, const char *msg, long length) {
  const XField *f;
  ControlSet *control = NULL;
  const char *id;

  (void) pattern; // unused
  (void) msg; // unused
  (void) length; // unused

  if(strncmp(channel, SMAX_UPDATES, sizeof(SMAX_UPDATES) - 1) != 0) return;

  id = &channel[sizeof(SMAX_UPDATES) - 1];

  pthread_mutex_lock(&mutex);

  f = xLookupField(controls, id);
  if(f) {
    control = (ControlSet *) calloc(1, sizeof(ControlSet));
    x_check_alloc(control);
    memcpy(control, f->value, sizeof(ControlSet));
  }

  pthread_mutex_unlock(&mutex);

  if(f) {
    pthread_t tid;

    control->id = xStringCopyOf(id); // We use a persistent and independent copy for the async call.

    // Call the control function from a dedicated thread, so we may return here without delay.
    if(pthread_create(&tid, NULL, ControlThread, control) < 0) {
      perror("ERROR! Failed to call control function");
      exit(errno);
    }
  }
}

/**
 * Configures an SMA-X control function. The designated function will be called every time the
 * monitored control variable is updated in the database, so it may act on the update as
 * appropriate. If another control functions was already defined for the variable, it will be
 * replaced with the new function. The same control function may be used with multiple control
 * variables, given that the triggering control variable is passed to it as arguments.
 *
 * When the control variables are updated, the associated control functions will be called
 * asynchronously, such that previous control calls may be executing still while new ones are
 * called. The design allows for control functions to take their sweet time executing, without
 * holding up other time-sensitive SMA-X processing. If simultaneous execution of control
 * functions is undesired, the control function(s) should implement mutexing as necessary to
 * avoid conflicts / clobbering.
 *
 * As a result of the asynchronous execution, there is also no guarantee of maintaining call order
 * with respect to the order in which the control variables are updated. If two updates arrive in
 * quick succession, it is possible that the asynchronous thread of the second one will make its
 * call to its control function before the first one gets a chance. Thus, if order is important,
 * you might want to process updates with a custom lower-level `RedisxSubscriberCall` wrapper
 * instead (see `smaxAddSubscriber()`).
 *
 * @param table   The hash table in which the control variable resides.
 * @param key     the control variable to monitor. It may not contain a sepatator.
 * @param func    The new function to call if the monitored control variable receives an update,
 *                or NULL to clear a previously configured function for the given variable.
 * @param parg    Optional pointer argument to pass along to the command procesing function, or
 *                NULL if the control function does not need extra data.
 * @return        X_SUCCESS (0)
 */
int smaxSetControlFunction(const char *table, const char *key, SMAXControlFunction func, void *parg) {
  static const char *fn = "smaxSetControlFunction";

  char *id;
  XField *prior = NULL;

  if(!table) return x_error(X_GROUP_INVALID, EINVAL, fn, "Table name is NULL");
  if(!table[0]) return x_error(X_GROUP_INVALID, EINVAL, fn, "Table name is empty");
  if(!key) return x_error(X_NAME_INVALID, EINVAL, fn, "Control variable name is NULL");
  if(!key[0]) return x_error(X_NAME_INVALID, EINVAL, fn, "Control variable name is empty");

  id = (char *) malloc(sizeof(SMAX_UPDATES) + strlen(table) + strlen(key) + X_SEP_LENGTH);
  x_check_alloc(id);

  sprintf(id, SMAX_UPDATES "%s" X_SEP "%s", table, key);

  pthread_mutex_lock(&mutex);

  // Remove and destroy any prior entry for the table, and unsubscribe updates for the control
  // variable if no new function replaces it.
  if(controls) {
    prior = xLookupRemove(controls, id);
    if(prior) {
      ControlSet *control = (ControlSet *) prior->value;

      free(control->id);
      free(control);

      prior->value = NULL;
      xDestroyField(prior);

      if(!func) smaxUnsubscribe(table, key);
    }
  }

  if(func) {
    XField *f;
    ControlSet *control;

    // Create controls lookup table as necessary, and set up subscriber to process updates
    if(!controls) {
      controls = xAllocLookup(SMAX_CONTROL_TABLE_SIZE);
      x_check_alloc(controls);

      // Start processing control calls...
      smaxAddSubscriber("", ProcessControls);
    }

    control = (ControlSet *) calloc(1, sizeof(ControlSet));
    x_check_alloc(control);

    control->id = xGetAggregateID(table, key);
    control->func = func;
    control->parg = parg;

    f = xCreateField(key, X_UNKNOWN, 0, NULL, NULL);
    x_check_alloc(f);

    f->value = control;

    // Add the new control and subscribe for updates as necessary
    xLookupPut(controls, table, f, NULL);

    if(!prior) smaxSubscribe(table, key);
  }

  pthread_mutex_unlock(&mutex);

  if(id) free(id);

  return X_SUCCESS;
}

