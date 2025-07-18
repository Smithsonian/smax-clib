/**
 * \file
 *
 * \date Jan 26, 2018
 * \author Attila Kovacs
 *
 * \brief
 *      SMA-X is a software implementation for SMA shared data, and is the base layer for the software
 *      reflective memory (RM) emulation, and DSM replacement.
 *      It works by communicating TCP/IP messages to a central Redis server.
 *
 *      There is also extra functionality, for configuring, performance tweaking, verbosity control,
 *      and some convenience methods (e.g. data serialization/deserialization).
 *
 */

/// For clock_gettime()
#define _POSIX_C_SOURCE 199309

#include <sys/types.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <sys/utsname.h>

#include "smax-private.h"
#include "procname.h"



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

/// \cond PRIVATE
// 'private' prototypes ------------->
void smaxInitNotify();
/// \endcond

// Local prototypes ------------------->
static int ProcessStructRead(RESP **component, PullRequest *req);
static int ParseStructData(XStructure *s, RESP *names, RESP *data, XMeta *meta);

static int SendStructDataAsync(RedisClient *cl, const char *id, const XStructure *s, boolean isTop);

static void InitScriptsAsync();

static boolean usePipeline = TRUE;
static int tcpBufSize = REDISX_TCP_BUF_SIZE;

static char *server;
static int serverPort = REDISX_TCP_PORT;
static RedisServer *sentinel;
static int nSentinel;

static char *user;
static char *auth;
static int dbIndex;

static Redis *redis;

static char *hostName;
static char *programID;

/**
 * Configures the SMA-X server before connecting.
 *
 * @param host    The SMA-X REdis server host name or IP address.
 * @param port    The Redis port number on the SMA-X server, or &lt=0 to use the default
 * @return        X_SUCCESS (0) if successful, or X_ALREADY_OPEN if cannot alter the server configuration
 *                because we are already in a connected state.
 *
 * @sa smaxSetAuth()
 * @sa smaxSetDB()
 * @sa smaxConnect()
 */
int smaxSetServer(const char *host, int port) {
  smaxLockConfig();

  if(smaxIsConnected()) {
    smaxUnlockConfig();
    return x_error(X_ALREADY_OPEN, EALREADY, "smaxSetServer", "already in connected state");
  }

  if(server) free(server);
  server = xStringCopyOf(host);
  serverPort = port > 0 ? port : REDISX_TCP_PORT;

  smaxUnlockConfig();
  return X_SUCCESS;
}

/**
 * Configure SMA-X to use a high availability Redis Sentinel configuration
 *
 * @param servers     An array of known Sentinel servers
 * @param nServers    The number of servers in the array
 * @return            X_SUCCESS (0) if successful or else an error code &lt;0.
 *
 * @sa smaxConnect()
 */
int smaxSetSentinel(const RedisServer *servers, int nServers) {
  static const char *fn = "smaxSetSentinel";

  prop_error(fn, redisxValidateSentinel(SMAX_SENTINEL_SERVICENAME, servers, nServers));

  sentinel = (RedisServer *) calloc(nServers, sizeof(RedisServer));
  if(!sentinel) return x_error(X_FAILURE, errno, fn, "alloc error (%d RedisServer)", nServers);

  memcpy(sentinel, servers, nServers * sizeof(RedisServer));
  nSentinel = nServers;

  return X_SUCCESS;
}

/**
 * Sets the SMA-X database authentication parameters (if any) before connecting to the SMA-X server.
 *
 * @param username    Redis ACL user name (if any), or NULL for no user-based authentication
 * @param password    Redis database password (if any), or NULL if the database is not password protected
 * @return            X_SUCCESS (0) if successful, or X_ALREADY_OPEN if cannot alter the server configuration
 *                    because we are already in a connected state.
 *
 * @sa smaxSetServer()
 * @sa smaxConnect()
 */
int smaxSetAuth(const char *username, const char *password) {
  smaxLockConfig();

  if(smaxIsConnected()) {
    smaxUnlockConfig();
    return x_error(X_ALREADY_OPEN, EALREADY, "smaxSetAuth", "already in connected state");
  }

  if(user) free(user);
  user = xStringCopyOf(username);

  if(auth) free(auth);
  auth = xStringCopyOf(password);

  smaxUnlockConfig();
  return X_SUCCESS;
}

/**
 * Sets a non-default Redis database index to use for SMA-X before connecting to the SMA-X server.
 *
 * @param idx         The Redis database index to use (if not the default one)
 * @return            X_SUCCESS (0) if successful, or X_ALREADY_OPEN if cannot alter the server configuration
 *                    because we are already in a connected state.
 *
 * @sa smaxSetServer()
 * @sa smaxConnect()
 */
