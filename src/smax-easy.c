/**
 * \file
 *
 * \date Apr 6, 2019
 * \author Attila Kovacs
 *
 * \brief
 *      A set of functions for simplified access to SMA-X for specific variable types.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "smax-private.h"

/**
 * Returns a dynamically allocated buffer with the raw string value stored in SMA-X.
 * This call can also be used to get single string values from SMA-X, since for single
 * string the stored raw value is simply the string itself. However, to properly
 * retrieve string arrays, you want to use smaxPullStrings() instead.
 *
 * \param[in]   table     The hash table name.
 * \param[in]   key       The variable name under which the data is stored.
 * \param[out]  meta      (optional) Pointer to metadata to be filled or NULL if not required.
 * \param[out]  status    Pointer int which an error status is returned.
 *
 * \return      Pointer to C array containing the elements of the specified type, or NULL.
 *
 * @sa smaxPullStrings()
 *
 */
char *smaxPullRaw(const char *table, const char *key, XMeta *meta, int *status) {
  static const char *fn = "smaxPullRaw";

  char *ptr = NULL;

  if(!status) {
    x_error(0, EINVAL, fn, "output 'status' parameter is NULL");
    return NULL;
  }

  *status = smaxPull(table, key, X_RAW, 1, &ptr, meta);
  if(*status) x_trace_null(fn, NULL);

  return ptr;
}

/**
 * Returns a dynamically allocated XStrucure for the specified hashtable in SMA-X.
 *
 * \param[in]   id        Aggregated structure ID.
 * \param[out]  meta      (optional) Pointer to metadata to be filled or NULL if not required.
 * \param[out]  status    Pointer int which an error status is returned.
 *
 * \return      Pointer to an XStructure, or NULL.
 *
 * @sa smaxLazyPullStruct()
 * @sa xDestroyStruct()
 * @sa smaxPullNode()
 */
XStructure *smaxPullStruct(const char *id, XMeta *meta, int *status) {
  static const char *fn = "smaxPullStruct";
  XStructure *s;

  if(!status) {
    x_error(0, EINVAL, fn, "output 'status' parameter is NULL");
    return NULL;
  }

  s = (XStructure *) calloc(1, sizeof(XStructure));
  x_check_alloc(s);

  *status = smaxPull(id, NULL, X_STRUCT, 1, s, meta);
  if(*status) x_trace_null(fn, NULL);

  return s;
}

/**
 * Returns a dynamically allocated deserialized XField for the specified node in SMA-X. You
 * should use this function with great care, as it might retrieve very large data from SMA-X,
 * and therefore block access to the database for a long time. It's OK to use to retrieve data
 * for smaller sub-hierarchies, but you should probably stay away from using to to pull large
 * hierarchies.
 *
 * @param id            The aggregate SMA-X ID of the node
 * @param[out] meta     Pointer to where to return metadata, or NULL if metadata is not required.
 * @param[out] status   Pointer to integer in which to return status, or NULL if not required.
 * @return              A field containing the data for the node, or NULL if there was an error.
 *
 * @sa smaxShareField()
 */
XField *smaxPullField(const char *id, XMeta *meta, int *status) {
  static const char *fn = "smaxPullField";

  char *str, *table, *key;
  XType type;
  int l = 0, ndim;
  int sizes[X_MAX_DIMS], count, s;
  void *value;
  XField *f;

  if(!id) {
    x_error(0, EINVAL, fn, "input ID is NULL");
    return NULL;
  }

  if(!id[0]) {
    x_error(0, EINVAL, fn, "input ID is empty");
    return NULL;
  }

  if(status) *status = X_FAILURE;

  str = redisxGetStringValue(smaxGetRedis(), SMAX_TYPES, id, &l);
  if(l < 0 || str == NULL) {
    if(status) *status = l;
    return x_trace_null(fn, "type");
  }

  type = smaxTypeForString(str);
  free(str);

  if(type == X_UNKNOWN) {
    if(status) *status = X_TYPE_INVALID;
    return x_trace_null(fn, "type");
  }

  str = redisxGetStringValue(smaxGetRedis(), SMAX_DIMS, id, &l);
  if(l < 0 || str == NULL) {
    if(status) *status = l;
    return x_trace_null(fn, "dims");
  }

  ndim = xParseDims(str, sizes);
  free(str);

  if(ndim < 0) {
    if(status) *status = ndim;
    return x_trace_null(fn, "dims");
  }

  count = xGetElementCount(ndim, sizes);
  if(count < 0) {
    if(status) *status = count;
    return x_trace_null(fn, "count");
  }

  value = calloc(count, xElementSizeOf(type));
  if(!value) {
    x_error(0, errno, fn, "alloc error (%d bytes)", xElementSizeOf(type) * count);
    return NULL;
  }

  table = xStringCopyOf(id);
  if(!table) {
    free(value);
    return x_trace_null(fn, "split id");
  }

  xSplitID(table, &key);

  s = smaxPull(table, key, type, count, value, meta);
  if(status) *status = s;

  if(s != X_SUCCESS) {
    free(value);
    return x_trace_null(fn, NULL);
  }

  f = xCreateField(key, type, ndim, sizes, value);
  if(!f) {
    if(status) *status = X_FAILURE;
    return x_trace_null(fn, NULL);
  }

  smax2xField(f);

  return f;
}

