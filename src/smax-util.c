/**
 * \file
 *
 * \date Jun 25, 2019
 * \author Attila Kovacs
 *
 * \brief
 *      A collection of commonly used functions for the SMA-X library.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // sleep()
#include <pthread.h>
#include <math.h>
#include <float.h>
#include <errno.h>
#include <ctype.h>

#include "smax.h"
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
  if(m) smaxResetMeta(m);
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
  return xGetElementCount(m->storeDim, m->storeSizes);
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
void smaxTransmitErrorHandler(Redis *redis, int channel, const char *op) {
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
/// \endcond


/**
 * \cond PROTECTED
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

  if(table) {
    if(!lTab) lTab = strlen(table);
    if(lTab > 0) hash += smaxGetHash(table, lTab, X_STRING);
  }

  if(key) {
    if(!lKey) lKey = strlen(key);
    if(lKey > 0) hash += smaxGetHash(key, lKey, X_STRING);
  }

  return (char) (hash & 0xff);
}
/// \endcond


/**
 * A quick 32-bit integer hashing algorithm. It uses a combination of 32-bit XOR products and summing to
 * obtain something reasonably robust at detecting changes. The returned hash is unique for data that fits in
 * 4-bytes.
 *
 * \param buf       Pointer to the byte buffer to calculate a hash on
 * \param size      Size of the byte buffer
 * \param type      SMA-X type (character arrays are negative, with values of -length, see smax.h)
 *
 * \return          An integer hash value.
 */
long smaxGetHash(const char *buf, const int size, const XType type) {
  int i;
  long sum = 0;

  const int nChars = xIsCharSequence(type) ? xElementSizeOf(type) : 0;

  if(!buf) return SMAX_DEFAULT_HASH;
  if(size <= 0) return SMAX_DEFAULT_HASH;

  for(i=0; i<size; i++) {
    // For character arrays, jump to next element if reached termination...
    if(nChars) if(buf[i] == '\0') {
      i += nChars - (i % nChars);
      continue;
    }

    sum += buf[i];  // Calculate a simple sum of all relevant bytes
  }

  return sum;
}


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
  static const char *funcName = "smaxGetScriptSHA1()";

  Redis *redis = smaxGetRedis();
  RESP *reply;
  char *sha1;


  if(scriptName == NULL) {
    *status = smaxError(funcName, X_NAME_INVALID);
    return NULL;
  }

  reply = redisxRequest(redis, "HGET", SMAX_SCRIPTS, scriptName, NULL, status);

  if(*status) {
    redisxDestroyRESP(reply);
    smaxError(funcName, *status);
    return NULL;
  }

  *status = redisxCheckDestroyRESP(reply, RESP_BULK_STRING, 0);
  if(*status) {
    smaxError(funcName, *status);
    return NULL;
  }

  sha1 = (char *) reply->value;
  reply->value = NULL;

  redisxDestroyRESP(reply);

  return sha1;
}

/**
 * \cond PROTECTED
 *
 * \return <code>TRUE</code> (non-zero) if SMA-X is currently diabled (e.g. to reconnect), or else
 *         <code>FALSE</code> (zero).
 */
boolean smaxIsDisabled() {
  return isDisabled;
}

static void *SMAXReconnectThread(void *arg) {
  // Detach this thread (i.e. never to be joined...)
  pthread_detach(pthread_self());

  fprintf(stderr, "INFO: SMA-X will attempt to reconnect...\n");

  // Keep trying until successful
  while(smaxReconnect() != X_SUCCESS) sleep(SMAX_RECONNECT_RETRY_SECONDS);

  fprintf(stderr, "INFO: SMA-X reconnected!\n");

  // Wait for prior connection errors to clear up, before we exit 'reconnecting' state...
  sleep(SMAX_RECONNECT_RETRY_SECONDS);

  // Reset the reconnection status...
  smaxLockConfig();
  isDisabled = FALSE;
  smaxUnlockConfig();

  return NULL;
}


/**
 * Prints the given UNIX time into the supplied buffer with subsecond precision.
 *
 * \param[in]   time    Pointer to time value.
 * \param[out]  buf     Pointer to string buffer, must be at least X_TIMESTAMP_LENGTH in size.
 *
 * \return      Number of characters printed, not including the terminating '\0';
 *
 */
__inline__ int smaxTimeToString(const struct timespec *time, char *buf) {
  if(!buf) return X_NULL;
  return sprintf(buf, "%lld.%06ld", (long long) time->tv_sec, (time->tv_nsec / 1000));
}


