/**
 * \file
 *
 * \date Jan 26, 2018
 * \author Attila Kovacs
 *
 * \brief
 *      SMA-X is a software implementation for SMA shared data, and is the base layer for the software
 *      reflective memory (RM) emulation, and DSM replacement.
 *      It works by communicating TCP/IP messages to a central REDIS server.
 *
 *      There is also extra functionality, for configuring, performance tweaking, verbosity control,
 *      and some convenience methods (e.g. data serialization/deserialization).
 *
 */

#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>

#include "redisx.h"
#include "smax.h"
#include "smax-private.h"

#if __Lynx__
#include "procname.h"
#endif



/// \cond PRIVATE
#define HMSET_NAME_OFFSET       0
#define HMSET_VALUE_OFFSET      1
#define HMSET_TYPE_OFFSET       2
#define HMSET_DIMS_OFFSET       3
#define HMSET_COMPONENTS        4

#define HMGET_VALUE_OFFSET      0
#define HMGET_TYPE_OFFSET       1
#define HMGET_DIMS_OFFSET       2
#define HMGET_TIMESTAMP_OFFSET  3
#define HMGET_ORIGIN_OFFSET     4
#define HMGET_SERIAL_OFFSET     5
#define HMGET_COMPONENTS        6

#define SHA1_LENGTH             41

#define STATE_UNKNOWN           (-1)
/// \endcond


// Script hash values for EVALSHA
char *HSET_WITH_META;       ///< SHA1 key for calling HSetWithMeta LUA script
char *HGET_WITH_META;       ///< SHA1 key for calling HGetWithMeta LUA script
char *HMSET_WITH_META;      ///< SHA1 key for calling HMSetWithMeta LUA script
char *GET_STRUCT;           ///< SHA1 key for calling HGetStruct LUA script



// Local prototypes ------------------->
static void ProcessUpdateNotificationAsync(const char *pattern, const char *channel, const char *msg, int length);

static int ProcessStructRead(RESP **component, PullRequest *req);
static int ParseStructData(XStructure *s, RESP *names, RESP *data, XMeta *meta);

static int SendStructDataAsync(RedisClient *cl, const char *id, const XStructure *s, boolean isTop);

static void InitScriptsAsync();

static boolean usePipeline = TRUE;
static int tcpBufSize = REDIS_TCP_BUF;

// A lock for ensuring exlusive access for the monitor list...
// and the variables that it controls, e.g. via lockNotify()
static pthread_mutex_t notifyLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notifyBlock = PTHREAD_COND_INITIALIZER;

// The most recent key update notification info, to be used by smaxWait() exclusively...
static char *notifyID;
static int notifySize;

static Redis *redis;

static char *programID;

/**
 * Enable or disable verbose reporting of all SMA-X operations (and possibly some details of them).
 * Reporting is done on the standard output (stdout). It may be useful when debugging programs
 * that use the SMA-X interface. Verbose reporting is DISABLED by default.
 *
 * \param value         TRUE to enable verbose reporting, or FALSE to disable.
 *
 * @sa smaxIsVerbose()
 *
 */
void smaxSetVerbose(boolean value) {
  redisxSetVerbose(value);
}


/**
 * Checks id verbose reporting is enabled.
 *
 * \return          TRUE if verbose reporting is enabled, otherwise FALSE.
 *
 * @sa smaxSetVerbose()
 */
boolean smaxIsVerbose() {
  return redisxIsVerbose();
}


/**
 * Enable or disable pipelined write operations. When pipelining, shares calls will return
 * as soon as the request is sent to the REDIS server, without waiting for a response.
 * Instead, responses are consumed asynchronously by a dedicated thread, which will report
 * errors to stderr. Pipelined writes can have a significant performance advantage over
 * handshaking at the cost of one extra socket connection to REDIS (dedicated to pipelining)
 * and the extra thread consuming responses.
 *
 * The default state of pipelined writes might vary by platform (e.g. enabled on Linux,
 * disabled on LynxOS).
 *
 * __IMPORTANT__: calls to smaxSetPipelined() must precede the call to smaxConnect().
 *
 * \param isEnabled     TRUE to enable pipelined writes, FALSE to disable.
 *
 * \return              X_SUCCESS   if the setting was successful
 *                      X_FAILURE   if the state could not be changed
 *                                  (e.g. because smaxOpen() was called previously).
 *
 * @sa smaxIsPipelined()
 * @sa smaxSetPipelineConsumer()
 */
int smaxSetPipelined(boolean isEnabled) {
  if(usePipeline == isEnabled) return X_SUCCESS;
  if(smaxIsConnected()) {
    fprintf(stderr, "WARNING! SMA-X : Cannot change pipeline state after smaxOpen().\n");
    return X_FAILURE;
  }
  usePipeline = isEnabled;
  return X_SUCCESS;
}


/**
 * Check if SMA-X is configured with pipeline mode enabled.
 *
 * \return      TRUE (1) if the pipeline is enabled, or else FALSE (0)
 *
 * \sa smaxSetPipelined()
 */
boolean smaxIsPipelined() {
  return usePipeline;
}


/**
 * Set the size of the TCP/IP buffers (send and receive) for future client connections.
 *
 * @param size      (bytes) requested buffer size, or <= 0 to use default value
 *
 * @sa smaxGetTcpBuf()
 */
void smaxSetTcpBuf(int size) {
  tcpBufSize = size;
}

/**
 * Returns the current TCP/IP buffer size (send and receive) to be used for future client connections.
 *
 * @return      (bytes) future TCP/IP buffer size, 0 if system default.
 *
 * @sa smaxSetTcpBuf()
 */
int smaxGetTcpBuf() {
  return tcpBufSize > 0 ? tcpBufSize : 0;
}


/**
 * Returns the host name on which we are running. It returns a reference to the same
 * static variable every time. As such you should never call free() on the returned value.
 *
 * \return      The host computer/node name.
 *
 * @sa smaxSetHostName()
 */
char *smaxGetHostName() {
  return redisxGetHostName();
}

/**
 * Sets a user-specified host name, to use instead of the system default.
 *
 * @param name  The user specified host name, or NULL to restore the system default.
 *
 * @sa smaxGetHostName()
 */
void smaxSetHostName(const char *name) {
  redisxSetHostName(name);
}

/**
 * Returns the SMA-X program ID.
 *
 * \return      The SMA-X program ID as &lt;hostname&gt;:&lt;programname&gt;, e.g. "hal9000:statusServer".
 *
 */
char *smaxGetProgramID() {
#if __Lynx__
  char procName[40];
#else
  const char *procName;
  extern char *__progname;
#endif

  const char *host;

  if(programID) return programID;

#if __Lynx__
  getProcessName(getpid(), procName, 40);
#else
  procName = __progname;
#endif

  host = smaxGetHostName();
  programID = xGetAggregateID(host, procName);

  return programID;
}


/**
 * Returns the Redis connection information for SMA-X
 *
 * \return      The structure containing the Redis connection data.
 *
 * @sa smaxConnect()
 * @sa smaxConnectTo()
 * @sa smaxIsConnected()
 */