/**
 * Returns a dynamically allocated buffer with the values stored in SMA-X cast to the specified type.
 *
 * \param[in]   table     The hash table name.
 * \param[in]   key       The variable name under which the data is stored.
 * \param[in]   type      SMA-X data type (e.g. X_INT).
 * \param[out]  meta      (optional) Pointer to metadata to be filled or NULL if not required.
 * \param[out]  n         Pointer to which the number of elements is returned (if *n > 0) or else an error code.
 *
 * \return      Pointer to C array containing the elements of the specified type, or NULL.
 *
 */
static void *smaxPullDynamic(const char *table, const char *key, XType type, XMeta *meta, int *n) {
  static const char *fn = "smaxPullType";

  int eSize, pos;
  char *raw;
  XMeta m = X_META_INIT;
  void *array;

  if(!n) {
    x_error(0, EINVAL, fn, "output parameter 'n' is NULL");
    return NULL;
  }

  eSize = xElementSizeOf(type);
  if(eSize < 1) {
    *n = x_error(X_TYPE_INVALID, EINVAL, fn, "invalid type: %d", type);
    return NULL;
  }

  *n = smaxPull(table, key, X_RAW, 1, &raw, &m);
  if(*n) {
    if(raw) free(raw);
    return x_trace_null(fn, NULL);
  }
  if(!raw) return NULL;

  if(meta != NULL) *meta = m;

  *n = smaxGetMetaCount(&m);
  if(*n < 1) {
    free(raw);
    x_error(0, ERANGE, fn, "invalid store count: %d", *n);
    return NULL;
  }

  array = calloc(*n, eSize);
  if(!array) {
    *n = x_error(0, errno, fn, "calloc() error (%d x %d)", *n, eSize);
    free(raw);
    return NULL;
  }

  *n = smaxStringToValues(raw, array, type, *n, &pos);
  free(raw);

  if(*n < 0) {
    free(array);
    return x_trace_null(fn, NULL);
  }

  return array;
}

/**
 * Returns a dynamically allocated array of integers stored in an SMA-X variable.
 *
 * \param[in]   table     The hash table name.
 * \param[in]   key       The variable name under which the data is stored.
 * \param[out]  meta      (optional) Pointer to metadata to be filled or NULL if not required.
 * \param[out]  n         Pointer to which the number of integers is returned (if *n > 0) or else an error code.
 *
 * \return      Pointer to C int[] array containing *n elements, or NULL.
 *
 * @sa smaxPullShorts()
 * @sa smaxPullLongs()
 * @sa smaxPullInt()
 */
int *smaxPullInts(const char *table, const char *key, XMeta *meta, int *n) {
  int stat;
  int *ptr = (int *) smaxPullDynamic(table, key, X_INT, meta, &stat);
  if(n) *n = stat;
  if(stat < 0) x_trace_null("smaxPullInts", NULL);
  return ptr;
}

/**
 * Returns a dynamically allocated array of long long (int64) integers stored in an SMA-X variable.
 *
 * \param[in]   table     The hash table name.
 * \param[in]   key       The variable name under which the data is stored.
 * \param[out]  meta      (optional) Pointer to metadata to be filled or NULL if not required.
 * \param[out]  n         Pointer to which the number of integers is returned (if *n > 0) or else an error code.
 *
 * \return      Pointer to C int[] array containing *n elements, or NULL.
 *
 * @sa smaxPullInts()
 * @sa smaxPullShorts()
 * @sa smaxPullLong()
 *
 */