int smaxSetDB(int idx) {
  smaxLockConfig();

  if(smaxIsConnected()) {
    smaxUnlockConfig();
    return x_error(X_ALREADY_OPEN, EALREADY, "smaxSetDB", "already in connected state");
  }

  dbIndex = idx > 0 ? idx : 0;

  smaxUnlockConfig();
  return X_SUCCESS;
}

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
 * Enable or disable pipelined write operations (enabled by default). When pipelining, share calls
 * will return as soon as the request is sent to the Redis server, without waiting for a response.
 * Instead, responses are consumed asynchronously by a dedicated thread, which will report
 * errors to stderr. Pipelined writes can have a significant performance advantage over
 * handshaking at the cost of one extra socket connection to Redis (dedicated to pipelining)
 * and the extra thread consuming responses.
 *
 * The default state of pipelined writes might vary by platform (e.g. enabled on Linux,
 * disabled on LynxOS).
 *
 * __IMPORTANT__: calls to smaxSetPipelined() must precede the call to smaxConnect().
 *
 * @param isEnabled     TRUE to enable pipelined writes, FALSE to disable (default is enabled).
 *
 * @return            X_SUCCESS (0) if successful, or X_ALREADY_OPEN if cannot alter the server configuration
 *                    because we are already in a connected state.
 *
 * @sa smaxIsPipelined()
 * @sa smaxSetPipelineConsumer()
 */
