/**
 * \file
 *
 * \date Jan 26, 2018
 * \author Attila Kovacs
 * \version 1.0
 *
 *  smax-clib C/C++ client library to an SMA-X database, available on GitHub as:
 *
 *   https://github.com/Smithsonian/redisx
 *
 */

#ifndef SMAX_H_
#define SMAX_H_

#include <time.h>
#include <math.h>

#include <redisx.h>
#include <xchange.h>

#ifndef SMAX_DEFAULT_HOSTNAME
#  define SMAX_DEFAULT_HOSTNAME             "smax"      ///< Host name of Redis server used for SMA-X.
#endif

#ifndef SMAX_SENTINEL_SERVICENAME
#  define SMAX_SENTINEL_SERVICENAME         "SMA-X"     ///< Sentinel service name for SMA-X.
#endif

#ifndef SMAX_DEFAULT_PIPELINE_ENABLED
#  define SMAX_DEFAULT_PIPELINE_ENABLED     TRUE        ///< Whether pipelining is enabled by default.
#endif

#ifndef SMAX_RESTORE_QUEUE_ON_RECONNECT
#  define SMAX_RESTORE_QUEUE_ON_RECONNECT   TRUE        ///< Whether read queues are restored if SMA-X is disconnected/reconnected.
#endif

#ifndef SMAX_DEFAULT_MAX_QUEUED
#  define SMAX_DEFAULT_MAX_QUEUED           1024        ///< Maximum number of pull requests allowed to be queued at once.
#endif

#ifndef SMAX_PIPE_READ_TIMEOUT_MILLIS
#  define SMAX_PIPE_READ_TIMEOUT_MILLIS     3000        ///< (ms) Timeout for pipelined (queued) pull requests
#endif

#ifndef SMAX_RECONNECT_RETRY_SECONDS
#  define SMAX_RECONNECT_RETRY_SECONDS      3           ///< (s) Time between reconnection attempts on lost SMA-X connections.
#endif

/// API major version
#define SMAX_MAJOR_VERSION  0

/// API minor version
#define SMAX_MINOR_VERSION  9

/// Integer sub version of the release
#define SMAX_PATCHLEVEL     1

/// Additional release information in version, e.g. "-1", or "-rc1".
#define SMAX_RELEASE_STRING "-devel"

/// \cond PRIVATE

#ifdef str_2
#  undef str_2
#endif

/// Stringify level 2 macro
#define str_2(s) str_1(s)

#ifdef str_1
#  undef str_1
#endif

/// Stringify level 1 macro
#define str_1(s) #s

/// \endcond

/// The version string for this library
/// \hideinitializer
#define SMAX_VERSION_STRING str_2(SMAX_MAJOR_VERSION) "." str_2(SMAX_MINOR_VERSION) \
                                  "." str_2(SMAX_PATCHLEVEL) SMAX_RELEASE_STRING

/**
 * Character arrays a treated somewhat differently, with the element size
 * bundled in the type, to allow variable length strings to be properly
 * parsed into them without overflow...
 *
 * \hideinitializer
 */

/// \cond PROTECTED
#define SMAX_DEFAULT_HASH   (0xdeadbeef)    ///< \hideinitializer (should be quite uncommon with bytes outside the ASCII range -- unlike 0)
#define SMAX_LOOKUP_SIZE    256             ///< Hash lookup size (DON'T change!)
/// \endcond


#define SMAX_TYPES          "<types>"       ///< Redis meta table where variable types are stored.
#define SMAX_DIMS           "<dims>"        ///< Redis meta table where variable dimensions are stored.
#define SMAX_TIMESTAMPS     "<timestamps>"  ///< Redis meta table where variable timestamps are stored.
#define SMAX_ORIGINS        "<origins>"     ///< Redis meta table where variable origins are stored.
#define SMAX_WRITES         "<writes>"      ///< Redis meta table where the number of times a variable has been written is stored.
#define SMAX_READS          "<reads>"       ///< Redis meta table where the number of times a variable has been read is stored.