long long *smaxPullLongs(const char *table, const char *key, XMeta *meta, int *n) {
  int stat;
  long long *ptr = (long long *) smaxPullDynamic(table, key, X_INT, meta, &stat);
  if(n) *n = stat;
  if(stat < 0) x_trace_null("smaxPullLongs", NULL);
  return ptr;
}

/**
 * Returns a dynamically allocated array of doubles stored in an SMA-X variable.
 *
 * \param[in]   table     The hash table name.
 * \param[in]   key       The variable name under which the data is stored.
 * \param[out]  meta      (optional) Pointer to metadata to be filled or NULL if not required.
 * \param[out]  n         Pointer to which the number of double is returned (if *n > 0) or else an error code.
 *
 * \return      Pointer to C double[] array containing *n elements, or NULL.
 *
 * @sa smaxPullDouble()
 * @sa smaxPullFloats()
 */
double *smaxPullDoubles(const char *table, const char *key, XMeta *meta, int *n) {
  int stat;
  double *ptr = (double *) smaxPullDynamic(table, key, X_DOUBLE, meta, &stat);
  if(n) *n = stat;
  if(stat < 0) x_trace_null("smaxPullDoubles", NULL);
  return ptr;
}

/**
 * Returns a single string value for a given SMA-X variable, or a NULL if the
 * value could not be retrieved.
 *
 * \param table           Hash table name.
 * \param key             Variable name under which the data is stored.
 *
 * \return      Pouinter to the string value stored in SMA-X, or NULL if the value could not be retrieved.
 *
 * @sa smaxLazyPullString()
 * @sa smaxPullStrings()
 */
char *smaxPullString(const char *table, const char *key) {
  char *str = NULL;
  int status;

  status = smaxPull(table, key, X_STRING, 1, &str, NULL);
  if(status < 0) {
    if(str) free(str);
    return x_trace_null("smaxPullString", NULL);
  }

  return str;
}

/**
 * Returns an array of pointers to individuals strings inside the retrieved contiguous data buffer. Thus,
 * to discard the returned data after use, you must first discard the underlying buffer (as pointed by the
 * first element) before discarding the array of pointers themselves. E.g.:
 *
 * <code>
 *    char **array = smaxPullStrings("mygroup", "myfield", &meta);
 *    ...
 *    if(array != NULL) {
 *      free(array[0]);     // discards the underlying contiguous buffer
 *      free(array);        // discards the array of pointers.
 *    }
 * </code>
 *
 * \param[in]   table     The hash table name.
 * \param[in]   key       The variable name under which the data is stored.
 * \param[out]  meta      (optional) Pointer to metadata to be filled or NULL if not required.
 * \param[out]  n         Pointer to which the number of double is returned (if *n > 0) or else an error code.
 *
 * \return     Pointer to a an array of strings (char *) containing *n elements, or NULL.
 *
 * @sa smaxPullString()
 * @sa smaxPullRaw()
 */
char **smaxPullStrings(const char *table, const char *key, XMeta *meta, int *n) {
  static const char *fn = "smaxPullStrings";

  int i, offset = 0;
  char *str;
  XMeta m = X_META_INIT;
  char **array;

  if(!n) {
    x_error(0, EINVAL, fn, "output parameter 'n' is NULL");
    return NULL;
  }

  str = smaxPullRaw(table, key, &m, n);
  if(*n < 0) {
    free(str);
    return x_trace_null(fn, NULL);
  }

  if(str == NULL) return NULL;

  if(meta != NULL) *meta = m;

  *n = smaxGetMetaCount(&m);
  if(*n < 1) {
    x_error(0, ERANGE, fn, "invalid store count: %d", *n);
    return NULL;
  }

  array = (char **) calloc(*n, sizeof(char *));
  if(!array) {
    x_error(0, errno, fn, "calloc() error (%d char*)", *n);
    return NULL;
  }

  for(i = 0; i < (*n); i++) {
    array[i] = &str[offset];
    offset += strlen(array[i]) + 1;
    if(offset > m.storeBytes) break;
  }

  return array;
}

/**
 * Returns a single integer value for a given SMA-X variable, or a default value if the
 * value could not be retrieved.
 *
 * \param table           The hash table name.
 * \param key             The variable name under which the data is stored.
 * \param defaultValue    The value to return in case of an error.
 *
 * \return      The integer value stored in SMA-X, or the specified default if the value could not be retrieved.
 *
 * @sa smaxLazyPullInt()
 * @sa smaxPullInts()
 * @sa smaPullLong()
 */