Redis *smaxGetRedis() {
  return redis;
}


/**
 * Checks whether SMA-X sharing is currently open (by a preceding call to smaxConnect() call.
 *
 * \sa smaxConnect()
 * @sa smaxConnectTo()
 * \sa smaxDisconnect()
 * @sa smaxReconnect()
 */
int smaxIsConnected() {
  return redisxIsConnected(redis);
}


/**
 * Initializes the SMA-X sharing library in this runtime instance, with the default Redis server address.
 *
 *
 * \return      X_SUCCESS           If the library was successfully initialized
 *              X_ALREADY_OPEN      If SMA-X sharing was already open.
 *              X_NO_SERVICE        If the there was an issue establishing the necessary network connection(s).
 *              X_NAME_INVALID      If the default redis name lookup failed.
 *              X_NULL              If the Redis IP address is NULL
 *
 * @sa smaxConnectTo()
 * \sa smaxDisconnect()
 * @sa smaxReconnect()
 * @sa smaxIsConnected()
 */
int smaxConnect() {
  return smaxConnectTo(SMAX_DEFAULT_HOSTNAME);
}


/**
 * Initializes the SMA-X sharing library in this runtime instance with the specified Redis server. SMA-X is
 * initialized in resilient mode, so that we'll automatically attempt to reconnect to the Redis server if
 * the connection is severed (once it was established). If that is not the desired behavior, you should
 * call <code>smaxSetResilient(FALSE)</code> after connecting.
 *
 * \param server    SMA-X Redis server name or IP address, e.g. "127.0.0.1".
 *
 * \return      X_SUCCESS           If the library was successfully initialized
 *              X_NO_SERVICE        If the there was an issue establishing the necessary network connection(s).
 *
 * @sa smaxConnect()
 * @sa smaxDisconnect()
 * @sa smaxReconnect()
 * @sa smaxIsConnected()
 * @sa smaxSetResilient()
 *
 */
int smaxConnectTo(const char *server) {
  static int isInitialized = FALSE;

  const char *funcName = "smaxConnectTo()";

  int status;

  smaxLockConfig();

  if(smaxIsConnected()) {
    smaxUnlockConfig();
    return X_SUCCESS;
  }

  // START one-time-only initialization ------>
  if(!isInitialized) {
    xvprintf("SMA-X> Initializing...\n");

    smaxGetProgramID();
    xvprintf("SMA-X> program ID: %s\n", programID);

    redisxSetTcpBuf(tcpBufSize);
    redis = redisxInit(server);
    if(redis == NULL) {
      smaxUnlockConfig();
      return smaxError(funcName, X_NO_INIT);
    }

    redisxSetTransmitErrorHandler(redis, smaxTransmitErrorHandler);

    smaxSetPipelineConsumer(smaxProcessPipedWritesAsync);
    smaxAddSubscriber(NULL, ProcessUpdateNotificationAsync);

    // Initial sotrage for update notifications
    notifySize = 80;
    notifyID = (char *) calloc(1, notifySize);

    isInitialized = TRUE;
  }
  // END one-time-only initialization <--------

  xvprintf("SMA-X> Connecting...\n");

  // Reset LUA script hashes after connecting to Redis.
  smaxAddConnectHook(InitScriptsAsync);

  // Flush lazy cache after disconnecting from Redis.
  smaxAddDisconnectHook((void (*)) smaxLazyFlush);

  status = redisxConnect(redis, usePipeline);
  if(status) {
    smaxUnlockConfig();
    return smaxError(funcName, status);
  }

  // By default, we'll try to reconnect to Redis if the connection is severed.
  smaxSetResilient(TRUE);

  smaxUnlockConfig();

  xvprintf("SMA-X> opened & ready.\n");

  return status;
}


/**
 * Disables the SMA-X sharing capability, closing underlying network connections.
 *
 * \return      X_SUCCESS (0)       if the sharing was properly ended.
 *              X_NO_INIT           if SMA-X was has not been started prior to this call.
 *
 * @sa smaxConnect()
 * @sa smaxConnectTo()
 * @sa smaxReconnect()
 * @sa smaxIsConnected()
 */
int smaxDisconnect() {
  if(!smaxIsConnected()) return X_NO_INIT;

  redisxDisconnect(redis);

  xvprintf("SMA-X> closed.\n");

  return X_SUCCESS;
}


/**
 * Reconnects to the SMA-X server. It will try connecting repeatedly at regular intervals until the
 * connection is made. If resilient mode is enabled, then locally accumulated shares will be sent to
 * the Redis server upon reconnection. However, subscriptions are not automatically re-established. The
 * caller is responsible for reinstate any necessary subscriptions after the reconnection or via an
 * approproate connection hook.
 *
 * \return      X_SUCCESS (0)   if successful
 *              X_NO_INIT       if SMA-X was never initialized.
 *
 *              or the error returned by redisxReconnect().
 *
 * @sa smaxConnect()
 * @sa smaxConnectTo()
 * @sa smaxDisconnect()
 * @sa smaxIsConnected()
 * @sa smaxSetResilient()
 * @sa smaxAddConnectHook()
 */
int smaxReconnect() {
  if(redis == NULL) return X_NO_INIT;

  xvprintf("SMA-X> reconnecting.\n");

  while(redisxReconnect(redis, usePipeline) != X_SUCCESS) if(SMAX_RECONNECT_RETRY_SECONDS > 0)
    sleep(SMAX_RECONNECT_RETRY_SECONDS);

  return X_SUCCESS;
}

/**
 * Add a callback function for when SMA-X is connected. It's a wrapper to redisxAddConnectHook().
 *
 * @param setupCall     Callback function
 *
 * @sa smaxRemoveConnectHook()
 * @sa smaxConnect()
 * @sa smaxConnectTo()
 */
void smaxAddConnectHook(const void (*setupCall)(void)) {
  redisxAddConnectHook(smaxGetRedis(), setupCall);
}

/**
 * Remove a post-connection callback function. It's a wrapper to redisxRemoveConnectHook().
 *
 * @param setupCall     Callback function
 *
 * @sa smaxAddConnectHook()
 * @sa smaxConnect()
 * @sa smaxConnectTo()
 */
void smaxRemoveConnectHook(const void (*setupCall)(void)) {
  redisxRemoveConnectHook(smaxGetRedis(), setupCall);
}


/**
 * Add a callback function for when SMA-X is disconnected. It's a wrapper to redisxAddDisconnectHook().
 *
 * @param cleanupCall   Callback function
 *
 * @sa smaxRemoveDisconnectHook()
 * @sa smaxDisconnect()
 */
void smaxAddDisconnectHook(const void (*cleanupCall)(void)) {
  redisxAddDisconnectHook(smaxGetRedis(), cleanupCall);
}

/**
 * Remove a post-cdisconnect callback function. It's a wrapper to redisxRemiveDisconnectHook().
 *
 * @param cleanupCall   Callback function
 *
 * @sa smaxAddDisconnectHook()
 * @sa smaxDisconnect()
 */
