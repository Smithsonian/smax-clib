/**
 * @file
 *
 * @date Created  on Jun 11, 2025
 * @author Attila Kovacs
 *
 * @since 1.1
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>

#include "smax-private.h"

/// \cond PRIVATE
/**
 * Structure for monitoring a response to a control variable 'command'.
 */
typedef struct {
  const char *table;    ///< Redis hash table name of SMA-X variable to monitor for update
  const char *key;      ///< Redis hash field to monitor for update
  int timeout;    ///< [s] Timeout
  int status;     ///< Return status
} ControlVar;

/// \endcond

static void *MonitorThread(void *arg) {
  ControlVar *control = (ControlVar *) arg;
  control->status = smaxWaitOnSubscribed(control->table, control->key, control->timeout);
  if(control->status != X_SUCCESS) return NULL;
  return (void *) smaxPullRaw(control->table, control->key, NULL, &control->status);
}

/**
 * Sets an SMA-X control variable, and returns the response to the monitored reply value
 *
 * @param table     SMA-X table name
 * @param key       The command keyword
 * @param value     Pointer to the value to set
 * @param type      The type of the value
 * @param count     Number of elements in value
 * @param replyKey  The keyword to monitor for responses.
 * @param timeout   [s] Maximum time to wait for a response before returning NULL.
 * @return          The raw value of the replyKey after it has changed, or NULL if there was
 *                  an error (errno will indicate the type of error).
 *
 * @sa smaxControlBoolean()
 * @sa smaxControlInt()
 * @sa smaxControlDouble()
 * @sa smaxControlString()
 */
char *smaxControl(const char *table, const char *key, const void *value, XType type, int count, const char *replyKey, int timeout) {
  static const char *fn = "smaxControl";

  ControlVar control = {};
  pthread_t tid;
  char *response = NULL;

  if(!replyKey || !replyKey[0]) {
    smaxError(fn, X_NAME_INVALID);
    return NULL;
  }

  // TODO aggregate and re-split table/keys in case of hierarchical key arguments, e.g. "commanded:value" and "actual:value"
  control.table = table;
  control.key = key;
  control.timeout = timeout;

  // To catch responses reliably, start monitoring
  if(smaxSubscribe(table, key) != X_SUCCESS) return x_trace_null(fn, NULL);

  // Launch monitoring thread with timeout
  if(pthread_create(&tid, NULL, MonitorThread, &control) < 0) {
    smaxUnsubscribe(table, key);
    return x_trace_null(fn, NULL);
  }

  // Now send the control command
  if(smaxShare(table, key, value, type, count) != X_SUCCESS) {
    pthread_cancel(tid);
    smaxUnsubscribe(table, key);
    return x_trace_null(fn, NULL);
  }

  // Wait for the response
  pthread_join(tid, (void **) &response);

  return response;
}

/**
 * Sets a boolean type SMA-X control variable, and returns the boolean response to the monitored reply,
 * or the specified default value in case of an error.
 *
 * @param table         SMA-X table name
 * @param key           The command keyword
 * @param value         Pointer to the value to set
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
boolean smaxControlBoolean(const char *table, const char *key, boolean value, const char *replyKey, boolean defaultReply, int timeout) {
  static const char *fn = "smaxControlBoolean";

  char *reply = smaxControl(table, key, &value, X_BOOLEAN, 1, replyKey, timeout);
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
char *smaxControlString(const char *table, const char *key, const char *value, const char *replyKey, int timeout) {
  static const char *fn = "smaxControlBoolean";

  char *reply = smaxControl(table, key, &value, X_STRING, 1, replyKey, timeout);
  if(!reply) return x_trace_null(fn, NULL);

  return reply;
}

/**
 * Sets an integer-type SMA-X control variable, and returns the integer response to the monitored reply,
 * or the specified default value in case of an error.
 *
 * @param table         SMA-X table name
 * @param key           The command keyword
 * @param value         Pointer to the value to set
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
int smaxControlInt(const char *table, const char *key, int value, const char *replyKey, int defaultReply, int timeout) {
  static const char *fn = "smaxControlInt";

  char *reply = smaxControl(table, key, &value, X_INT, 1, replyKey, timeout);
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
 * @param replyKey      The keyword to monitor for responses.
 * @param timeout       [s] Maximum time to wait for a response before returning NAN.
 * @return              The double-precision value of the monitored keyword after it updated, or
 *                      NAN if there was an error (errno will indicate the type of error).
 *
 * @sa smaxControlBoolean()
 * @sa smaxControlInt()
 * @sa smaxControlDouble()
 */
double smaxControlDouble(const char *table, const char *key, double value, const char *replyKey, int timeout) {
  static const char *fn = "smaxControlDouble";

  char *reply = smaxControl(table, key, &value, X_DOUBLE, 1, replyKey, timeout);
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