int smaxPullInt(const char *table, const char *key, int defaultValue) {
  int i = 0;
  int status = smaxPull(table, key, X_INT, 1, &i, NULL);
  return status ? defaultValue : i;
}

/**
 * Returns a single integer value for a given SMA-X variable, or a default value if the
 * value could not be retrieved.
 *
 * \param table           The hash table name.
 * \param key             The variable name under which the data is stored.
 * \param defaultValue    The value to return in case of an error.
 *
 * \return      The integer value stored in SMA-X, or the specified default if the value could not be retrieved.
 *
 * @sa smaxLazyPullLong()
 * @sa smaxPullLongs()
 * @sa smaxPullInt()
 */
long long smaxPullLong(const char *table, const char *key, long long defaultValue) {
  long long l = 0;
  int status = smaxPull(table, key, X_LLONG, 1, &l, NULL);
  return status ? defaultValue : l;
}

/**
 * Returns a single floating-point value for a given SMA-X variable, or a NAN if the
 * value could not be retrieved.
 *
 * \param table           Hash table name.
 * \param key             Variable name under which the data is stored.
 *
 * \return      The floating-point value stored in SMA-X, or NAN if the value could not be retrieved.
 *
 * @sa smaxLazyPullDouble()
 * @sa smaxPullDoubleDefault()
 */
double smaxPullDouble(const char *table, const char *key) {
  return smaxPullDoubleDefault(table, key, NAN);
}

/**
 * Returns a single floating-point value for a given SMA-X variable, or a specified
 * default value if the SMA-X value could not be retrieved.
 *
 * \param table           Hash table name.
 * \param key             Variable name under which the data is stored.
 * \param defaultValue    The value to return in case of an error.
 *
 * \return      The floating-point value stored in SMA-X, or the specified default if the value could not be retrieved.
 *
 * @sa smaxLazyPullDoubleDefault()
 * @sa smaxPullDouble()
 */
double smaxPullDoubleDefault(const char *table, const char *key, double defaultValue) {
  double d;
  int status = smaxPull(table, key, X_DOUBLE, 1, &d, NULL);
  return status ? defaultValue : d;
}

/**
 * Shares a single signed byte value to SMA-X.
 *
 * \param table     Hash table name.
 * \param key       Variable name under which the data is stored.
 * \param value     Integer value.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareShort()
 * @sa smaxShareBytes()
 */
int smaxShareByte(const char *table, const char *key, signed char value) {
  prop_error("smaxShareInt", smaxShareBytes(table, key, &value, 1));
  return X_SUCCESS;
}

/**
 * Shares a single short integer value to SMA-X.
 *
 * \param table     Hash table name.
 * \param key       Variable name under which the data is stored.
 * \param value     Integer value.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareByte()
 * @sa smaxShareInt()
 * @sa smaxShareShorts()
 */
int smaxShareShort(const char *table, const char *key, short value) {
  prop_error("smaxShareInt", smaxShareShorts(table, key, &value, 1));
  return X_SUCCESS;
}

/**
 * Shares a single integer value to SMA-X.
 *
 * \param table     Hash table name.
 * \param key       Variable name under which the data is stored.
 * \param value     Integer value.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareShort()
 * @sa smaxShareLong()
 * \sa smaxShareHex()
 * @sa smaxShareInts()
 */
int smaxShareInt(const char *table, const char *key, int value) {
  prop_error("smaxShareInt", smaxShareInts(table, key, &value, 1));
  return X_SUCCESS;
}

/**
 * Shares a single 64-bit integer value to SMA-X.
 *
 * \param table     Hash table name.
 * \param key       Variable name under which the data is stored.
 * \param value     Integer value.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareInt()
 * \sa smaxShareHex()
 * @sa smaxShareInts()
 */
int smaxShareLong(const char *table, const char *key, long long value) {
  prop_error("smaxShareLong", smaxShareLLongs(table, key, &value, 1));
  return X_SUCCESS;
}


/**
 * Shares a single boolean value to SMA-X. All non-zero values are mapped
 * to "1".
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param value     A boolean value.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * \sa smaxShareBooleans()
 */