#define SMAX_SCRIPTS        "scripts"       ///< Redis table in which the built-in LUA script hashes are stored.

// Additional standard static metadata table names...
#define META_DESCRIPTION    "<descriptions>"    ///< Redis hash table in which variable descriptions are stored.
#define META_UNIT           "<units>"           ///< Redis hash table in which data physical unit names are stored
#define META_COORDS         "<coords>"          ///< Redis hash table in which data coordinates system descriptions are stored.

#define SMAX_UPDATES_ROOT   "smax"              ///< Notification class for SMA-X updates.
#define SMAX_UPDATES        SMAX_UPDATES_ROOT X_SEP     ///< PUB/SUB message channel heade for hash table updates.
#define SMAX_UPDATES_LENGTH  (sizeof(SMAX_UPDATES) - 1) ///< \hideinitializer String length of SMA-X update channel prefix.


// SMA-X program message types.
#define SMAX_MSG_STATUS     "status"        ///< Program status update.
#define SMAX_MSG_INFO       "info"          ///< Informational program message.
#define SMAX_MSG_DETAIL     "detail"        ///< Additional program detail provided (e.g. for verbose mode)
#define SMAX_MSG_PROGRESS   "progress"      ///< Program progress update.
#define SMAX_MSG_DEBUG      "debug"         ///< Program debug messages (also e.g. traces).
#define SMAX_MSG_WARNING    "warning"       ///< Program warnings.
#define SMAX_MSG_ERROR      "error"         ///< Program errors.

#define SMAX_ORIGIN_LENGTH  80              ///< (bytes) Maximum length of 'origin' meatdata, including termination.

/**
 * \brief Synchronization point that can be waited upon when queueing pipelined pulls.
 *
 * \sa smaxCreateSyncPoint()
 * \sa smaxDestroySyncPoint()
 * \sa smaxSync()
 *
 */
typedef struct {
  int status;                   ///< Synchronization status variable (usually X_INCOMPLETE or X_SUCCESS)
  pthread_cond_t *isComplete;   ///< Condition variable that is used for the actual wait.
  pthread_mutex_t *lock;        ///< Mutex lock
} XSyncPoint;

/**
 * \brief Structure that defines a coordinate axis in an XCoordinateSystem for an SMA-X data array.
 *
 * \sa XCoordinateSystem
 */
typedef struct {
  char *name;                   ///< Coordinate name, e.g. "x" or "time"
  char *unit;                   ///< Coordinate unit name, e.g. "GHz" or "ms"
  double refIndex;              ///< Data index at which the reference coordinate value is defined
  double refValue;              ///< Reference coordinate value in units set above
  double step;                  ///< Coordinate step between consecutive data, in the units defined above
} XCoordinateAxis;


/**
 * \brief Structure that defines a coordinate system, with one or more XCoordinateAxis.
 *
 * \sa smaxCreateCoordinateSystem()
 * \sa smaxDestroyCoordinateSystem()
 *
 */
typedef struct {
  int nAxis;                ///< Number of coordinate axes (i.e. dimension)
  XCoordinateAxis *axis;    ///< Array of coordinate axes, with nAxis size.
} XCoordinateSystem;


/**
 * \brief SMA-X standard metadata
 *
 * \sa smaxDestroyMeta()
 */
typedef struct XMeta {
  int status;                       ///< Error code or X_SUCCESS.
  XType storeType;                  ///< Type of variable as stored.
  int storeDim;                     ///< Dimensionality of the data as stored.
  int storeSizes[X_MAX_DIMS];       ///< Sizes along each dimension of the data as stored.
  int storeBytes;                   ///< Total number of bytes stored.
  char origin[SMAX_ORIGIN_LENGTH];  ///< Host name that last modified.
  struct timespec timestamp;        ///< Timestamp of the last modification.
  int serial;                       ///< Number of times the variable was updated.
} XMeta;