void smaxRemoveDisconnectHook(const void (*cleanupCall)(void)) {
  redisxRemoveDisconnectHook(smaxGetRedis(), cleanupCall);
}

/**
 * Change the pipeline response consumer function (from it's default or other previous consumer). It is a wrapper
 * for redisxSetPipelineConsumer().
 *
 * @param f     The function to process ALL pipeline responses from Redis.
 * @return      X_SUCCESS if successful, or else an error by redisxSetPipelineConsumer()
 *
 * @sa smaxSetPipelined()
 * @sa smaxIsPipelined()
 */
int smaxSetPipelineConsumer(void (*f)(RESP *)) {
  return redisxSetPipelineConsumer(smaxGetRedis(), f);
}

/**
 * Pull data from the specified hash table. This calls data via the interactive client to Redis.
 *
 * \param[in]   table     Hash table name.
 * \param[in]   key       Variable name under which the data is stored.
 * \param[in]   type      SMA-X variable type, e.g. X_FLOAT or X_CHARS(40), of the buffer.
 * \param[in]   count     Number of points to retrieve into the buffer.
 * \param[out]  value     Pointer to the buffer to which the data is to be retrieved.
 * \param[out]  meta      Pointer to metadata or NULL if no metadata is needed.
 *
 * \return          X_SUCCESS (0)       if successful, or
 *                  X_NO_INIT           if the SMA-X library was not initialized.
 *                  X_HOST_INVALID      if the host (owner ID) is NULL.
 *                  X_NAME_INVALID      if the 'key' argument is NULL.
 *                  X_NULL_VALUE        if the 'value' argument is NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 *
 * @sa smaxLazyPull()
 * @sa smaxQueue()
 */
int smaxPull(const char *table, const char *key, XType type, int count, void *value, XMeta *meta) {
  PullRequest *data;
  int status;

  data = (PullRequest *) calloc(1, sizeof(PullRequest));

  // Make sure structures are retrieved all the same no matter how their names are split
  // into group + key.
  if(type == X_STRUCT) {
    data->group = xGetAggregateID(table, key);
    data->key = NULL;
  }
  else {
    data->group = xStringCopyOf(table);
    data->key = xStringCopyOf(key);
  }

  data->value = value;
  data->type = type;
  data->count = count;
  data->meta = meta;

  status = smaxRead(data, INTERACTIVE_CHANNEL);
  //if(status) smaxZero(value, type, count);

  smaxDestroyPullRequest(data);

  if(status) return smaxError("smaxPull()", status);

  return X_SUCCESS;
}


/**
 * Share the data into a Redis hash table over the interactive Redis client. It's a fire-and-forget
 * type implementation, which sends the data to Redis, without waiting for confirmation of its arrival.
 * The choice improves the efficiency and throughput, and minimizes execution time, of the call, but it
 * also means that a pipelined pull request in quick succession, e.g. via smaxQueue(), may return
 * a value on the pipeline client _before_ this call is fully executed on the interactive Redis client.
 *
 * (It is generally unlikely that you will follow this share call with a pipelined pull of the same
 * variable. It would not only create superflous network traffic for no good reason, but it also
 * would have unpredictable results. So, don't.)
 *
 * \param table     Hash table name in which to share entry.
 * \param key       Variable name under which the data is stored.
 * \param value     Pointer to the buffer whose data is to be shared.
 * \param type      SMA-X variable type, e.g. X_FLOAT or X_CHARS(40), of the buffer.
 * \param count     Number of 1D elements.
 *
 * \return          X_SUCCESS (0)       if successful, or
 *                  X_NO_INIT           if the SMA-X library was not initialized.
 *                  X_HOST_INVALID      if the host (owner ID) is NULL.
 *                  X_NAME_INVALID      if the 'key' argument is NULL.
 *                  X_SIZE_INVALID      if count < 1 or count > X_MAX_ELEMENTS
 *                  X_NULL_VALUE        if the 'value' argument is NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 *
 * \sa smaxShareArray()
 * \sa smaxShareField()
 * \sa smaxShareStruct()
 */
int smaxShare(const char *table, const char *key, const void *value, XType type, int count) {
  return smaxShareArray(table, key, value, type, 1, &count);
}


/**
 * Share a multidimensional array, such as an int[][][], or float[][], in a single atomic
 * transaction.
 *
 * \param table     Hash table in which to write entry.
 * \param key       Variable name under which the data is stored.
 * \param ptr       Pointer to the data buffer, such as an int[][][] or float[][].
 * \param type      SMA-X variable type, e.g. X_FLOAT or X_CHARS(40), of the buffer.
 * \param ndim      Dimensionality of the data (0 <= ndim <= X_MAX_DIMS).
 * \param sizes     An array of ints containing the sizes along each dimension.
 *
 * \return          X_SUCCESS (0)       if successful, or
 *                  X_NO_INIT           if the SMA-X library was not initialized.
 *                  X_HOST_INVALID      if the host (owner ID) is NULL.
 *                  X_NAME_INVALID      if the 'key' argument is NULL.
 *                  X_SIZE_INVALID      if ndim or sizes are invalid.
 *                  X_NULL_VALUE        if the 'value' argument is NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 *
 * @sa smaxShare()
 */
int smaxShareArray(const char *table, const char *key, const void *ptr, XType type, int ndim, const int *sizes) {
  const char *funcName = "smaxShareArray()";

  XField f = {0};
  char trybuf[REDIS_CMDBUF_SIZE];
  int count, status;

  if(ndim < 0 || ndim > X_MAX_DIMS) return smaxError(funcName, X_SIZE_INVALID);
  count = xGetElementCount(ndim, sizes);
  if(count < 1 || count > X_MAX_ELEMENTS) return smaxError(funcName, X_SIZE_INVALID);

  f.value = (type == X_RAW || type == X_STRUCT) ?
          (char *) ptr :
          smaxValuesToString(ptr, type, count, trybuf, REDIS_CMDBUF_SIZE);

  if(f.value == NULL) return smaxError(funcName, X_NULL);

  f.isSerialized = TRUE;
  f.name = (char *) key;
  f.type = type;
  f.ndim = ndim;
  memcpy(f.sizes, sizes, ndim * sizeof(int));

  status = smaxShareField(table, &f);

  if(f.value != trybuf) if(type != X_RAW && type != X_STRUCT) free(f.value);

  return status;
}


/**
 * Share a field object, which may contain any SMA-X data type.
 *
 * \param table     Hash table in which to write entry.
 * \param f         Pointer for XField holding the data to share.
 *
 * \return          X_SUCCESS (0)       if successful, or
 *                  X_NO_INIT           if the SMA-X library was not initialized.
 *                  X_HOST_INVALID      if the host (owner ID) is NULL.
 *                  X_NAME_INVALID      if the 'key' argument is NULL.
 *                  X_SIZE_INVALID      if ndim or sizes are invalid.
 *                  X_NULL_VALUE        if the 'value' argument is NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 *
 * \sa smaxShare()
 * \sa smaxShareField()
 * \sa smaxShareStruct()
 * @sa xSetField()
 * @sa xGetField()
 */