int smaxShareBoolean(const char *table, const char *key, boolean value) {
  prop_error("smaxShareBoolean", smaxShareBooleans(table, key, &value, 1));
  return X_SUCCESS;
}

/**
 * Shares a single floating point value to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param value     floating-point value.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareDoubles()
 * @sa smaxShareFloats()
 */
int smaxShareDouble(const char *table, const char *key, double value) {
  prop_error("smaxShareDouble", smaxShareDoubles(table, key, &value, 1));
  return X_SUCCESS;
}

/**
 * Shares a single string value to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param sValue    Pointer to string.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareStrings()
 */
int smaxShareString(const char *table, const char *key, const char *sValue) {
  prop_error("smaxShareString", smaxShare(table, key, &sValue, X_RAW, 1));
  return X_SUCCESS;
}

/**
 * Shares a binary sequence to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    pointer to the byte buffer.
 * \param n         Number of bytes in buffer to share.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareShorts()
 * @sa smaxShareInts()
 * @sa smaxShareLongs()
 * @sa smaxShareLLongs()
 * @sa smaxShareInt()
 */
int smaxShareBytes(const char *table, const char *key, const signed char *values, int n) {
  prop_error("smaxShareBytes", smaxShare(table, key, values, X_BYTE, n));
  return X_SUCCESS;
}

/**
 * Shares an array of shorts to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to `short[]` array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS(0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareInt()
 * @sa smaxShareBytes()
 * @sa smaxShareInts()
 * @sa smaxShareLongs()
 * @sa smaxShareLLongs()
 *
 */
int smaxShareShorts(const char *table, const char *key, const short *values, int n) {
  prop_error("smaxShareShorts", smaxShare(table, key, values, X_SHORT, n));
  return X_SUCCESS;
}

/**
 * Shares an array of `long` integers to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to `long[]` array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareInt()
 * @sa smaxShareLLongs()
 * @sa smaxShareInts()
 * @sa smaxShareShorts()
 * @sa smaxShareBytes()
 */
int smaxShareLongs(const char *table, const char *key, const long *values, int n) {
  prop_error("smaxShareLongs", smaxShare(table, key, values, X_LONG, n));
  return X_SUCCESS;
}

/**
 * Shares an array of `long long` integers to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to `long long[]` array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareLongs()
 * @sa smaxShareInts()
 * @sa smaxShareShorts()
 * @sa smaxShareBytes()
 * @sa smaxShareInt()
 */
int smaxShareLLongs(const char *table, const char *key, const long long *values, int n) {
  prop_error("smaxShareLongs", smaxShare(table, key, values, X_LLONG, n));
  return X_SUCCESS;
}

/**
 * Shares an array of long integers to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to `int[]` array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareLLongs()
 * @sa smaxShareLongs()
 * @sa smaxShareShorts()
 * @sa smaxShareBytes()
 * @sa smaxShareInt()
 */
int smaxShareInts(const char *table, const char *key, const int *values, int n) {
  prop_error("smaxShareInts", smaxShare(table, key, values, X_INT, n));
  return X_SUCCESS;
}

/**
 * Shares an array of boolean values to SMA-X. All non-zero values are mapped
 * to "1".
 *
 * \param table     Hash table name.
 * \param key       Variable name under which the data is stored.
 * \param values    Pointer to `boolean[]` array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareBoolean()
 */
int smaxShareBooleans(const char *table, const char *key, const boolean *values, int n) {
  prop_error("smaxShareBooleans", smaxShare(table, key, values, X_BOOLEAN, n));
  return X_SUCCESS;
}

/**
 * Shares an array of floats to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to `float[]` array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareDouble()
 * @sa smaxShareDoubles()
 */
int smaxShareFloats(const char *table, const char *key, const float *values, int n) {
  prop_error("smaxShareFloats", smaxShare(table, key, values, X_FLOAT, n));
  return X_SUCCESS;
}

/**
 * Shares an array of doubles to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to `double[]` array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareDouble()
 * @sa smaxShareFloats()
 */
int smaxShareDoubles(const char *table, const char *key, const double *values, int n) {
  prop_error("smaxShareDoubles", smaxShare(table, key, values, X_DOUBLE, n));
  return X_SUCCESS;
}

/**
 * Shares an array of strings to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param sValues   Pointer to array of string pointers.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS (0), or else an appropriate error code (&lt;0) from smaxShare().
 *
 * @sa smaxShareString()
 */