/**
 * Default initialized for SMA-X medatadata structure. You should always initialize local metadata with
 * this.
 */
#define X_META_INIT             { 0, X_UNKNOWN, -1, {0}, -1, {'\0'}, {}, 0 }

/**
 * \brief SMA-X program message
 *
 */
typedef struct {
  char *host;                   ///< Host where message originated from
  char *prog;                   ///< Originator program name
  char *type;                   ///< Message type, e.g. "info", "detail", "warning", "error"
  char *text;                   ///< Message body (with timestamp stripped).
  double timestamp;             ///< Message timestamp, if available (otherwise 0.0)
} XMessage;

// Meta helpers ----------------------------------------------->
XMeta *smaxCreateMeta();
void smaxResetMeta(XMeta *m);
int smaxGetMetaCount(const XMeta *m);
void smaxSetOrigin(XMeta *m, const char *origin);

// Globally available functions provided by SMA-X ------------->
int smaxSetServer(const char *host, int port);
int smaxSetSentinel(const RedisServer *servers, int nServers);
int smaxSetAuth(const char *username, const char *password);
int smaxSetDB(int idx);
int smaxSetTcpBuf(int size);

int smaxSetTLS(const char *ca_path, const char *ca_file);
int smaxDisableTLS();
int smaxSetTLSVerify(boolean value);
int smaxSetMutualTLS(const char *cert_file, const char *key_file);
int smaxSetTLSServerName(const char *host);
int smaxSetTLSCiphers(const char *list);
int smaxSetTLSCipherSuites(const char *list);
int smaxSetDHCipherParams(const char *dh_file);

int smaxConnect();
int smaxConnectTo(const char *server);
int smaxDisconnect();
int smaxIsConnected();
int smaxReconnect();

// Connect/disconnect callback hooks  -------------------->
int smaxAddConnectHook(void (*setupCall)(void));
int smaxRemoveConnectHook(void (*setupCall)(void));
int smaxAddDisconnectHook(void (*cleanupCall)(void));
int smaxRemoveDisconnectHook(void (*cleanupCall)(void));

// Basic information exchage routines -------------------->
int smaxPull(const char *table, const char *key, XType type, int count, void *value, XMeta *meta);
int smaxShare(const char *table, const char *key, const void *value, XType type, int count);
int smaxShareArray(const char *table, const char *key, const void *value, XType type, int ndim, const int *sizes);
int smaxShareField(const char *table, const XField *f);

// Some convenience methods for simpler pulls ------------>
char *smaxPullRaw(const char *table, const char *key, XMeta *meta, int *status);
long long smaxPullLong(const char *table, const char *key, long long defaultValue);
int smaxPullInt(const char *table, const char *key, int defaultValue);
double smaxPullDouble(const char *table, const char *key);
double smaxPullDoubleDefault(const char *table, const char *key, double defaultValue);
char *smaxPullString(const char *table, const char *key);
long long *smaxPullLongs(const char *table, const char *key, XMeta *meta, int *n);
int *smaxPullInts(const char *table, const char *key, XMeta *meta, int *n);
double *smaxPullDoubles(const char *table, const char *key, XMeta *meta, int *n);
char **smaxPullStrings(const char *table, const char *key, XMeta *meta, int *n);
XStructure *smaxPullStruct(const char *name, XMeta *meta, int *status);
XField *smaxPullField(const char *id, XMeta *meta, int *status);

// Convenience methods for serialized strucures ---------->
boolean smaxGetBooleanField(const XStructure *s, const char *name, boolean defaultValue);
long long smaxGetLongField(const XStructure *s, const char *name, long long defaultValue);
double smaxGetDoubleField(const XStructure *s, const char *name, double defaultValue);
char *smaxGetRawField(const XStructure *s, const char *name, char *defaultValue);
int smaxGetArrayField(const XStructure *s, const char *name, void *dst, XType type, int count);