int smaxShareField(const char *table, const XField *f) {
  int status;

  if(f->type == X_STRUCT) {
    char *id = xGetAggregateID(table, f->name);
    status = smaxShareStruct(id, (XStructure *) f->value);
    if(id != NULL) free(id);
    return status;
  }

  status = smaxWrite(table, f);
  if(status) {
    if(status == X_NO_SERVICE) smaxStorePush(table, f);
    return smaxError("smaxShareField()", status);
  }

  return X_SUCCESS;
}


/**
 * Sends a structure to Redis, and all its data including recursive
 * sub-structures, in a single atromic transaction.
 *
 * \param id        Structure's ID, i.e. its own aggregated hash table name.
 * \param s         Pointer to the structure data.
 *
 * \return          X_SUCCESS (0)       if successful, or
 *                  X_NO_INIT           if the SMA-X library was not initialized.
 *                  X_HOST_INVALID      if the host (owner ID) is NULL.
 *                  X_NAME_INVALID      if the 'key' argument is NULL.
 *                  X_NULL_VALUE        if the 'value' argument is NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 * \sa smaxShare()
 * \sa smaxShareField()
 * \sa xCreateStruct()
 */
static int SendStruct(const char *id, const XStructure *s) {
  const char *funcName = "SendStruct()";

  RedisClient *cl = redis->interactive;
  int status;

  if(id == NULL) return smaxError(funcName, X_GROUP_INVALID);
  if(s == NULL) return smaxError(funcName, X_NULL);
  if(!redisxIsConnected(redis)) return smaxError(funcName, X_NO_SERVICE);

  status = redisxLockEnabled(cl);
  if(!status) {
    // TODO the following should be done atomically, but multi/exec blocks don't work
    // with evalsha(?)...

    // Send the structure data, recursively
    status = SendStructDataAsync(cl, id, s, TRUE);
    redisxUnlockClient(cl);
  }

  return status ? smaxError(funcName, status) : X_SUCCESS;
}

/**
 * Share a structure, and all its data including recursive
 * sub-structures, in a single atromic transaction.
 *
 * \param id        Structure's ID, i.e. its own aggregated hash table name.
 * \param s         Pointer to the structure data.
 *
 * \return          X_SUCCESS (0)       if successful, or
 *                  X_NO_INIT           if the SMA-X library was not initialized.
 *                  X_HOST_INVALID      if the host (owner ID) is NULL.
 *                  X_NAME_INVALID      if the 'key' argument is NULL.
 *                  X_NULL_VALUE        if the 'value' argument is NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 * \sa smaxShare()
 * \sa smaxShareField()
 * \sa xCreateStruct()
 */
int smaxShareStruct(const char *id, const XStructure *s) {
  int status = SendStruct(id, s);

  if(status == X_NO_SERVICE) {
    XField *f = smaxCreateField(id, X_STRUCT, 0, NULL, s);

    if(f) {
      smaxStorePush(NULL, f);
      free(f);
    }
  }

  return status;
}

/**
 * Subscribes to a specific key(s) in specific group(s). Both the group and key names may contain Redis
 * subscription patterns, e.g. '*' or '?', or bound characters in square-brackets, e.g. '[ab]'. The
 * subscription only enables receiving update notifications from Redis for the specified variable or
 * variables. After subscribing, you can either wait on the subscribed variables to change, or add
 * callback functions to process subscribed variables changes, via smaxAddSubscriber().
 *
 *
 *
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
  char *p = smaxGetUpdateChannelPattern(table, key);
  int status = redisxSubscribe(redis, p);
  free(p);
  if(status) return smaxError("smaxSubscribe()", status);
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
  char *p = smaxGetUpdateChannelPattern(table, key);
  int status = redisxUnsubscribe(redis, p);
  free(p);
  if(status) return smaxError("smaxUnsubscribe()", status);
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
void smaxAddSubscriber(const char *idStem, RedisSubscriberCall f) {
  char *stem = xGetAggregateID(SMAX_UPDATES_ROOT, idStem);
  redisxAddSubscriber(smaxGetRedis(), stem, f);
  free(stem);
}

/**
 * Remove all instances of a subscriber callback function from the current list of functions processing PUB/SUB messages.
 * This call only deactivates the callback routine, but does not stop the delivery of update notifications from
 * the Redis server. You should therefore also call smaxUnsubscribe() as appropriate to stop notifications
 * for variables that no longer have associated callbacks.
 *
 * @param f     Function to remove
 * @return      X_SUCCESS if successful, or else an error returned by redisxRemoveSubscriber().
 *
 * @sa smaxUnsubscribe()
 */