int smaxShareStrings(const char *table, const char *key, const char **sValues, int n) {
  static const char *fn = "smaxShareStrings";

  char *buf;
  int i, *l, L = 0;

  if(sValues == NULL) return x_error(X_NULL, EINVAL, fn, "input 'sValues' is NULL");

  l = (int *) calloc(n, sizeof(int));
  if(!l) return x_error(X_NULL, errno, fn, "calloc() error (%d int)", n);

  for(i=0; i<n; i++) {
    if(sValues[i] == NULL) l[i] = 1;
    else l[i] = strlen(sValues[i]) + 1;
    L += l[i];
  }

  if(L == 0) L = 1;
  buf = (char *) malloc(L);
  if(!buf) return x_error(X_NULL, errno, fn, "malloc() error (%d bytes)", L);

  L = 0;
  for(i=0; i<n; i++) {
    if(sValues[i] == NULL) *buf = '\0';
    else memcpy(buf, sValues[i], l[i]);
    L += l[i];
  }

  free(l);

  L = smaxShare(table, key, buf, X_RAW, 1);

  free(buf);

  prop_error(fn, L);

  return X_SUCCESS;
}

/**
 * Creates a field for 1-D array of a given name and type using specified native values.
 * It is like `xCreate1DField()` except that the field is created in serialized form.
 *
 * \param name      Field name
 * \param type      Storage type, e.g. X_INT.
 * \param size      Array size.
 * \param value     Pointer to the native array in memory.
 *
 * \return          A newly created field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreate1DField(const char *name, XType type, int size, const void *value) {
  XField *f = smaxCreateField(name, type, 1, &size, value);
  return f ? f : x_trace_null("smaxCreate1DField", NULL);
}

/**
 * Creates a scalar field of a given name and type using the specified native value.
 * It is like `xCreateScalarField()` except that the field is created in serialized form.
 *
 * \param name      Field name
 * \param type      Storage type, e.g. X_INT.
 * \param value     Pointer to the native data location in memory.
 *
 * \return          A newly created scalar field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreateScalarField(const char *name, XType type, const void *value) {
  XField *f = smaxCreate1DField(name, type, 1, value);
  return f ? f : x_trace_null("smaxCreateScalarField", NULL);
}

/**
 * Creates a field holding a single double-precision value.
 * It is like `xCreateDoubleField()` except that the field is created in serialized form.
 *
 * \param name      Field name
 * \param value     Associated value
 *
 * \return          A newly created field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreateDoubleField(const char *name, double value) {
  XField *f = smaxCreateScalarField(name, X_DOUBLE, &value);
  return f ? f : x_trace_null("smaxCreateDoubleField", NULL);
}

/**
 * Creates a field holding a single wide (64-bit) integer value.
 * It is like `xCreateLongField()` except that the field is created in serialized form.
 *
 * \param name      Field name
 * \param value     Associated value
 *
 * \return          A newly created field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreateLongField(const char *name, long long value) {
  XField *f = smaxCreateScalarField(name, X_LLONG, &value);
  return f ? f : x_trace_null("smaxCreateLongField", NULL);
}

/**
 * Creates a field holding a single integer value.
 *It is like `xCreateIntField()` except that the field is created in serialized form.
 *
 * \param name      Field name
 * \param value     Associated value
 *
 * \return          A newly created field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreateIntField(const char *name, int value) {
  XField *f = smaxCreateScalarField(name, X_INT, &value);
  return f ? f : x_trace_null("smaxCreateLongField", NULL);
}

/**
 * Creates a field holding a single boolean value.
 *It is like `xCreateBooleanField()` except that the field is created in serialized form.
 *
 * \param name      Field name
 * \param value     Associated value
 *
 * \return          A newly created field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreateBooleanField(const char *name, boolean value) {
  XField *f = smaxCreateScalarField(name, X_BOOLEAN, &value);
  return f ? f : x_trace_null("smaxCreateBooleanField", NULL);
}

/**
 * Creates a field holding a single string value.
 * It is like `xCreateStringField()` except that the field is created in serialized form.
 *
 * \param name      Field name
 * \param value     Associated value
 *
 * \return          A newly created field referencing the supplied string, or NULL if there was an error.
 */
XField *smaxCreateStringField(const char *name, const char *value) {
  XField *f = smaxCreateScalarField(name, X_STRING, &value);
  return f ? f : x_trace_null("smaxCreateStringField", NULL);
}