// Pipelined pull requests ------------------------------->
int smaxQueue(const char *table, const char *key, XType type, int count, void *value, XMeta *meta);
XSyncPoint *smaxCreateSyncPoint();
int smaxQueueCallback(void (*f)(void *), void *arg);
void smaxDestroySyncPoint(XSyncPoint *sync);
int smaxSync(XSyncPoint *sync, int timeoutMillis);
int smaxWaitQueueComplete(int timeoutMillis);


// Lazy pulling ------------------------------------------>
int smaxLazyPull(const char *table, const char *key, XType type, int count, void *value, XMeta *meta);
long long smaxLazyPullLong(const char *table, const char *key, long long defaultValue);
double smaxLazyPullDouble(const char *table, const char *key);
double smaxLazyPullDoubleDefault(const char *table, const char *key, double defaultValue);
int smaxLazyPullChars(const char *table, const char *key, char *buf, int n);
char *smaxLazyPullString(const char *table, const char *key);
int smaxLazyPullStruct(const char *id, XStructure *s);
int smaxLazyCache(const char *table, const char *key, XType type);
int smaxGetLazyCached(const char *table, const char *key, XType type, int count, void *value, XMeta *meta);
int smaxLazyEnd(const char *table, const char *key);
int smaxLazyFlush();
int smaxGetLazyUpdateCount(const char *table, const char *key);


// Some convenience methods for simpler shares ----------->
int smaxShareBoolean(const char *table, const char *key, boolean value);
int smaxShareInt(const char *table, const char *key, long long value);
int smaxShareDouble(const char *table, const char *key, double value);
int smaxShareString(const char *table, const char *key, const char *sValue);
int smaxShareBooleans(const char *table, const char *key, const boolean *values, int n);
int smaxShareBytes(const char *table, const char *key, const char *values, int n);
int smaxShareShorts(const char *table, const char *key, const short *values, int n);
int smaxShareInts(const char *table, const char *key, const int *values, int n);
int smaxShareLongs(const char *table, const char *key, const long *values, int n);
int smaxShareLLongs(const char *table, const char *key, const long long *values, int n);
int smaxShareFloats(const char *table, const char *key, const float *values, int n);
int smaxShareDoubles(const char *table, const char *key, const double *values, int n);
int smaxShareStrings(const char *table, const char *key, const char **sValues, int n);
int smaxShareStruct(const char *id, const XStructure *s);

// Notifications ---------------------------------------------->
int smaxSubscribe(const char *table, const char *key);
int smaxUnsubscribe(const char *table, const char *key);
int smaxWaitOnSubscribed(const char *table, const char *key, int timeout);
int smaxWaitOnSubscribedGroup(const char *matchTable, char **changedKey, int timeout);
int smaxWaitOnSubscribedVar(const char *matchKey, char **changedTable, int timeout);
int smaxWaitOnAnySubscribed(char **changedTable, char **changedKey, int timeout);
int smaxReleaseWaits();
int smaxAddSubscriber(const char *stem, RedisSubscriberCall f);
int smaxRemoveSubscribers(RedisSubscriberCall f);

// Messages --------------------------------------------------->
int smaxSendStatus(const char *msg, ...);
int smaxSendInfo(const char *msg, ...);
int smaxSendDetail(const char *msg, ...);
int smaxSendDebug(const char *msg, ...);
int smaxSendWarning(const char *msg, ...);
int smaxSendError(const char *msg, ...);
int smaxSendProgress(double fraction, const char *msg, ...);
int smaxAddMessageProcessor(const char *host, const char *prog, const char *type, void (*f)(XMessage *));
int smaxAddDefaultMessageProcessor(const char *host, const char *prog, const char *type);
int smaxRemoveMessageProcessor(int id);
void smaxSetMessageSenderID(const char *id);