void smaxRemoveSubscribers(RedisSubscriberCall f) {
  redisxRemoveSubscribers(smaxGetRedis(), f);
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
    sprintf(p, SMAX_UPDATES "%s", table);
  }
  else {
    p = (char *) malloc(sizeof(SMAX_UPDATES) + strlen(table) + X_SEP_LENGTH + strlen(key));
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
 *
 * \return      X_SUCCESS (0)       if a variable was pushed on a host.
 *              X_NO_INIT           if the SMA-X sharing was not initialized via smaxConnect().
 *              X_HOST_INVALID      if the buffer for the returned host name is NULL.
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
int smaxWaitOnAnySubscribed(char **changedTable, char **changedKey, int timeout) {
  const char *funcName = "smaxWaitOnAnySubscribed()";
  int status = X_SUCCESS;
  struct timespec endTime;

  if(changedTable == NULL) return smaxError(funcName, X_GROUP_INVALID);
  if(changedKey == NULL) return smaxError(funcName, X_NAME_INVALID);

  xvprintf("SMA-X> waiting for notification...\n");

  *changedTable = NULL;
  *changedKey = NULL;

  if(timeout > 0) {
    clock_gettime(CLOCK_REALTIME, &endTime);
    endTime.tv_sec += timeout;
  }

  smaxLockNotify();

  // Waits for a notification...
  while(*changedTable == NULL) {
    const char *sep;

    status = timeout > 0 ? pthread_cond_timedwait(&notifyBlock, &notifyLock, &endTime) : pthread_cond_wait(&notifyBlock, &notifyLock);
    if(status) {
      // If the wait returns with an error, the mutex is uncloked.
      if(status == ETIMEDOUT) return X_INCOMPLETE;

      fprintf(stderr, "WARNING! SMA-X : pthread_cond_wait() error %d. Ignored.\n", status);
      continue;
    }

    // Check for premature release...
    if(!strcmp(notifyID, RELEASEID)) {
      smaxUnlockNotify();
      return smaxError(funcName, X_INTERRUPTED);
    }

    if(notifyID[0] == '\0') {
      fprintf(stderr, "WARNING! SMA-X : published message contained NULL. Ignored.\n");
      continue;
    }

    xvprintf("SMA-X> %s: got %s.\n", funcName, notifyID);

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

  if(status) return smaxError(funcName, status);

  return X_SUCCESS;
}


/**
 * Unblocks all sma_wait*() calls, which will return X_REL_PREMATURE, as a result.
 *
 * \return  X_SUCCESS (0)       if a variable was pushed on a host.
 *          X_NO_INIT           if the SMA-X sharing was not initialized, e.g. via smaxConnect().
 *
 * \sa smaxWaitOnAnySubscribed()
 *
 */
int smaxReleaseWaits() {
  xvprintf("SMA-X> release read.\n");

  smaxLockNotify();

  if(notifySize < sizeof(RELEASEID)) {
    char *oldid = notifyID;
    notifyID = realloc(notifyID, sizeof(RELEASEID));
    if(!notifyID) {
      perror("WARNING! realloc notifyID");
      free(oldid);
    }
    notifySize = notifyID ? sizeof(RELEASEID) : 0;
  }

  if(notifyID) {
    strcpy(notifyID, RELEASEID);
    pthread_cond_broadcast(&notifyBlock);
  }
  smaxUnlockNotify();

  return X_SUCCESS;
}



/**
 * Retrieve the current number of variables stored on host (or owner ID).
 *
 * \param table    Hash table name.
 *
 * \return         The number of keys (fields) in the specified table (>= 0), or an error code (<0), such as:
 *                 X_NO_INIT           if the SMA-X sharing was not initialized, e.g. via smaConnect().
 *                 X_HOST_INVALID      if the host (owner ID) is NULL.
 *                 or one of the errors returned by redisxArrayRequest().
 *
 * @sa smaxGetKeys()
 */
int smaxKeyCount(const char *table) {
  const char *funcName = "smaNumVars()";
  RESP *reply;
  int status;

  if(table == NULL) return smaxError(funcName, X_GROUP_INVALID);

  reply = redisxRequest(redis, "HLEN", table, NULL, NULL, &status);
  if(status) {
    redisxDestroyRESP(reply);
    return smaxError(funcName, status);
  }

  status = redisxCheckRESP(reply, RESP_INT, 0);
  if(status) return redisxError(funcName, status);

  status = reply->n;
  redisxDestroyRESP(reply);

  if(status < 0) return smaxError(funcName, status);

  xvprintf("SMA-X> Get number of variables: %d.\n", reply->n);

  return reply->n;
}


/**
 * Returns a snapshot of the key names stored in a given Redis hash table, ot NULL if there
 * was an error.
 *
 * \param[out]  table     Host name or owner ID whose variable to count.
 * \param[out]  n         Pointer to which the number of keys (>=0) or an error (<0) is returned.
 *                        An error returned by redisxGetKeys(), or else:
 *
 *                          X_NO_INIT           if the SMA-X sharing was not initialized, e.g. via smaxConnect().
 *                          X_HOST_INVALID      if the host (owner ID) is NULL.
 *
 * \return          An array of pointers to the names of Redis keys.
 *
 * @sa smaxKeyCount()
 */
char **smaxGetKeys(const char *table, int *n) {
  char **keys;

  if(table == NULL) {
    *n = X_GROUP_INVALID;
    return NULL;
  }

  xvprintf("SMA-X> get variable names.\n");

  keys = redisxGetKeys(redis, table, n);
  if(*n > 0) return keys;

  // CLEANUP --- There was an error.
  if(keys) {
    int i;
    for(i=*n; --i >= 0; ) if(keys[i]) free(keys[i]);
    free(keys);
  }
  *n = 0;
  return NULL;
}


/**
 * \cond PROTECTED
 *
 * Retrieves data from the SMA-X database, interatively or as a pipelined request.
 *
 * \param[in,out]   req           Pull request
 * \param[in]       channel       INTERACTIVE_CHANNEL or PIPELINE_CHANNEL
 *
 * \return              X_SUCCESS (0)       if successful, or
 *                      X_NULL              if the request or its value field is NULL
 *                      X_N_INIT            if the SMA-X library was not initialized.
 *                      X_HOST_INVALID      if the host (owner ID) is NULL.
 *                      X_GROUP_INVALID     if the 'group' argument is NULL.
 *                      X_NO_SERVICE        if there was no connection to the Redis server.
 *                      X_FAILURE           if there was an underlying failure.
 */
int smaxRead(PullRequest *req, int channel) {
  const char *funcName = "smaxRead()";
  char *args[5], *script = NULL;
  RESP *reply = NULL;
  RedisClient *cl;
  int status, n = 0;

  if(req == NULL) return smaxError(funcName, X_NULL);
  if(req->group == NULL) return smaxError(funcName, X_GROUP_INVALID);
  if(req->value == NULL) return smaxError(funcName, X_NULL);
  if(redis == NULL) smaxError(funcName, X_NO_INIT);
  if(!redisxIsConnected(redis)) return smaxError(funcName, X_NO_SERVICE);

  xvprintf("SMA-X> read %s:%s.\n", (req->group == NULL ? "" : req->group), req->key);

  script = (req->type == X_STRUCT) ? GET_STRUCT : HGET_WITH_META;

  if(!script) return smaxScriptError(req->type == X_STRUCT ? "GetStruct" : "HGetWithMeta", X_NULL);

  if(req->type == X_STRUCT || req->meta != NULL) {
    // Use Atomic scripts for structures and when requesting metadata
    args[n++] = "EVALSHA";
    args[n++] = script;
    args[n++] = "1";    // number of Redis keys sent.

    if(req->type == X_STRUCT) {
      args[n++] = req->group;
    }
    else {
      args[n++] = req->group;
      args[n++] = req->key;
    }
  }
  else {
    // If not requesting a struct or metadata, then use simple HGET
    args[n++] = "HGET";
    args[n++] = req->group;
    args[n++] = req->key;
  }

  cl = redisxGetClient(redis, channel);
  if(cl == NULL) return smaxError(funcName, X_NO_SERVICE);

  status = redisxLockEnabled(cl);
  if(status) return smaxError(funcName, status);

  // Call script
  status = redisxSendArrayRequestAsync(cl, args, NULL, n);

  if(channel != PIPELINE_CHANNEL) if(!status) reply = redisxReadReplyAsync(cl);

  redisxUnlockClient(cl);

  // Process reply as needed...
  if(channel != PIPELINE_CHANNEL) if(!status) {
    // Process the value
    status = smaxProcessReadResponse(reply, req);
    redisxDestroyRESP(reply);
  }

  if(status) return smaxError(funcName, status);

  return X_SUCCESS;
}
/// \endcond


/**
 * Private error handling for xProcessReadResponse(). Not used otherwise.
 *
 */
static int RequestError(const PullRequest *req, int status) {
  if(req->meta) req->meta->status = status;
  return smaxError("xProcessReadResponse()", status);
}


/**
 * \cond PROTECTED
 *
 * Converts a string data, from a Redis response, to binary values for the given variable.
 *
 * \param[in]       reply         String content of the Redis response to a read.
 * \param[in,out]   req           Pointer to a PullRequest structure to be completed with the data.
 *
 * \return              X_SUCCESS (0) if successful, or else an appropriate error
 *                      such as expected for sma_pull().
 *
 */
int smaxProcessReadResponse(RESP *reply, PullRequest *req) {
  int status = X_SUCCESS;
  RESP *data = NULL;

  if(req == NULL) return smaxError("smaxProcessReadResponse()", X_NULL);
  if(reply == NULL) return RequestError(req, REDIS_NULL);

  // Clear metadata if requested.
  if(req->meta != NULL) smaxResetMeta(req->meta);

  // Safety pin for X_RAW / X_STRUCT processing...
  if(req->type == X_RAW || req->type == X_STRUCT) req->count = 1;

  // Check that request is not crazy.
  if(req->count <= 0) return RequestError(req, X_SIZE_INVALID);

  // (nil)
  if(reply->n < 0) {
    // (can be used to check if key existed...)
    xZero(req->value, req->type, req->count);
    return X_SUCCESS;
  }

  // If we had a NULL value without n < 0, then it's an error.
  if(!req->value) return RequestError(req, X_NULL);
  xvprintf("SMA-X> received %s:%s.\n", req->group == NULL ? "" : req->group, req->key);

  // If expecting metadata, then initialize the destination metadata to defaults.
  if(req->meta != NULL) smaxResetMeta(req->meta);

  if(reply->type == RESP_BULK_STRING) {
    // Got value only, without metadata
    data = reply;
  }
  else if(reply->type == RESP_ARRAY) {
    // Got value together with metadata
    RESP **component = (RESP **) reply->value;

    if(req->type == X_STRUCT) {
      if(reply->n == 0) return X_NAME_INVALID;          // No such structure...
      return ProcessStructRead(component, req);
    }

    // The part that contains the value(s)
    data = component[0];

    // Fill the metadata as requested...
    if(req->meta != NULL) {
      // Fill in the metadata as much as possible.
      XMeta *m = req->meta;

      m->storeBytes = component[0]->n;
      if(reply->n > 1) m->storeType = xTypeForString((char *) component[1]->value);
      if(reply->n > 2) m->storeDim = xParseDims((char *) component[2]->value, m->storeSizes);
      if(reply->n > 3) smaxParseTime((char *) component[3]->value, &m->timestamp.tv_sec, &m->timestamp.tv_nsec);
      if(reply->n > 4) smaxSetOrigin(m, (char *) component[4]->value);
      if(reply->n > 5) if(component[5]->value) m->serial = strtol((char *) component[5]->value, NULL, 10);
    }
  }
  else data = NULL;

  if(req->value != NULL) {
    if(data == NULL) {
      // Fill with zeroes...
      xZero(req->value, req->type, req->count);
    }
    else if(data->value == NULL) {
      // Fill with zeroes...
      xZero(req->value, req->type, req->count);
    }
    else if(req->type == X_RAW) {
      // Simply move the pointer to the raw value over to the pull request.
      *(char **) req->value = (char *) data->value;    // req->value is a pointer to a string reference, i.e. char**
      data->value = NULL;                              // Dereference the text so it does not get destroyed with the RESP.
    }
    else if(req->type == X_STRING) {
      // Unpack strings
      xUnpackStrings((char *) data->value, data->n, req->count, (char **) req->value);
    }
    else {
      // Unpack fixed-sized types.
      int parsed;
      status = smaxStringToValues((char *) data->value, req->value, req->type, req->count, &parsed);
      if(status >= 0) status = X_SUCCESS; // Keeps errors only, not the number of elements parsed.
    }
  }

  if(reply->type == RESP_ERROR) if(strstr("NOSCRIPT", (char *) reply->value)) return smaxScriptError("smaxProcessReadResponse()", X_NULL);

  if(status) return RequestError(req, status);

  return X_SUCCESS;
}
/// \endcond

static int ProcessStructRead(RESP **component, PullRequest *req) {
  const char *funcName = "xProcessStructRead()";

  XStructure *base = (XStructure *) req->value;
  XStructure **s;
  XMeta *metas = NULL;
  char **names;
  int i, nStructs, status = X_SUCCESS;

  nStructs = component[0]->n;

  if(nStructs <= 0) return smaxError(funcName, X_STRUCT_INVALID);

  // Allocate temporary storage
  names = (char **) calloc(nStructs, sizeof(char *));
  metas = (XMeta *) calloc(nStructs, sizeof(XMeta));

  if(req->meta != NULL) {
    smaxResetMeta(req->meta);
    req->meta->storeType = X_STRUCT;
    req->meta->storeDim = 1;
    req->meta->storeSizes[0] = 1;
  }

  // Parse all structure data (for embedded structures)
  s = (XStructure **) calloc(nStructs, sizeof(XStructure *));

  for(i=0; i<nStructs; i++) {
    RESP **sub = (RESP **) component[0]->value;

    s[i] = xCreateStruct();
    names[i] = (char *) sub[i]->value;

    status = ParseStructData(s[i], component[2*i+1], component[2*i+2], &metas[i]);
    if(status) break;

    // Set the meta for the last value written
    if(req->meta != NULL) {
      struct timespec *rt = &req->meta->timestamp;
      const struct timespec *t = &metas[i].timestamp;
      if((t->tv_sec + 1e-9*t->tv_nsec) > (rt->tv_sec + 1e-9 * rt->tv_nsec)) {
        *rt = *t;
        smaxSetOrigin(req->meta, metas[i].origin);
        req->meta->serial = metas[i].serial;
      }
    }
  }

  if(!status) {
    // Assign substructures...
    for(i=0; i<nStructs; i++) {
      XField *f;

      // For each substructure field, try match one of the reported structure components.
      for(f = s[i]->firstField; f != NULL; f = f->next) if(f->type == X_STRUCT) {
        int k = nStructs; // default for unassigned

        if(f->value != NULL) for(k=0; k<nStructs; k++) if(names[k]) if(!strcmp(names[k], f->value)) {
          // Replace the name with the corresponding structure as the value.
          free(f->value);
          f->value = (char *) s[k];
          s[k]->parent = s[i];
          names[k] = NULL;
          break;
        }

        // Set unassigned (orphaned) nested structures to empty structs.
        if(k >= nStructs) {
          XStructure *sub = xCreateStruct();
          if(f->value) free(f->value);
          f->value = (char *) sub;
          sub->parent = s[i];
        }
      }
    }

    // Assuming structure with name equals read->key as the top-level structure
    for(i=0; i<nStructs; i++) if(names[i]) if(!strcmp(names[i], req->group)) break;

    if(i == nStructs) i = 0;  // Default assignment to the first returned structure...

    // Destroy the prior contents of the destination structure...
    xClearStruct(base);

    // Copy the top level structure into the destination...
    *base = *s[i];
    free(s[i]);   // We can free the original top-level structure, after having copied it to the destination.
  }

  // Clean up temporary allocations.
  free(names);
  free(metas);
  free(s);

  if(status) return smaxError(funcName, status);

  return X_SUCCESS;
}


static int ParseStructData(XStructure *s, RESP *names, RESP *data, XMeta *meta) {
  const char *funcName = "xParseStructData()";

  RESP **component, **keys, **values, **types, **dims, **timestamps, **origins, **serials;
  int i;

  if(names == NULL) return smaxError(funcName, X_NULL);
  if(data == NULL) return smaxError(funcName, X_NULL);
  if(names->type != RESP_ARRAY) return smaxError(funcName, REDIS_UNEXPECTED_RESP);
  if(data->type != RESP_ARRAY) return smaxError(funcName, REDIS_UNEXPECTED_RESP);
  if(data->n != HMGET_COMPONENTS) return smaxError(funcName, X_NOT_ENOUGH_TOKENS);

  component = (RESP **) data->value;

  for(i=HMGET_COMPONENTS; --i >= 0; ) {
    if(component[i]->type != RESP_ARRAY) return smaxError(funcName, REDIS_UNEXPECTED_RESP);
    if(component[i]->n != names->n) return smaxError(funcName, X_NOT_ENOUGH_TOKENS);
  }

  if(meta != NULL) smaxResetMeta(meta);

  keys = (RESP **) names->value;

  values = (RESP **) component[HMGET_VALUE_OFFSET]->value;
  types = (RESP**) component[HMGET_TYPE_OFFSET]->value;
  dims = (RESP **) component[HMGET_DIMS_OFFSET]->value;
  timestamps = (RESP **) component[HMGET_TIMESTAMP_OFFSET]->value;
  origins = (RESP **) component[HMGET_ORIGIN_OFFSET]->value;
  serials = (RESP **) component[HMGET_SERIAL_OFFSET]->value;

  for(i=0; i<names->n; i++) {
    XField *f = (XField *) calloc(1, sizeof(XField));

    f->isSerialized = TRUE;
    f->name = keys[i]->value;
    keys[i]->value = NULL;      // Dereference the RESP field name so it does not get destroyed with RESP.

    f->value = (char *) values[i]->value;
    values[i]->value = NULL;    // Dereference the RESP data so it does not get destroyed with RESP.

    f->type = xTypeForString((char *) types[i]->value);
    f->ndim = xParseDims((char *) dims[i]->value, f->sizes);

    xSetField(s, f);

    if(meta != NULL) {
      time_t sec;
      long nsec;

      smaxParseTime((char *) timestamps[i]->value, &sec, &nsec);

      // Set the meta to that of the last field written...
      if((sec + 1e-9 * nsec) > (meta->timestamp.tv_sec + 1e-9 * meta->timestamp.tv_nsec)) {
        meta->timestamp.tv_sec = sec;
        meta->timestamp.tv_nsec = nsec;
        smaxSetOrigin(meta, (char *) origins[i]->value);
        if(serials[i]->value) meta->serial = strtol(serials[i]->value, NULL, 10);
      }
    }
  }

  return X_SUCCESS;
}


/**
 *
 * \cond PROTECTED
 *
 * Sends a write request to REDIS over the specified communication channel. The caller
 * is responsible for granting exclusive access to that channel before calling this
 * function in order to avoid clobber in a parallel environment.
 *
 * It's a fire-and-forget type implementation, which sends the data to Redis, without waiting for
 * confirmation of its arrival. The choice improves the efficiency and throughput, and minimizes execution
 * time, of the call, but it also means that a pipelined pull request in quick succession, e.g. via smaxQueue(),
 * may return a value on the pipeline client _before_ this call is fully executed on the interactive Redis client.
 *
 * (It is generally unlikely that you will follow this share call with a pipelined pull of the same
 * variable. It would not only create superflous network traffic for no good reason, but it also
 * would have unpredictable results. So, don't.)
 *
 * \param table         Hash table name.
 * \param f             XField value to write (it cannot be an XStructure!)
 *
 * \return              X_SUCCESS       if successful, or one of the errors returned by setRedisValue(), or
 *                      X_NO_INIT       if the SMA-X sharing was not initialized (via sma_x_open()).
 *                      X_HOST_INVALID  if the host (owner ID) is NULL.
 *                      X_NAME_INVALID  if the 'key' arhument is NULL.
 *                      X_SIZE_INVALID  if ndim or sizes is invalid or if the total element count exceeds
 *                                      X_MAX_ELEMENTS.
 *                      X_NULL          if the 'value' argument is NULL.
 */
int smaxWrite(const char *table, const XField *f) {
  const char *funcName = "smaxWrite()";

  int status;
  char *args[9];
  char dims[X_MAX_STRING_DIMS];
  RedisClient *cl;

  if(table == NULL) return smaxError(funcName, X_GROUP_INVALID);
  if(f->name == NULL) return smaxError(funcName, X_NAME_INVALID);
  if(f->value == NULL) return smaxError(funcName, X_NULL);
  if(f->ndim < 0 || f->ndim > X_MAX_DIMS) return smaxError(funcName, X_SIZE_INVALID);
  if(xGetFieldCount(f) <= 0) return smaxError(funcName, X_SIZE_INVALID);
  if(redis == NULL) return smaxError(funcName, X_NO_INIT);
  if(!redisxIsConnected(redis)) return smaxError(funcName, X_NO_SERVICE);
  if(HSET_WITH_META == NULL) return smaxScriptError("HSetWithMeta", X_NULL);

  // Create timestamped string values.
  if(f->type == X_STRUCT) return smaxError(funcName, X_TYPE_INVALID);

  xPrintDims(dims, f->ndim, f->sizes);

  args[0] = "EVALSHA";
  args[1] = HSET_WITH_META;
  args[2] = "1";                    // number of Redis keys sent.
  args[3] = (char *) table;         // the Redis key: hash-table name
  args[4] = smaxGetProgramID();
  args[5] = f->name;                // hash field name.
  args[6] = f->value;               // Value. If not serialized, we'll deal with it below...
  args[7] = xStringType(f->type);
  args[8] = dims;

  cl = redisxGetClient(redis, INTERACTIVE_CHANNEL);
  if(cl == NULL) return smaxError(funcName, X_NO_SERVICE);

  if(!f->isSerialized) {
    args[6] = smaxValuesToString(f->value, f->type, xGetFieldCount(f), NULL, 0);
    if(!args[6]) return smaxError(funcName, X_NULL);
  }

  status = redisxLockEnabled(cl);
  if(!status) {
    // Writes not to request reply.
    status = redisxSkipReplyAsync(cl);

    // Call script
    if(!status) {
      int L[9] = {0};
      status = redisxSendArrayRequestAsync(cl, args, L, 9);
    }

    if(status) smaxError(funcName, status);

    redisxUnlockClient(cl);
  }

  if(!f->isSerialized) if(f->type != X_RAW) free(args[6]);

  if(status) return smaxError(funcName, status);

  return X_SUCCESS;
}
/// \endcond


/**
 * Writes the structure data, recursively for nested sub-structures, into the database, by calling
 * the HMGetWithMeta for setting all fields of each component structure.
 *
 * \param cl            Redis client to use.
 * \param id            Structure id (aggregated Redis hastable name)
 * \param s             Pointer to the structure to send.
 * \param timestamp     Pointer to the timestamp to be used for all fields.
 * \param isTop         Whether this is the top-level structure being sent. External callers
 *                      should usually set this TRUE, and as the call processes nested
 *                      substructures, the recursion will make those calls with the parameter set to FALSE.
 *
 * \return      X_SUCCESS, or else an appropriate error code.
 */
static int SendStructDataAsync(RedisClient *cl, const char *id, const XStructure *s, boolean isTop) {
  const char *funcName = "xSendStructDataAsync()";

  int status = X_SUCCESS, nFields = 0, *L, n, next;
  char **args;
  XField *f;

  if(id == NULL) return smaxError(funcName, X_GROUP_INVALID);
  if(s == NULL) return smaxError(funcName, X_NULL);
  if(!redisxIsConnected(redis)) return smaxError(funcName, X_NO_SERVICE);
  if(HMSET_WITH_META == NULL) return smaxScriptError("HMSetWithMeta", X_NULL);

  for(f = s->firstField; f != NULL; f = f->next) {
    if(!xIsFieldValid(f)) continue;
    nFields++;
  }

  if(nFields == 0) return X_SUCCESS;    // Empty struct, nothing to do...

  n = 6 + nFields * HMSET_COMPONENTS;
  args = (char **) calloc(n, sizeof(char *));
  if(!args) return smaxError(funcName, X_FAILURE);

  L = (int *) calloc(n, sizeof(int));
  if(!L) {
    free(args);
    return smaxError(funcName, X_FAILURE);
  }

  args[0] = "EVALSHA";
  args[1] = HMSET_WITH_META;
  args[2] = "1";                    // number of Redis keys sent.
  args[3] = (char *) id;            // the Redis key: hash-table name
  args[4] = smaxGetProgramID();

  next = 5;

  for(f = s->firstField; f != NULL; f = f->next, next += HMSET_COMPONENTS) {
    if(!xIsFieldValid(f)) continue;

    args[next + HMSET_NAME_OFFSET] = f->name;
    args[next + HMSET_TYPE_OFFSET] = xStringType(f->type);

    if(f->type == X_STRUCT) {
      args[next + HMSET_VALUE_OFFSET] = xGetAggregateID(id, f->name);
      args[next + HMSET_DIMS_OFFSET] = "1";
      status = SendStructDataAsync(cl, args[next + HMSET_VALUE_OFFSET], (XStructure *) f->value, FALSE);
    }
    else {
      args[next + HMSET_VALUE_OFFSET] = f->isSerialized ? f->value : smaxValuesToString(f->value, f->type, xGetFieldCount(f), NULL, 0);
      args[next + HMSET_DIMS_OFFSET] = (char *) malloc(X_MAX_STRING_DIMS);
      xPrintDims(args[next + HMSET_DIMS_OFFSET], f->ndim, f->sizes);
    }
  }

  // Finally T/F to specify whether or not to notify parents
  args[next] = isTop ? "T" : "F";

  if(!status) {
    // Don't want a reply.
    status = redisxSkipReplyAsync(cl);
    if(status) smaxError(funcName, status);

    // Call script
    status = redisxSendArrayRequestAsync(cl, args, NULL, n);

    if(status) smaxError(funcName, status);
  }

  next = 5;
  for(f = s->firstField; f != NULL; f = f->next, next += HMSET_COMPONENTS) {
    if(!xIsFieldValid(f)) continue;
    if(f->type == X_STRUCT) free(args[next + HMSET_VALUE_OFFSET]);  // aggregated struct name
    else {
      free(args[next + HMSET_DIMS_OFFSET]);                         // string count
      if(!f->isSerialized) free(args[next + HMSET_VALUE_OFFSET]);   // serialized value
    }
  }

  free(args);
  free(L);

  if(status) return smaxError(funcName, status);

  return X_SUCCESS;
}


/**
 * Loads the script SHA1 ids for a given SMA-X script name, and checks to make sure
 * that the corresponding script is in fact loaded into the Redis database.
 *
 * @param name      Name of the script, e.g. "HGetWithMeta".
 * @param pSHA1     Pointer to the string, which is to contain the script SHA1 value obtained from SMA-X.
 * @return          X_SUCCESS (0) if the script SHA1 was successfully obtained and is valid (can be used),
 *                  or else
 *
 *                    X_NULL            if either argument is NULL, or if there is no script SHA available
 *                                      in Redis for the given name, or
 *                    X_NAME_INVALID    if the name is empty, or
 *                    X_NO_SERVICE      if the script with the given SHA1 id is not loaded into Redis.
 *
 * @sa InitScriptsAsync()
 */
static int InitScript(const char *name, char **pSHA1) {
  RESP *reply;
  char *sha1 = NULL;
  int status = X_SUCCESS;

  if(*pSHA1 != NULL) free(*pSHA1);
  *pSHA1 = NULL;

  sha1 = smaxGetScriptSHA1(name, &status);
  if(status) return status;
  if(!sha1) return X_NULL;

  reply = redisxRequest(redis, "SCRIPT", "EXISTS", sha1, NULL, &status);
  if(!status) status = redisxCheckRESP(reply, RESP_ARRAY, 1);
  if(!status) {
    RESP **array = (RESP **) reply->value;
    if(array[0]->n != 1) status = X_NO_SERVICE;
  }

  redisxDestroyRESP(reply);

  if(!status) *pSHA1 = sha1;

  return status;
}


/**
 * Initializes the SHA1 script IDs for the essential LUA helpers.
 */
static void InitScriptsAsync() {
  static const char *names[] = { "HSetWithMeta", "HGetWithMeta", "HMSetWithMeta", "GetStruct", NULL };
  static char **pSHA[] = {  & HSET_WITH_META,  & HGET_WITH_META,  & HMSET_WITH_META, & GET_STRUCT, NULL };

  int status = X_FAILURE;
  boolean first;

  for(first = TRUE; status != X_SUCCESS; first = FALSE) {
    int i;

    for(i = 0; names[i]; i++) {
      status = InitScript(names[i], pSHA[i]);
      if(status) break;
    }

    if(status != X_SUCCESS) {

      if(!smaxIsDisabled()) {
        if(first) fprintf(stderr, "ERROR! SMA-X: Missing LUA script(s) in Redis.\n");
        return; // if not reconnecting, give up...
      }

      if(first) fprintf(stderr, "WARNING! SMA-X: Waiting for LUA scripts to be loaded into Redis.\n");
      if(SMAX_RECONNECT_RETRY_SECONDS > 0) sleep(SMAX_RECONNECT_RETRY_SECONDS);
    }
  }
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


static void ProcessUpdateNotificationAsync(const char *pattern, const char *channel, const char *msg, int length) {
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


/**
 *
 * Process responses to pipelined HSET calls (integer RESP).
 *
 * \param reply     The RESP reply received from Redis on its pipeline channel.
 *
 */
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