/**
 * Prints the current time into the supplied buffer with subsecond precision.
 *
 * \param[out]  buf     Pointer to string buffer, must be at least X_TIMESTAMP_LENGTH in size.
 *
 * \return      Number of characters printed, not including the terminating '\0';
 *
 */
int smaxTimestamp(char *buf) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return smaxTimeToString(&ts, buf);
}

/**
 * Parses a timestamp into broken-down UNIX time.
 *
 * \param[in]   timestamp     Timestamp string as returned in redis queries;
 * \param[out]  secs          Pointer to the returned UNIX time (seconds).
 * \param[out]  nanosecs      Pointer to the retuned sub-second remainder as nanoseconds, or NULL if nor requested.
 *
 * \return              X_SUCCESS(0)    if the timestamp was successfully parsed.
 *                      -1              if there was no timestamp (empty or invalid string)
 *                      1               if there was an error parsing the nanosec part.
 *                      X_NULL          if the secs arhument is NULL
 */
int smaxParseTime(const char *timestamp, time_t *secs, long *nanosecs) {
  char *next;

  if(!timestamp) return -1;
  if(!secs) return X_NULL;

  *secs = (time_t) strtoll(timestamp, &next, 10);
  if(errno == ERANGE || next == timestamp) {
    *nanosecs = 0;
    return -1;
  }

  if(*next == '.') {
    char *end;
    double d = strtod(next, &end);
    if(errno == ERANGE || end == next) {
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
 * \return      Corresponding UNIX time with sub-second precision.
 */
double smaxGetTime(const char *timestamp) {
  time_t sec;
  long nsec;

  if(timestamp == NULL) return 0.0; // TODO NAN?

  if(smaxParseTime(timestamp, &sec, &nsec) < 0) return 0.0;
  return sec + 1e-9 * nsec;
}


/**
 * Creates a generic field of a given name and type and dimensions using the specified native values.
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
  const char *funcName = "smaxCreateField()";
  int n;
  XField *f;

  if(type != X_RAW && type != X_STRING) if(xStringElementSizeOf(type) < 1) {
    smaxError(funcName, X_TYPE_INVALID);
    return NULL;
  }

  if(type == X_RAW || type == X_STRUCT) return xCreateField(name, type, ndim, sizes, value);

  if(type != X_STRING) if(xStringElementSizeOf(type) < 1) {
    xError(funcName, X_TYPE_INVALID);
    return NULL;
  }

  n = xGetElementCount(ndim, sizes);
  if(n < 1) {
    xError(funcName, X_SIZE_INVALID);
    return NULL;
  }

  f = xCreateField(name, type, ndim, sizes, NULL);
  if(!f) return NULL;

  f->value = smaxValuesToString(value, type, n, NULL, 0);
  f->isSerialized = TRUE;

  return f;
}

/**
 * Converts a standard xchange field (with a native value storage) to an SMA-X field with
 * serialized string value storage.
 *
 * @param f     Pointer to field to convert
 * @return      X_SUCCESS (0) if successful, or
 *              X_NULL if the input field or the serialized value is NULL.
 *
 * @sa smax2xField()
 * @sa x2smaxStruct()
 */
int x2smaxField(XField *f) {
  void *value;

  if(!f) return X_NULL;
  if(!f->value) return X_SUCCESS;
  if(f->type == X_RAW) return X_SUCCESS;
  if(f->type == X_STRUCT) {
    f->isSerialized = TRUE;
    return x2smaxStruct((XStructure *) f->value);
  }
  if(f->isSerialized) return X_SUCCESS;

  value = f->value;
  f->value = smaxValuesToString(value, f->type, xGetFieldCount(f), NULL, 0);
  free(value);

  f->isSerialized = TRUE;

  if(!f->value) return X_NULL;

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
  void *str;
  int pos = 0, result, count, eSize;

  if(!f) return X_NULL;
  if(!f->value) return X_SUCCESS;
  if(f->type == X_RAW) return X_SUCCESS;
  if(f->type == X_STRUCT) {
    f->isSerialized = FALSE;
    return smax2xStruct((XStructure *) f->value);
  }
  if(!f->isSerialized) return X_SUCCESS;

  eSize = xElementSizeOf(f->type);
  if(eSize <= 0) return X_TYPE_INVALID;

  count = xGetFieldCount(f);
  if(count <= 0) return X_SIZE_INVALID;

  str = f->value;
  f->value = calloc(count, eSize);
  if(!f->value) {
    free(str);
    return X_NULL;
  }

  result = smaxStringToValues(str, f->value, f->type, count, &pos);
  free(str);

  f->isSerialized = FALSE;

  return result;
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
  XField *f;
  int status = X_SUCCESS;

  if(!s) return X_STRUCT_INVALID;

  for(f = s->firstField; f; f = f->next) {
    int res = x2smaxField(f);
    if(!status) status = res;
  }

  return status;
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
  XField *f;
  int status = X_SUCCESS;

  if(!s) return X_STRUCT_INVALID;

  for(f = s->firstField; f; f = f->next) {
    int res = smax2xField(f);
    if(!status) status = res;
  }

  return status;
}


/**
 * Splits the id into two strings (sharing the same input buffer) for SMA-X table and field.
 * The original input id is string terminated after the table name. And the pointer to the key
 * part that follows after the last separator is returned in the second (optional argument).
 *
 * \param[in,out] id        String containing an aggregate SMA-X ID (table:field), which will be terminated after the table part.
 * \param[out] pKey         Returned pointer to the second component after the separator within the same buffer. This is
 *                          not an independent pointer. Use smaxStringCopyOf() if you need an idependent string
 *                          on which free() can be called! The returned value pointed to may be NULL if the ID
 *                          could not be split. The argument may also be null, in which case the input string is
 *                          just terminated at the stem, without returning the second part.
 *
 * \return      X_SUCCESS (0)       if the ID was successfully split into two components.
 *              X_NULL              if the id argument is NULL.
 *              X_NAME_INVALID      if no separator was found
 *
 */
int smaxSplitID(char *id, char **pKey) {
  char *s;

  if(id == NULL) return X_NULL;

  // Default NULL return for the second component.
  if(pKey) *pKey = NULL;

  s = xLastSeparator(id);
  if(s) *s = '\0';
  else return X_NAME_INVALID;

  if(pKey) *pKey = s + X_SEP_LENGTH;

  return X_SUCCESS;
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
  if(!smaxIsConnected()) return X_NO_INIT;
  return redisxGetTime(smaxGetRedis(), t);
}


/**
 * Serializes binary values into a string representation (for REDIS).
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
  int eSize=1, k, stringSize = 1;
  char *sValue, *next;

  const double *d = (double *) value;
  const float *f = (float *) value;
  const int *i = (int *) value;
  const long long *l = (long long *) value;
  const short *s = (short *) value;
  char *c = (char *) value;
  char **S = (char **) value;

  if(value == NULL) type = X_UNKNOWN;                   // Print zero(es) for null value.
  if(type == X_STRUCT) return NULL;                     // structs are not serialized by this function.
  if(type == X_RAW) if(value) return *(char **) value;

  // Figure out how big the serialized string might be...
  if(type == X_UNKNOWN) stringSize = 2 * eCount;
  else if(type == X_STRING) {
    stringSize = 1;
    for(k = 0; k < eCount; k++) stringSize += strlen(S[k]) + 1;
  }
  else {
    eSize = xElementSizeOf(type);
    if(eSize <= 0) return NULL;                         // Unsupported element type...
    stringSize = eCount * xStringElementSizeOf(type);
  }

  if(stringSize <= 0) stringSize = 1;                   // empty string

  // Use the supplied buffer if large enough, or dynamically allocate one.
  if(trybuf != NULL && stringSize <= trylength) sValue = trybuf;
  else sValue = (char *) malloc(stringSize);

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
    case X_BOOLEAN:
      for(k=0; k<eCount; k++) next += sprintf(next, "%c ", (i[k] != 0 ? '1' : '0'));
      break;
    case X_BYTE:
      for(k=0; k<eCount; k++) next += sprintf(next, "%hhd ", c[k]);
      break;
    case X_BYTE_HEX:
      for(k=0; k<eCount; k++) next += sprintf(next, "0x%hhx ", (unsigned char) c[k]);
      break;
    case X_SHORT:
      for(k=0; k<eCount; k++) next += sprintf(next, "%hd ", s[k]);
      break;
    case X_SHORT_HEX:
      for(k=0; k<eCount; k++) next += sprintf(next, "0x%hx ", (unsigned short) s[k]);
      break;
    case X_INT:
      for(k=0; k<eCount; k++) next += sprintf(next, "%d ", i[k]);
      break;
    case X_INT_HEX:
      for(k=0; k<eCount; k++) next += sprintf(next, "0x%x ", i[k]);
      break;
    case X_LONG:
      for(k=0; k<eCount; k++) next += sprintf(next, "%lld ", l[k]);
      break;
    case X_LONG_HEX:
      for(k=0; k<eCount; k++) next += sprintf(next, "0x%llx ", l[k]);
      break;
    case X_FLOAT:
      for(k=0; k<eCount; k++) {
        next += xPrintFloat(next, f[k]);
        *(next++) = ' ';
      }
      break;
    case X_DOUBLE:
      for(k=0; k<eCount; k++) {
        next += xPrintDouble(next, d[k]);
        *(next++) = ' ';
      }
      break;
    case X_STRING:
      for(k=0; k<eCount; k++) next += sprintf(next, "%s\r", S[k]);
      break;
    default:
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
  if(errno == ERANGE) {
    *next = NextToken(*next);
    *status = X_PARSE_ERROR;
  }
}

/**
 * Deserializes a string to binary values.
 *
 * \param[in]   str           Serialized ASCII representation of the data (as stored by REDIS).
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
  char *next;
  int status = 0, eSize, k;

  double *d = (double *) value;
  float *f = (float *) value;
  int *i = (int *) value;
  long long *l = (long long *) value;
  short *s = (short *) value;
  char *c = (char *) value;
  boolean *b = (boolean *) value;

  if(value == NULL) return X_NULL;
  if(eCount <= 0) return X_SIZE_INVALID;

  if(type == X_RAW || type == X_STRUCT) return X_TYPE_INVALID;

  if(type == X_STRING) return xUnpackStrings(str, strlen(str), eCount, (char **) value);

  eSize = xElementSizeOf(type);
  if(eSize <= 0) return X_SIZE_INVALID;

  if(str == NULL) {
    xZero(value, type, eCount);
    return X_NULL;
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
      case X_BOOLEAN:
        for(k=0; k<eCount && *next; k++) {
          b[k] = xParseBoolean(next, &next);
          CheckParseError(&next, &status);
        }
        break;
      case X_BYTE:
      case X_BYTE_HEX:
        for(k=0; k<eCount && *next; k++) {
          c[k] = (char) strtol(next, &next, 0);
          CheckParseError(&next, &status);
        }
        break;
      case X_SHORT:
      case X_SHORT_HEX:
        for(k=0; k<eCount && *next; k++) {
          s[k] = (short) strtol(next, &next, 0);
          CheckParseError(&next, &status);
        }
        break;
      case X_INT:
      case X_INT_HEX:
        for(k=0; k<eCount && *next; k++) {
          i[k] = (int) strtol(next, &next, 0);
          CheckParseError(&next, &status);
        }
        break;
      case X_LONG:
      case X_LONG_HEX:
        for(k=0; k<eCount && *next; k++) {
          l[k] = (int) strtoll(next, &next, 0);
          CheckParseError(&next, &status);
        }
        break;
      case X_FLOAT:
        for(k=0; k<eCount && *next; k++) {
          f[k] = (float) xParseDouble(next, &next);
          CheckParseError(&next, &status);
        }
        break;
      case X_DOUBLE:
        for(k=0; k<eCount && *next; k++) {
          d[k] = xParseDouble(next, &next);
          CheckParseError(&next, &status);
        }
        break;
      default: return X_TYPE_INVALID;         // Unknown type...
    }

    // Zero out the remaining elements...
    if(k < eCount) xZero(&c[k], type, eCount - k);
  }

  *pos = next - str;

  return status ? status : k;
}


#if !(__Lynx__ && __powerpc__)

/**
 * Deletes variables and metadata from SMA-X.
 *
 * @param pattern   Glob variable name pattern
 * @return          The number of variables deleted from the SQL DB
 */
int smaxDeletePattern(const char *pattern) {
  char *metaPattern;
  int n = redisxDeleteEntries(smaxGetRedis(), pattern);

  if(n < 0) return -1;

  metaPattern = (char *) malloc(strlen(pattern) + 20);
  if(!metaPattern) {
    perror("ERROR! alloc meta name pattern");
    exit(errno);
  }

  sprintf(metaPattern, "<*>" X_SEP "%s", pattern);
  redisxDeleteEntries(smaxGetRedis(), metaPattern);
  free(metaPattern);

  return n;
}

#endif