// TODO List type access -------------------------------------->
/*
int smaxGetListSize(const char *name);
int smaxPushToList(const char *name, const char *value);
char *smaxPopFromlist(const char *name, int *n);
char *smaxPeekList(const char *name, int idx, int *n);
char **smaxGetListEntries(const char *name, int from, int toInclusive, int *n);
*/

// Summaries -------------------------------------------------->
int smaxKeyCount(const char *table);
char **smaxGetKeys(const char *table, int *n);

// Metadata --------------------------------------------------->
double smaxPullTime(const char *table, const char *key);
XType smaxPullTypeDimension(const char *table, const char *key, int *ndim, int *sizes);

// Optional static metadata ----------------------------------->
int smaxSetDescription(const char *table, const char *key, const char *description);
char *smaxGetDescription(const char *table, const char *key);
int smaxSetUnits(const char *table, const char *key, const char *unit);
char *smaxGetUnits(const char *table, const char *key);
int smaxSetCoordinateAxis(const char *id, int n, const XCoordinateAxis *axis);
XCoordinateAxis *smaxGetCoordinateAxis(const char *id, int n);
int smaxSetCoordinateSystem(const char *table, const char *key, const XCoordinateSystem *coords);
XCoordinateSystem *smaxGetCoordinateSystem(const char *table, const char *key);
XCoordinateSystem *smaxCreateCoordinateSystem(int nAxis);
void smaxDestroyCoordinateSystem(XCoordinateSystem *coords);
int smaxPushMeta(const char *meta, const char *table, const char *key, const char *value);
char *smaxPullMeta(const char *meta, const char *table, const char *key, int *len);


// Structures ------------------------------------------------->
int x2smaxStruct(XStructure *s);
int x2smaxField(XField *f);
int smax2xStruct(XStructure *s);
int smax2xField(XField *f);

XField *smaxCreateScalarField(const char *name, XType type, const void *value);
XField *smaxCreate1DField(const char *name, XType type, int size, const void *value);
XField *smaxCreateField(const char *name, XType type, int ndim, const int *sizes, const void *value);
XField *smaxCreateDoubleField(const char *name, double value);
XField *smaxCreateLongField(const char *name, long long value);
XField *smaxCreateIntField(const char *name, int value);
XField *smaxCreateBooleanField(const char *name, boolean value);
XField *smaxCreateStringField(const char *name, const char *value);


// Helpers / Controls ----------------------------------------->
Redis *smaxGetRedis();
void smaxSetVerbose(boolean value);
boolean smaxIsVerbose();
void smaxSetResilient(boolean value);
boolean smaxIsResilient();
void smaxSetResilientExit(boolean value);
int smaxSetPipelined(boolean isEnabled);
boolean smaxIsPipelined();
int smaxSetMaxPendingPulls(int n);
char *smaxGetScriptSHA1(const char *scriptName, int *status);
char *smaxGetHostName();
void smaxSetHostName(const char *name);
char *smaxGetProgramID();
int smaxGetServerTime(struct timespec *t);
const char *smaxErrorDescription(int code);
int smaxError(const char *func, int errorCode);

// Low-level utilities ---------------------------------------->
char *smaxStringType(XType type);
XType smaxTypeForString(const char *type);
int smaxUnpackStrings(const char *data, int len, int count, char **dst);
int smaxStringToValues(const char *str, void *value, XType type, int count, int *parsed);
char *smaxValuesToString(const void *value, XType type, int count, char *trybuf, int trylength);
int smaxTimestamp(char *buf);
int smaxParseTime(const char *timestamp, time_t *secs, long *nanosecs);
double smaxGetTime(const char *timestamp);
int smaxTimeToString(const struct timespec *time, char *buf);
int smaxSetPipelineConsumer(void (*f)(RESP *));

// The following is not available on prior to the POSIX.1-2001 standard
#if _POSIX_C_SOURCE >= 200112L
int smaxDeletePattern(const char *pattern);
#endif


#endif /* SMAX_H_ */