int smaxSetPipelined(boolean isEnabled) {
  if(usePipeline == isEnabled) return X_SUCCESS;

  smaxLockConfig();

  if(smaxIsConnected()) {
    smaxUnlockConfig();
    return x_error(X_ALREADY_OPEN, EALREADY, "smaxSetPipelined", "Cannot change pipeline state after connecting");
  }

  usePipeline = isEnabled;
  smaxUnlockConfig();

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
 * @sa smaxConnect;
 */
int smaxSetTcpBuf(int size) {
  smaxLockConfig();

  if(smaxIsConnected()) {
    smaxUnlockConfig();
    return x_error(X_ALREADY_OPEN, EALREADY, "smaxSetTcpBuf", "Cannot change pipeline state after connecting");
  }

  tcpBufSize = size;
  smaxUnlockConfig();

  return X_SUCCESS;
}

/**
 * Returns the host name on which this program is running. It returns a reference to the same
 * static variable every time. As such you should never call free() on the returned value.
 * Note, that only the leading part of the host name is returned, so for a host
 * that is registered as 'somenode.somedomain' only 'somenode' is returned.
 *
 * \return      The host name string (leading part only).
 *
 * \sa smaxSetHostName()
 *
 */
char *smaxGetHostName() {
  if(hostName == NULL) {
    struct utsname u;
    int i;
    uname(&u);

    // Keep only the leading part only...
    for(i=0; u.nodename[i]; i++) if(u.nodename[i] == '.') {
      u.nodename[i] = '\0';
      break;
    }

    hostName = xStringCopyOf(u.nodename);
  }
  return hostName;
}

/**
 * Changes the host name to the user-specified value instead of the default (leading component
 * of the value returned by gethostname()). Subsequent calls to smaxGetHostName() will return
 * the newly set value. An argument of NULL resets to the default.
 *
 * @param name      the host name to use, or NULL to revert to the default (leading component
 *                  of gethostname()).
 *
 * @sa smaxGetHostName()
 */
void smaxSetHostName(const char *name) {
  char *oldName = hostName;
  hostName = xStringCopyOf(name);
  if(oldName) free(oldName);
}

/**
 * Returns the SMA-X program ID.
 *
 * \return      The SMA-X program ID as &lt;hostname&gt;:&lt;programname&gt;, e.g. "hal9000:statusServer".
 *
 */
char *smaxGetProgramID() {
#if (__Lynx__ && __powerpc__)
  char procName[40];
#else
  const char *procName = DEFAULT_PROCESS_NAME;
  extern char *__progname;
#endif

  const char *host;

  if(programID) return programID;

#if (__Lynx__ && __powerpc__)
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
  return redis && redisxIsConnected(redis);
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
  static const char *fn = "smaxConnectTo";

  prop_error(fn, smaxSetServer(server, -1));
  prop_error(fn, smaxConnect());

  return X_SUCCESS;
}

/**
 * Initializes the SMA-X sharing library in this runtime instance, and connects to the SMA-X server.
 * If the server was not explicitly configure previously it will try the default server
 * (SMAX_DEFAULT_HOSTNAME = "smax"), or else localhost (127.0.0.1).
 *
 *
 * \return      X_SUCCESS           If the library was successfully initialized
 *              X_ALREADY_OPEN      If SMA-X sharing was already open.
 *              X_NO_SERVICE        If the there was an issue establishing the necessary network connection(s).
 *              X_NAME_INVALID      If the redis server name lookup failed.
 *              X_NULL              If the Redis IP address is NULL
 *
 * @sa smaxSetServer()
 * @sa smaxSetSentinel()
 * @sa smaxSetAuth()
 * @sa smaxConnectTo()
 * @sa smaxDisconnect()
 * @sa smaxReconnect()
 * @sa smaxIsConnected()
 */
int smaxConnect() {
  static const char *fn = "smaxConnect";

  int status;

  smaxLockConfig();

  if(smaxIsConnected()) {
    smaxUnlockConfig();
    return X_SUCCESS;
  }

  // START one-time-only initialization ------>
  if(!redis) {
    xvprintf("SMA-X> Initializing...\n");

    smaxGetProgramID();
    xvprintf("SMA-X> program ID: %s\n", programID);

    if(!server) {
      server = getenv("SMAX_HOST");
      if(server) xvprintf("SMA-X> server from SMAX_HOST: %s\n", server);
    }

    if(sentinel) redis = redisxInitSentinel(SMAX_SENTINEL_SERVICENAME, sentinel, nSentinel);
    else redis = redisxInit(server ? server : SMAX_DEFAULT_HOSTNAME);

    if(redis == NULL) {
      smaxUnlockConfig();
      return x_trace(fn, NULL, X_NO_INIT);
    }

    // Configuration...
    if(!sentinel) redisxSetPort(redis, serverPort);

    redisxSetTcpBuf(redis, tcpBufSize);

    if(user) redisxSetUser(redis, user);
    if(auth) redisxSetPassword(redis, auth);
    if(dbIndex) redisxSelectDB(redis, dbIndex);

    status = smaxConfigTLSAsync(redis);
    if(status) {
      smaxUnlockConfig();
      return x_trace(fn, NULL, status);
    }

    redisxSetSocketErrorHandler(redis, smaxSocketErrorHandler);

    smaxSetPipelineConsumer(smaxProcessPipedWritesAsync);
    smaxInitNotify();
  }
  // END one-time-only initialization <--------

  xvprintf("SMA-X> Connecting...\n");

  // Reset LUA script hashes after connecting to Redis.
  smaxAddConnectHook(InitScriptsAsync);

  // Flush lazy cache after disconnecting from Redis.
  smaxAddDisconnectHook((void (*)) smaxLazyFlush);

  // Release pending waits if disconnected
  smaxAddDisconnectHook((void (*)) smaxReleaseWaits);

  status = redisxConnect(redis, usePipeline);

  // If failed on default host, then try localhost...
  if(status && !server) {
    xvprintf("Trying localhost...\n");
    redisxSetHostname(redis, "127.0.0.1");
    status = redisxConnect(redis, usePipeline);
  }

  if(status) {
    smaxUnlockConfig();
    return x_trace(fn, NULL, status);
  }

  // By default, we'll try to reconnect to Redis if the connection is severed.
  smaxSetResilient(TRUE);

  smaxUnlockConfig();

  xvprintf("SMA-X> opened & ready.\n");

  return X_SUCCESS;
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
  if(!smaxIsConnected()) return x_error(X_NO_INIT, ENOTCONN, "smaxDisconnect", "not connected");

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
  if(redis == NULL) return x_error(X_NO_INIT, ENOTCONN, "smaxReconnect", "not connected");

  xvprintf("SMA-X> reconnecting.\n");

  while(redisxReconnect(redis, usePipeline) != X_SUCCESS) if(SMAX_RECONNECT_RETRY_SECONDS > 0)
    sleep(SMAX_RECONNECT_RETRY_SECONDS);

  return X_SUCCESS;
}

/**
 * Resets the Redis server for SMA-X. SMA-X must be disconnected when this function is called,
 * or else it will return an error. Resetting SMA-X allows to change configuration settings
 * before the next connection.
 *
 * @return    X_SUCCESS (0) if successful or else X_ALREADY_OPEN if we are currently connected
 *            to SMA-X.
 *
 * @sa smaxConnect()
 */
int smaxReset() {
  smaxLockConfig();
  if(smaxIsConnected()) {
    smaxUnlockConfig();
    return x_error(X_ALREADY_OPEN, EBUSY, "smaxReset", "cannot reset while connected");
  }

  redisxDestroy(redis);
  redis = NULL;

  smaxUnlockConfig();

  return X_SUCCESS;
}

/**
 * Add a callback function for when SMA-X is connected. It's a wrapper to redisxAddConnectHook().
 *
 * @param setupCall     Callback function
 * @return              X_SUCCESS (0) or an error code (&lt;0) from redisxAddConnectHook().
 *
 * @sa smaxRemoveConnectHook()
 * @sa smaxConnect()
 * @sa smaxConnectTo()
 */
int smaxAddConnectHook(void (*setupCall)(void)) {
  static const char *fn = "smaxAddConnectHook";
  Redis *r = smaxGetRedis();
  if(!r) return smaxError(fn, X_NO_INIT);
  prop_error(fn, redisxAddConnectHook(r, (void (*)(Redis *)) setupCall));
  return X_SUCCESS;
}

/**
 * Remove a post-connection callback function. It's a wrapper to redisxRemoveConnectHook().
 *
 * @param setupCall     Callback function
 * @return              X_SUCCESS (0) or an error code (&lt;0) from redisxAddConnectHook().
 *
 * @sa smaxAddConnectHook()
 * @sa smaxConnect()
 * @sa smaxConnectTo()
 */
int smaxRemoveConnectHook(void (*setupCall)(void)) {
  static const char *fn = "smaxRemoveConnectHook";
  Redis *r = smaxGetRedis();
  if(!r) return smaxError(fn, X_NO_INIT);
  prop_error(fn, redisxRemoveConnectHook(r, (void (*)(Redis *)) setupCall));
  return X_SUCCESS;
}

/**
 * Add a callback function for when SMA-X is disconnected. It's a wrapper to redisxAddDisconnectHook().
 *
 * @param cleanupCall   Callback function
 * @return              X_SUCCESS (0) or an error code (&lt;0) from redisxAddConnectHook().
 *
 * @sa smaxRemoveDisconnectHook()
 * @sa smaxDisconnect()
 */
int smaxAddDisconnectHook(void (*cleanupCall)(void)) {
  static const char *fn = "smaxAddDisconnectHook";
  Redis *r = smaxGetRedis();
  if(!r) return smaxError(fn, X_NO_INIT);
  prop_error(fn, redisxAddDisconnectHook(r, (void (*)(Redis *)) cleanupCall));
  return X_SUCCESS;
}

/**
 * Remove a post-cdisconnect callback function. It's a wrapper to redisxRemiveDisconnectHook().
 *
 * @param cleanupCall   Callback function
 * @return              X_SUCCESS (0) or an error code (&lt;0) from redisxAddConnectHook().
 *
 * @sa smaxAddDisconnectHook()
 * @sa smaxDisconnect()
 */
int smaxRemoveDisconnectHook(void (*cleanupCall)(void)) {
  static const char *fn = "smaxRemoveDisconnectHook";
  Redis *r = smaxGetRedis();
  if(!r) return smaxError(fn, X_NO_INIT);
  prop_error(fn, redisxRemoveDisconnectHook(r, (void (*)(Redis *)) cleanupCall));
  return X_SUCCESS;
}

/**
 * Change the pipeline response consumer function (from it's default or other previous consumer). It is a wrapper
 * for redisxSetPipelineConsumer().
 *
 * @param f     The function to process ALL pipeline responses from Redis.
 * @return      X_SUCCESS (0) if successful, or else an error by redisxSetPipelineConsumer()
 *
 * @sa smaxSetPipelined()
 * @sa smaxIsPipelined()
 */
int smaxSetPipelineConsumer(void (*f)(RESP *)) {
  static const char *fn = "smaxSetPipelineConsumer";
  Redis *r = smaxGetRedis();

  if(!r) return smaxError(fn, X_NO_INIT);

  prop_error(fn, redisxSetPipelineConsumer(r, f));
  return X_SUCCESS;
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
 *                  X_GROUP_INVALID     if the 'table' argument is invalid.
 *                  X_NAME_INVALID      if the 'key' argument is invalid.
 *                  X_NULL              if an essential argument is NULL or contains NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 *
 * @sa smaxLazyPull()
 * @sa smaxQueue()
 */
int smaxPull(const char *table, const char *key, XType type, int count, void *value, XMeta *meta) {
  static const char *fn = "smaxPull";

  PullRequest *data;
  char *id = NULL;
  int status = X_SUCCESS;

  if(type == X_FIELD) {
    XField *f;

    if(!value) return x_error(X_NULL, EINVAL, fn, "output value pointer is NULL");

    id = xGetAggregateID(table, key);
    if(!id) return x_trace(fn, NULL, X_NULL);

    f = smaxPullField(id, meta, &status);
    if(f) {
      *(XField *) value = *f;
      free(f);
    }

    prop_error(fn, status);
    return X_SUCCESS;
  }
  else if(type == X_STRUCT) {
    id = xGetAggregateID(table, key);
    if(!id) return x_trace(fn, NULL, X_NULL);
  }

  data = (PullRequest *) calloc(1, sizeof(PullRequest));
  x_check_alloc(data);

  // Make sure structures are retrieved all the same no matter how their names are split
  // into group + key.
  if(type == X_STRUCT) {
    data->group = id;
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

  status = smaxRead(data, REDISX_INTERACTIVE_CHANNEL);
  //if(status) smaxZero(value, type, count);

  smaxDestroyPullRequest(data);

  prop_error(fn, status);

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
 *                  X_GROUP_INVALID     if the table name is invalid.
 *                  X_NAME_INVALID      if the 'key' argument is invalid.
 *                  X_SIZE_INVALID      if count < 1 or count > X_MAX_ELEMENTS
 *                  X_NULL        if the 'value' argument is NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 *
 * \sa smaxShareArray()
 * \sa smaxShareField()
 * \sa smaxShareStruct()
 */
int smaxShare(const char *table, const char *key, const void *value, XType type, int count) {
  prop_error("smaxShare", smaxShareArray(table, key, value, type, 1, &count));
  return X_SUCCESS;
}

/**
 * Share a multidimensional array, such as an `int[][][]`, or `float[][]`, in a single atomic
 * transaction.
 *
 * \param table     Hash table in which to write entry.
 * \param key       Variable name under which the data is stored.
 * \param ptr       Pointer to the data buffer, such as an `int[][][]` or `float[][]`.
 * \param type      SMA-X variable type, e.g. X_FLOAT or X_CHARS(40), of the buffer.
 * \param ndim      Dimensionality of the data (0 <= `ndim` <= X_MAX_DIMS).
 * \param sizes     An array of ints containing the sizes along each dimension.
 *
 * \return          X_SUCCESS (0)       if successful, or
 *                  X_NO_INIT           if the SMA-X library was not initialized.
 *                  X_GROUP_INVALID     if the table name is invalid.
 *                  X_NAME_INVALID      if the 'key' argument is invalid.
 *                  X_SIZE_INVALID      if ndim or sizes are invalid.
 *                  X_NULL              if the 'value' argument is NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 *
 * @sa smaxShare()
 */
int smaxShareArray(const char *table, const char *key, const void *ptr, XType type, int ndim, const int *sizes) {
  static const char *fn = "smaxShareArray";

  XField f = X_FIELD_INIT;
  char trybuf[REDISX_CMDBUF_SIZE];
  int count, status;

  count = xGetElementCount(ndim, sizes);
  prop_error(fn, count);

  if(count < 1 || count > X_MAX_ELEMENTS) return x_error(X_SIZE_INVALID, EINVAL, fn, "invalid element count: %d", count);

  f.value = (type == X_STRUCT) ?
          (void *) ptr : smaxValuesToString(ptr, type, count, trybuf, REDISX_CMDBUF_SIZE);

  if(f.value == NULL) return x_trace(fn, NULL, X_NULL);

  f.isSerialized = TRUE;
  f.name = (char *) key;
  f.type = type;
  f.ndim = ndim;
  memcpy(f.sizes, sizes, ndim * sizeof(int));

  status = smaxShareField(table, &f);

  if(f.value != trybuf) if(type != X_RAW && type != X_STRUCT) free(f.value);

  prop_error(fn, status);

  return X_SUCCESS;
}

/**
 * Share a field object, which may contain any SMA-X data type.
 *
 * \param table     Hash table in which to write entry.
 * \param f         Pointer for XField holding the data to share.
 *
 * \return          X_SUCCESS (0)       if successful, or
 *                  X_NO_INIT           if the SMA-X library was not initialized.
 *                  X_GROUP_INVALID     if the table name is invalid.
 *                  X_NAME_INVALID      if the 'key' argument is invalid.
 *                  X_SIZE_INVALID      if ndim or sizes are invalid.
 *                  X_NULL              if the 'value' argument is NULL.
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
  static const char *fn = "smaxShareField";

  int status;

  if(f->type == X_STRUCT) {
    char *id = xGetAggregateID(table, f->name);
    status = smaxShareStruct(id, (XStructure *) f->value);
    if(id != NULL) free(id);
    return x_trace(fn, NULL, status);
  }

  status = smaxWrite(table, f);
  if(status) {
    if(status == X_NO_SERVICE) status = smaxStorePush(table, f);
    return x_trace(fn, NULL, status);
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
 *                  X_GROUP_INVALID     if the table name is invalid.
 *                  X_NAME_INVALID      if the 'key' argument is NULL.
 *                  X_NULL              if the 'value' argument is NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 * \sa smaxShare()
 * \sa smaxShareField()
 * \sa xCreateStruct()
 */
static int SendStruct(const char *id, const XStructure *s) {
  static const char *fn = "SendStruct";

  Redis *r = smaxGetRedis();
  RedisClient *cl;
  int status;

  if(!r) return smaxError(fn, X_NO_INIT);

  cl = r->interactive;

  status = redisxLockConnected(cl);
  if(!status) {
    // TODO the following should be done atomically, but multi/exec blocks don't work
    // with evalsha(?)...

    // Send the structure data, recursively
    status = SendStructDataAsync(cl, id, s, TRUE);
    redisxUnlockClient(cl);
  }

  prop_error(fn, status);

  return X_SUCCESS;
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
 *                  X_GROUP_INVALID     if the table name is invalid.
 *                  X_NAME_INVALID      if the 'key' argument is invalid.
 *                  X_NULL              if the 'value' argument is NULL.
 *                  X_NO_SERVICE        if there was no connection to the Redis server.
 *                  X_FAILURE           if there was an underlying failure.
 * \sa smaxShare()
 * \sa smaxShareField()
 * \sa xCreateStruct()
 */
int smaxShareStruct(const char *id, const XStructure *s) {
  static const char *fn = "smaxShareStruct";

  int status = SendStruct(id, s);

  if(status == X_NO_SERVICE) {
    XField *f = smaxCreateField(id, X_STRUCT, 0, NULL, s);

    if(f) {
      status = smaxStorePush(NULL, f);
      free(f);
    }
  }

  prop_error(fn, status);

  return X_SUCCESS;
}

/**
 * Retrieve the current number of variables stored on host (or owner ID).
 *
 * \param table    Hash table name.
 *
 * \return         The number of keys (fields) in the specified table (>= 0), or an error code (<0), such as:
 *                 X_NO_INIT           if the SMA-X sharing was not initialized, e.g. via smaConnect().
 *                 X_GROUP_INVALID     if the table name is invalid.
 *                 or one of the errors (&lt;0) returned by redisxRequest().
 *
 * @sa smaxGetKeys()
 */
int smaxKeyCount(const char *table) {
  static const char *fn = "smaxKeyCount";
  Redis *r = smaxGetRedis();
  RESP *reply;
  int status;

  if(table == NULL) return x_error(X_GROUP_INVALID, EINVAL, fn, "table is NULL");
  if(!table[0]) return x_error(X_GROUP_INVALID, EINVAL, fn, "table is empty");
  if(!r) return smaxError(fn, X_NO_INIT);

  reply = redisxRequest(r, "HLEN", table, NULL, NULL, &status);
  if(status) return x_trace(fn, NULL, status);

  status = redisxCheckRESP(reply, RESP_INT, 0);
  if(!status) status = reply->n;
  redisxDestroyRESP(reply);

  prop_error(fn, status);

  xvprintf("SMA-X> Get number of variables: %d.\n", status);
  return status;
}

/**
 * Returns a snapshot of the key names stored in a given Redis hash table, ot NULL if there
 * was an error.
 *
 * \param table           Host name or owner ID whose variable to count.
 * \param[out]  n         Pointer to which the number of keys (>=0) or an error (<0) is returned.
 *                        An error returned by redisxGetKeys(), or else:
 *
 *                          X_NO_INIT           if the SMA-X sharing was not initialized, e.g. via smaxConnect().
 *                          X_GROUP_INVALID     if the table name is invalid.
 *                          X_NULL              if the output 'n' pointer is NULL.
 *
 * \return          An array of pointers to the names of Redis keys.
 *
 * @sa smaxKeyCount()
 */
char **smaxGetKeys(const char *table, int *n) {
  static const char *fn = "smaxGetKeys";

  Redis *r = smaxGetRedis();
  char **keys;

  if(n == NULL) {
    x_error(0, EINVAL, fn, "parameter 'n' is NULL");
    return NULL;
  }

  if(!r) {
    smaxError(fn, X_NO_INIT);
    return NULL;
  }

  xvprintf("SMA-X> get variable names.\n");

  keys = redisxGetKeys(r, table, n);

  if(*n > 0) return keys;

  // CLEANUP --- There was an error.
  if(keys) {
    int i;
    for(i=*n; --i >= 0; ) if(keys[i]) free(keys[i]);
    free(keys);
  }

  if(*n < 0) x_trace_null(fn, NULL);

  *n = 0;

  return NULL;
}

/**
 * \cond PROTECTED
 *
 * Retrieves data from the SMA-X database, interactively or as a pipelined request.
 *
 * \param[in,out]   req           Pull request
 * \param[in]       channel       REDISX_INTERACTIVE_CHANNEL or REDISX_PIPELINE_CHANNEL
 *
 * \return              X_SUCCESS (0)       if successful, or
 *                      X_NULL              if the request or its value field is NULL
 *                      X_NO_INIT           if the SMA-X library was not initialized.
 *                      X_GROUP_INVALID     if the table name is invalid.
 *                      X_NAME_INVALID      if the 'key argument is invalid.
 *                      X_NO_SERVICE        if there was no connection to the Redis server.
 *                      X_TIMEDOUT          if timed out waiting for a response
 *                      X_FAILURE           if there was an underlying failure.
 */
int smaxRead(PullRequest *req, int channel) {
  static const char *fn = "smaxRead";

  const char *args[5], *script = NULL;
  Redis *r = smaxGetRedis();
  RESP *reply = NULL;
  RedisClient *cl;
  int status, n = 0;

  if(req == NULL) return x_error(X_NULL, EINVAL, fn, "'req' is NULL");
  if(req->group == NULL) return x_error(X_GROUP_INVALID, EINVAL, fn, "req->group is NULL");
  if(!req->group[0]) return x_error(X_GROUP_INVALID, EINVAL, fn, "req->group is empty");
  if(req->value == NULL) return x_error(X_NULL, EINVAL, fn, "req->value is NULL");
  if(req->type != X_STRUCT) {
    if(req->key == NULL) return x_error(X_NAME_INVALID, EINVAL, fn, "req->group is NULL");
    if(!req->key[0]) return x_error(X_NAME_INVALID, EINVAL, fn, "req->group is empty");
  }
  if(!r) return smaxError(fn, X_NO_INIT);

  xvprintf("SMA-X> read %s:%s.\n", (req->group ? req->group : ""), (req->key ? req->key : ""));

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

  cl = redisxGetLockedConnectedClient(r, channel);
  if(cl == NULL) return x_trace(fn, NULL, X_NO_SERVICE);

  // Call script
  status = redisxSendArrayRequestAsync(cl, args, NULL, n);

  if(channel != REDISX_PIPELINE_CHANNEL) if(!status) reply = redisxReadReplyAsync(cl, &status);

  redisxUnlockClient(cl);

  // Process reply as needed...
  if(!status && reply) {
    // Process the value
    status = smaxProcessReadResponse(reply, req);
    redisxDestroyRESP(reply);
  }

  prop_error(fn, status);

  return X_SUCCESS;
}
/// \endcond

/**
 * Private error handling for xProcessReadResponse(). Not used otherwise.
 *
 */
static int RequestError(const PullRequest *req, int status) {
  if(req->meta) req->meta->status = status;
  return status;
}

/**
 * \cond PROTECTED
 *
 * Converts a string data, from a Redis response, to binary values for the given variable.
 *
 * \param[in]       reply         String content of the Redis response to a read.
 * \param[in,out]   req           Pointer to a PullRequest structure to be completed with the data.
 *
 * \return              X_SUCCESS (0) if successful, or else an appropriate error code (&lt;0)
 *                      such as expected for smax_pull().
 *
 */
int smaxProcessReadResponse(RESP *reply, PullRequest *req) {
  static const char *fn = "smaxProcessReadResponse";

  int status = X_SUCCESS;
  RESP *data = NULL;

  if(reply == NULL) return x_error(RequestError(req, REDIS_NULL), ENOENT, fn, "Redis NULL response");
  if(req == NULL) return x_error(X_NULL, EINVAL, fn, "'req' is NULL");

  // Clear metadata if requested.
  if(req->meta != NULL) smaxResetMeta(req->meta);

  // Safety pin for X_RAW / X_STRUCT processing...
  if(req->type == X_RAW || req->type == X_STRUCT) req->count = 1;

  // Check that request is not crazy.
  if(req->count <= 0) return x_error(RequestError(req, X_SIZE_INVALID), ERANGE, fn, "invalid req->count: %d", req->count);

  // (nil)
  if(reply->n < 0) {
    // (can be used to check if key existed...)
    xZero(req->value, req->type, req->count);
    return X_SUCCESS;
  }

  // If we had a NULL value without n < 0, then it's an error.
  if(!req->value) return x_error(RequestError(req, X_NULL), ENOENT, fn, "unexpected NULL value");
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
      if(reply->n > 1) m->storeType = smaxTypeForString((char *) component[1]->value);
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
      smaxUnpackStrings((char *) data->value, data->n, req->count, (char **) req->value);
    }
    else {
      // Unpack fixed-sized types.
      int parsed;
      status = smaxStringToValues((char *) data->value, req->value, req->type, req->count, &parsed);
    }
  }

  if(reply->type == RESP_ERROR) if(strstr("NOSCRIPT", (char *) reply->value)) return smaxScriptError("smaxProcessReadResponse()", X_NULL);

  prop_error(fn, RequestError(req, status));

  // Keeps errors only, not the number of elements parsed.
  return X_SUCCESS;
}
/// \endcond

static int ProcessStructRead(RESP **component, PullRequest *req) {
  static const char *fn = "xProcessStructRead";

  XStructure *base = (XStructure *) req->value;
  XStructure **s;
  XMeta *metas = NULL;
  char **names;
  int i, nStructs, status = X_SUCCESS;

  nStructs = component[0]->n;

  if(nStructs <= 0) return x_error(X_STRUCT_INVALID, EINVAL, fn, "invalid number of structures: %d", nStructs);

  // Allocate temporary storage
  names = (char **) calloc(nStructs, sizeof(char *));
  if(!names) return x_error(X_NULL, errno, fn, "calloc() error (%d char *)", nStructs);

  metas = (XMeta *) calloc(nStructs, sizeof(XMeta));
  if(!metas) {
    free(names);
    return x_error(X_NULL, errno, fn, "calloc() error (%d char *)", nStructs);
  }

  if(req->meta != NULL) {
    smaxResetMeta(req->meta);
    req->meta->storeType = X_STRUCT;
    req->meta->storeDim = 1;
    req->meta->storeSizes[0] = 1;
  }

  // Parse all structure data (for embedded structures)
  s = (XStructure **) calloc(nStructs, sizeof(XStructure *));
  if(!s) {
    free(metas);
    free(names);
    return x_error(X_NULL, errno, fn, "calloc() error (%d XStructure)", nStructs);
  }

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

  prop_error(fn, status);

  return X_SUCCESS;
}

static int ParseStructData(XStructure *s, RESP *names, RESP *data, XMeta *meta) {
  static const char *fn = "xParseStructData";

  RESP **component, **keys, **values, **types, **dims, **timestamps, **origins, **serials;
  int i;

  if(names == NULL) return x_error(X_NULL, EINVAL, fn, "RESP names is NULL");
  if(data == NULL) return x_error(X_NULL, EINVAL, fn, "RESP data is NULL");
  if(names->type != RESP_ARRAY) return x_error(REDIS_UNEXPECTED_RESP, EINVAL, fn, "RESP names is not an array: '%c'", names->type);
  if(data->type != RESP_ARRAY) return x_error(REDIS_UNEXPECTED_RESP, EINVAL, fn, "RESP data is not an array: '%c'", data->type);
  if(data->n != HMGET_COMPONENTS) return x_error(X_NOT_ENOUGH_TOKENS, ERANGE, fn, "RESP data size: expected %d, got %d", HMGET_COMPONENTS, data->n);

  component = (RESP **) data->value;

  for(i=HMGET_COMPONENTS; --i >= 0; ) {
    if(component[i]->type != RESP_ARRAY) return x_error(REDIS_UNEXPECTED_RESP, EINVAL, fn, "RESP component[%d] is not an array: '%c'", i, component[i]->type);
    if(component[i]->n != names->n) return x_error(X_NOT_ENOUGH_TOKENS, ERANGE, fn, "RESP component[%d] wrong size: expected %d, got %d", i, names->n, component[i]->n);
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
    x_check_alloc(f);

    f->isSerialized = TRUE;
    f->name = keys[i]->value;
    keys[i]->value = NULL;      // Dereference the RESP field name so it does not get destroyed with RESP.

    f->value = (char *) values[i]->value;
    values[i]->value = NULL;    // Dereference the RESP data so it does not get destroyed with RESP.

    f->type = smaxTypeForString((char *) types[i]->value);
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
 * \cond PROTECTED
 *
 * Sends a write request to Redis over the specified communication channel. The caller
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
 *                      X_NO_INIT       if the SMA-X sharing was not initialized (via smax_x_open()).
 *                      X_GROUP_INVALID if the table name is invalid.
 *                      X_NAME_INVALID  if the field's name is invalid.
 *                      X_SIZE_INVALID  if ndim or sizes is invalid or if the total element count exceeds
 *                                      X_MAX_ELEMENTS.
 *                      X_NULL          if the 'value' argument is NULL.
 */
int smaxWrite(const char *table, const XField *f) {
  static const char *fn = "smaxWrite";

  int status;
  char *args[9];
  char dims[X_MAX_STRING_DIMS];
  Redis *r = smaxGetRedis();
  RedisClient *cl;

  if(table == NULL) return x_error(X_GROUP_INVALID, EINVAL, fn, "table is NULL");
  if(!table[0]) return x_error(X_GROUP_INVALID, EINVAL, fn, "table is empty");
  if(f->name == NULL) return x_error(X_NAME_INVALID, EINVAL, fn, "field->name is NULL");
  if(!f->name[0]) return x_error(X_NAME_INVALID, EINVAL, fn, "field->name is empty");
  if(f->value == NULL) return x_error(X_NAME_INVALID, EINVAL, fn, "field->value is NULL");
  if(HSET_WITH_META == NULL) return smaxScriptError("HSetWithMeta", X_NULL);

  // Create timestamped string values.
  if(f->type == X_STRUCT) return x_error(X_TYPE_INVALID, EINVAL, fn, "structures not supported");
  if(!r) return smaxError(fn, X_NO_INIT);

  xPrintDims(dims, f->ndim, f->sizes);

  args[0] = "EVALSHA";
  args[1] = HSET_WITH_META;
  args[2] = "1";                    // number of Redis keys sent.
  args[3] = (char *) table;         // the Redis key: hash-table name
  args[4] = smaxGetProgramID();
  args[5] = f->name;                // hash field name.
  args[6] = f->value;               // Value. If not serialized, we'll deal with it below...
  args[7] = smaxStringType(f->type);
  args[8] = dims;

  if(!f->isSerialized) {
    int count = xGetFieldCount(f);
    prop_error(fn, count);

    args[6] = smaxValuesToString(f->value, f->type, xGetFieldCount(f), NULL, 0);
    if(!args[6]) return x_trace(fn, NULL, X_NULL);
  }

  cl = redisxGetLockedConnectedClient(r, REDISX_INTERACTIVE_CHANNEL);
  if(cl == NULL) {
    if(!f->isSerialized) if(f->type != X_RAW) free(args[6]);
    return x_trace(fn, NULL, X_NO_SERVICE);
  }

  // Writes not to request reply.
  status = redisxSkipReplyAsync(cl);
  if(!status) {
    // Call script
    status = redisxSendArrayRequestAsync(cl, (const char **) args, NULL, 9);
  }

  redisxUnlockClient(cl);

  if(!f->isSerialized) if(f->type != X_RAW) free(args[6]);

  prop_error(fn, status);

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
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0).
 */
static int SendStructDataAsync(RedisClient *cl, const char *id, const XStructure *s, boolean isTop) {
  static const char *fn = "xSendStructDataAsync";

  int status = X_SUCCESS, nFields = 0, *L, n, next;
  char **args;
  XField *f;

  if(id == NULL) return x_error(X_GROUP_INVALID, EINVAL, fn, "'id' is NULL");
  if(!id[0]) return x_error(X_GROUP_INVALID, EINVAL, fn, "'id' is empty");
  if(s == NULL) return x_error(X_NULL, EINVAL, fn, "input structure is NULL");
  if(HMSET_WITH_META == NULL) return smaxScriptError("HMSetWithMeta", X_NULL);

  for(f = s->firstField; f != NULL; f = f->next) {
    if(!xIsFieldValid(f)) continue;
    nFields++;
  }

  if(nFields == 0) return X_SUCCESS;    // Empty struct, nothing to do...

  n = 6 + nFields * HMSET_COMPONENTS;
  args = (char **) malloc(n * sizeof(char *));
  if(!args) return x_error(X_FAILURE, errno, fn, "malloc() error (%d char *)", n);

  L = (int *) calloc(n, sizeof(int));
  if(!L) {
    free(args);
    return x_error(X_FAILURE, errno, fn, "malloc() error (%d int)", n);
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
    args[next + HMSET_TYPE_OFFSET] = smaxStringType(f->type);

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

    // Call script
    if(!status) status = redisxSendArrayRequestAsync(cl, (const char **) args, NULL, n);
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

  prop_error(fn, status);

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
 *                    X_NAME_INVALID    if the name is invalid, or
 *                    X_NO_SERVICE      if the script with the given SHA1 id is not loaded into Redis.
 *
 * @sa InitScriptsAsync()
 */
static int InitScript(const char *name, char **pSHA1) {
  static const char *fn = "InitScript";

  Redis *r = smaxGetRedis();
  RESP *reply;
  char *sha1 = NULL;
  int status = X_SUCCESS;

  if(!name) return smaxError(fn, X_NAME_INVALID);
  if(!r) return smaxError(fn, X_NO_INIT);

  if(*pSHA1 != NULL) free(*pSHA1);
  *pSHA1 = NULL;

  sha1 = smaxGetScriptSHA1(name, &status);
  if(status) {
    if(sha1) free(sha1);
    return x_trace(fn, name, status);
  }
  if(!sha1) return x_trace(fn, name, X_NULL);

  reply = redisxRequest(r, "SCRIPT", "EXISTS", sha1, NULL, &status);
  if(!status) status = redisxCheckRESP(reply, RESP_ARRAY, 1);
  if(!status) {
    RESP **array = (RESP **) reply->value;
    if(array[0]->n != 1) status = X_NO_SERVICE;
  }

  redisxDestroyRESP(reply);

  if(status) return x_trace(fn, name, status);

  *pSHA1 = sha1;

  return X_SUCCESS;
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
      x_trace_null("InitScriptsAsync", NULL);

      if(!smaxIsDisabled()) {
        if(first) fprintf(stderr, "ERROR! SMA-X: Missing LUA script(s) in Redis.\n");
        return; // if not reconnecting, give up...
      }

      if(first) fprintf(stderr, "WARNING! SMA-X: Waiting for LUA scripts to be loaded into Redis.\n");
      if(SMAX_RECONNECT_RETRY_SECONDS > 0) sleep(SMAX_RECONNECT_RETRY_SECONDS);
    }
  }
}