/**
 * Returns the first value in a structure's field as an integer, or the specified default
 * value if there is no such field in the structure, or the content cannot be parse into an integer.
 *
 * @param s                 Pointer to the XStructure.
 * @param name              Field name
 * @param defaultValue      Value to return if no corresponding integer field value.
 * @return                  The (first) field value as a long long, or the default value if there is no such field.
 *
 * @sa xGetField()
 */
boolean smaxGetBooleanField(const XStructure *s, const char *name, boolean defaultValue) {
  boolean b;
  const XField *f = xGetField(s, name);

  if(!f) return defaultValue;

  b = xParseBoolean(f->value, NULL);
  if(b < 0) return defaultValue;
  return b;
}

/**
 * Returns the first value in a structure's field as an integer, or the specified default
 * value if there is no such field in the structure, or the content cannot be parse into an integer.
 *
 * @param s                 Pointer to the XStructure.
 * @param name              Field name
 * @param defaultValue      Value to return if no corresponding integer field value.
 * @return                  The (first) field value as a long long, or the default value if there is no such field.
 *
 * @sa xGetField()
 */
long long smaxGetLongField(const XStructure *s, const char *name, long long defaultValue) {
  int i;
  char *end;
  const XField *f = xGetField(s, name);

  if(!f) return defaultValue;

  errno = 0;
  i = strtol(f->value, &end, 0);
  if(errno) return defaultValue;
  return i;
}

/**
 * Returns the first value in a structure's field as a double precision float, or the specified
 * default value if there is no such field in the structure, or the content cannot be parse into an double.
 *
 * @param s                 Pointer to the XStructure.
 * @param name              Field name
 * @param defaultValue      Value to return if no corresponding integer field value.
 * @return                  The (first) field value as a double, or the specified default if there is no such field.
 *
 * @sa xGetField()
 */
double smaxGetDoubleField(const XStructure *s, const char *name, double defaultValue) {
  double d;
  char *end;
  const XField *f = xGetField(s, name);

  if(!f) return defaultValue;

  errno = 0;
  d = strtod(f->value, &end);
  if(errno) return defaultValue;
  return d;
}

/**
 * Returns the string value in a structure's field, or the specified default value if there is no
 * such field in the structure.
 *
 * @param s                 Pointer to the XStructure.
 * @param name              Field name
 * @param defaultValue      Value to return if no corresponding integer field value.
 * @return                  The field's string (raw) value, or the specified default if there is no such field.
 *
 * @sa xGetField()
 */
char *smaxGetRawField(const XStructure *s, const char *name, char *defaultValue) {
  const XField *f = xGetField(s, name);
  if(!f) return defaultValue;
  return f->value;
}

/**
 * Gets the data of an SMA-X structure field as an array of values of the specified type and element count.
 * The field's data will be truncated or padded with zeroes to provide the requested element count always.
 *
 * @param s             Pointer to SMA-X structure
 * @param name          Field name
 * @param[out] dst      Array to return values in.
 * @param type          Type of data.
 * @param count         Number of elements in return array. The field data will be truncated or padded as necessary.
 * @return              X_SUCCESS (0) if successful, or
 *                      X_STRUCT_INVALID if the input structure is NULL,
 *                      X_NULL if dst is NULL,
 *                      X_SIZE_INVALID if n is 0 or negative,
 *                      X_NAME_INVALID if the structure does not have a field by the specified name,
 *                      or else an error returned by smaxStringtoValues().
 */
int smaxGetArrayField(const XStructure *s, const char *name, void *dst, XType type, int count) {
  static const char *fn = "smaxGetArrayField";

  int pos;
  const XField *f;

  if(!s) return x_error(X_STRUCT_INVALID, EINVAL, fn, "input structure is NULL");
  if(!name) return x_error(X_NAME_INVALID, EINVAL, fn, "field name is NULL");
  if(!name[0]) return x_error(X_NAME_INVALID, EINVAL, fn, "field name is empty");
  if(!dst) return x_error(X_NULL, EINVAL, fn, "output 'dst' buffer is NULL");
  if(count < 1) return x_error(X_SIZE_INVALID, EINVAL, fn, "invalid count: %d", count);

  f = xGetField(s, name);
  if(!f) return X_NAME_INVALID;

  prop_error(fn, smaxStringToValues(f->value, dst, type, count, &pos));

  return X_SUCCESS;
}


