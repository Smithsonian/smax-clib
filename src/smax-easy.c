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
#include <stdarg.h>
#include <math.h>
#include <errno.h>

#include "redisx.h"
#include "smax.h"

// Local prototypes ------------------------------------>
static int WaitOn(const char *table, const char *key, int timeout, ...);


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
  char *ptr;
  *status = smaxPull(table, key, X_RAW, 1, &ptr, meta);
  if(*status) smaxError("smaxPullRaw()", *status);
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
 */
XStructure *smaxPullStruct(const char *id, XMeta *meta, int *status) {
  static const char *funcName = "smaxPullStruct()";
  XStructure *s;

  if(id == NULL) {
    *status = smaxError(funcName, X_NAME_INVALID);
    return NULL;
  }

  s = (XStructure *) calloc(1, sizeof(XStructure));
  *status = smaxPull(id, NULL, X_STRUCT, 1, s, meta);
  if(*status) smaxError(funcName, *status);

  return s;
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
  static const char *funcName = "smaxPullType()";

  int eSize, pos;
  char *raw;
  XMeta m = X_META_INIT;
  void *array;

  eSize = xElementSizeOf(type);
  if(eSize < 1) {
    smaxError(funcName, X_TYPE_INVALID);
    return NULL;
  }

  *n = smaxPull(table, key, X_RAW, 1, &raw, &m);
  if(raw == NULL) {
    if(!*n) *n = X_NULL;
    return NULL;
  }

  if(*n) {
    free(raw);
    smaxError(funcName, *n);
    return NULL;
  }

  if(meta != NULL) *meta = m;

  *n = smaxGetMetaCount(&m);
  if(*n < 1) {
    free(raw);
    return NULL;
  }

  array = calloc(*n, eSize);

  *n = smaxStringToValues(raw, array, type, *n, &pos);

  free(raw);

  if(*n < 0) {
    free(array);
    smaxError(funcName, *n);
    return NULL;
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
  return (int *) smaxPullDynamic(table, key, X_INT, meta, n);
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
  return (long long *) smaxPullDynamic(table, key, X_INT, meta, n);
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
  return (double *) smaxPullDynamic(table, key, X_DOUBLE, meta, n);
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
  if(status) {
    if(str) free(str);
    return NULL;
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
  int i, offset = 0;
  char *str;
  XMeta m = X_META_INIT;
  char **array;

  str = smaxPullRaw(table, key, &m, n);
  if(str == NULL) return NULL;
  if(*n < 0) {
    free(str);
    smaxError("smaxPullStrings()", *n);
    return NULL;
  }

  if(meta != NULL) *meta = m;

  *n = smaxGetMetaCount(&m);
  if(*n < 1) return NULL;

  array = (char **) calloc(*n, sizeof(char *));

  for(i=0; i<(*n); i++) {
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
  int i=0;
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
  long long l=0;
  int status = smaxPull(table, key, X_LONG, 1, &l, NULL);
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
 * Shares a single integer value to SMA-X.
 *
 * \param table     Hash table name.
 * \param key       Variable name under which the data is stored.
 * \param value     Integer value.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * \sa smaxShareHex()
 * @sa smaxShareInts()
 */
int smaxShareInt(const char *table, const char *key, long long value) {
  return smaxShareLongs(table, key, &value, 1);
}


/**
 * Shares a single integer value to SMA-X in a hexadecimal representatin.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param value     Integer value.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * \sa smaxShareInt()
 */
int smaxShareHex(const char *table, const char *key, long long value) {
  return smaxShare(table, key, &value, X_LONG_HEX, 1);
}


/**
 * Shares a single boolean value to SMA-X. All non-zero values are mapped
 * to "1".
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param value     A boolean value.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * \sa smaxShareBooleans()
 */
int smaxShareBoolean(const char *table, const char *key, boolean value) {
  return smaxShareBooleans(table, key, &value, 1);
}

/**
 * Shares a single floating point value to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param value     floating-point value.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * @sa smaxShareDoubles()
 * @sa smaxShareFloats()
 */
int smaxShareDouble(const char *table, const char *key, double value) {
  return smaxShareDoubles(table, key, &value, 1);
}

/**
 * Shares a single string value to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param sValue    Pointer to string.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * @sa smaxShareStrings()
 */
int smaxShareString(const char *table, const char *key, const char *sValue) {
  return smaxShare(table, key, &sValue, X_RAW, 1);
}


/**
 * Shares a binary sequence to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    pointer to the byte buffer.
 * \param n         Number of bytes in buffer to share.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * @sa smaxShareShorts()
 * @sa smaxShareInts()
 * @sa smaxShareLongs()
 * @sa smaxShareInt()
 */
int smaxShareBytes(const char *table, const char *key, const char *values, int n) {
  return smaxShare(table, key, values, X_BYTE, n);
}

/**
 * Shares an array of shorts to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to short[] array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * @sa smaxShareInt()
 * @sa smaxShareBytes()
 * @sa smaxShareInts()
 * @sa smaxShareLongs()
 *
 */
int smaxShareShorts(const char *table, const char *key, const short *values, int n) {
  return smaxShare(table, key, values, X_SHORT, n);
}


/**
 * Shares an array of wide integers to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to long long[] array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * @sa smaxShareInts()
 * @sa smaxShareShorts()
 * @sa smaxShareBytes()
 * @sa smaxShareInt()
 */
int smaxShareLongs(const char *table, const char *key, const long long *values, int n) {
  return smaxShare(table, key, values, X_LONG, n);
}


/**
 * Shares an array of long integers to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to int[] array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * @sa smaxShareLongs()
 * @sa smaxShareShorts()
 * @sa smaxShareBytes()
 * @sa smaxShareInt()
 */
int smaxShareInts(const char *table, const char *key, const int *values, int n) {
  return smaxShare(table, key, values, X_INT, n);
}



/**
 * Shares an array of boolean values to SMA-X. All non-zero values are mapped
 * to "1".
 *
 * \param table     Hash table name.
 * \param key       Variable name under which the data is stored.
 * \param values    Pointer to boolean[] array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * @sa smaxShareBoolean()
 */
int smaxShareBooleans(const char *table, const char *key, const boolean *values, int n) {
  return smaxShare(table, key, values, X_BOOLEAN, n);
}


/**
 * Shares an array of floats to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to float[] array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * @sa smaxShareDouble()
 * @sa smaxShareDoubles()
 */
int smaxShareFloats(const char *table, const char *key, const float *values, int n) {
  return smaxShare(table, key, values, X_FLOAT, n);
}

/**
 * Shares an array of doubles to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param values    Pointer to double[] array.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * @sa smaxShareDouble()
 * @sa smaxShareFloats()
 */
int smaxShareDoubles(const char *table, const char *key, const double *values, int n) {
  return smaxShare(table, key, values, X_DOUBLE, n);
}

/**
 * Shares an array of strings to SMA-X.
 *
 * \param table     The hash table name.
 * \param key       The variable name under which the data is stored.
 * \param sValues   Pointer to array of string pointers.
 * \param n         Number of elements in array to share.
 *
 * \return      X_SUCCESS, or else an appropriate error code from smaxShare().
 *
 * @sa smaxShareString()
 */
int smaxShareStrings(const char *table, const char *key, const char **sValues, int n) {
  char *buf;
  int i, *l, L = 0;

  if(sValues == NULL) return X_NULL;

  l = (int *) calloc(n, sizeof(int));

  for(i=0; i<n; i++) {
    if(sValues[i] == NULL) l[i] = 1;
    else l[i] = strlen(sValues[i]) + 1;
    L += l[i];
  }

  if(L == 0) L = 1;
  buf = (char *) malloc(L);

  L = 0;
  for(i=0; i<n; i++) {
    if(sValues[i] == NULL) *buf = '\0';
    else memcpy(buf, sValues[i], l[i]);
    L += l[i];
  }

  free(l);

  L = smaxShare(table, key, buf, X_RAW, 1);

  free(buf);

  return L;
}


/**
 * Creates a field for 1-D array of a given name and type using specified native values.
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
  return smaxCreateField(name, type, 1, &size, value);
}


/**
 * Creates a scalar field of a given name and type using the specified native value.
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
  return smaxCreate1DField(name, type, 1, value);
}


/**
 * Creates a field holding a single double-precision value.
 *
 * \param name      Field name
 * \param value     Associated value
 *
 * \return          A newly created field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreateDoubleField(const char *name, double value) {
  return smaxCreateScalarField(name, X_DOUBLE, &value);
}

/**
 * Creates a field holding a single wide (64-bit) integer value.
 *
 * \param name      Field name
 * \param value     Associated value
 *
 * \return          A newly created field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreateLongField(const char *name, long long value) {
  return smaxCreateScalarField(name, X_LONG, &value);
}

/**
 * Creates a field holding a single integer value.
 *
 * \param name      Field name
 * \param value     Associated value
 *
 * \return          A newly created field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreateIntField(const char *name, int value) {
  return smaxCreateScalarField(name, X_INT, &value);
}

/**
 * Creates a field holding a single boolean value.
 *
 * \param name      Field name
 * \param value     Associated value
 *
 * \return          A newly created field with the supplied data, or NULL if there was an error.
 *
 * @sa xSetField()
 */
XField *smaxCreateBooleanField(const char *name, boolean value) {
  return smaxCreateScalarField(name, X_BOOLEAN, &value);
}

/**
 * Creates a field holding a single string value.
 *
 * \param name      Field name
 * \param value     Associated value
 *
 * \return          A newly created field referencing the supplied string, or NULL if there was an error.
 */
XField *smaxCreateStringField(const char *name, const char *value) {
  return smaxCreateScalarField(name, X_STRING, &value);
}


/**
 * Gets the data of an SMA-X structure field as an array of values of the specified type and element count.
 * The field's data will be truncated or padded with zeroes to provide the requested element count always.
 *
 * @param s             Pointer to SMA-X structure
 * @param name          Field name
 * @param dst           Array to return values in.
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
  int i, pos;
  const XField *f = xGetField(s, name);

  if(!s) return X_STRUCT_INVALID;
  if(!dst) return X_NULL;
  if(count < 1) return X_SIZE_INVALID;

  if(!f) return X_NAME_INVALID;

  i = smaxStringToValues(f->value, dst, type, count, &pos);
  if(i != X_SUCCESS) return i;

  return X_SUCCESS;
}


/**
 * Waits for a specific pushed entry. There must be an active subscription that includes the specified
 * group & variable, or else the call will block indefinitely.
 *
 * \param table     Hash table name
 * \param key       Variable name to wait on.
 * \param timeout   (s) Timeout value. 0 or negative values result in an indefinite wait.
 *
 * \return      X_SUCCESS (0)       if the variable was updated on some host (or owner).
 *              X_NO_INIT           if the SMA-X sharing was not initialized via smaxConnect().
 *              X_GROUP_INVALID     if the 'group' argument is NULL;
 *              X_NAME_INVALID      if the 'key' argument is NULL.
 *              X_REL_PREMATURE     if smaxReleaseWaits() was called.
 *
 * \sa smaxSubscribe()
 * @sa smaxWaitOnSubscribed()
 * @sa smaxWaitOnSubscribedGroup()
 * @sa smaxWaitOnSubscribedVar()
 * @sa smaxWaitOnAnySubscribed()
 * @sa smaxReleaseWaits()
 */
int smaxWaitOnSubscribed(const char *table, const char *key, int timeout) {
  if(table == NULL) return X_GROUP_INVALID;
  if(key == NULL) return X_NAME_INVALID;
  return WaitOn(table, key, timeout);
}



/**
 * Waits for changes on a specific group. The must be an active subscription including that group, or else the
 * call will block indefinitely.
 *
 * \param[in]  matchTable    Hash table name (e.g. owner ID) to wait on.
 * \param[out] changedKey    Pointer to the string that holds the name of the variable which unblocked the wait
 *                           or which is set to NULL. The lease of the buffer is for the call only. The caller
 *                           should copy its content if persistent storage is required.
 * \param[in] timeout        (s) Timeout value. 0 or negative values result in an indefinite wait.
 *
 * \return      X_SUCCESS (0)       if a variable was updated on the host.
 *              X_NO_INIT           if the SMA-X sharing was not initialized via smaxConnect().
 *              X_HOST_INVALID      if the host (owner ID) is NULL.
 *              X_REL_PREMATURE     if smaxReleaseWaits() was called.
 *
 * \sa smaxSubscribe()
 * @sa smaxWaitOnSubscribedVar()
 * @sa smaxWaitOnSubscribed()
 * @sa smaxWaitOnAnySubscribed()
 * @sa smaxReleaseWaits()
 */
int smaxWaitOnSubscribedGroup(const char *matchTable, char **changedKey, int timeout) {
  if(matchTable == NULL) return X_GROUP_INVALID;
  return WaitOn(matchTable, NULL, timeout, changedKey);
}


/**
 * Waits for a specific pushed variable from any group/table. There must be an active subscription that includes the specified
 * variable in one or more groups/tables, or else the call will block indefinitely.
 *
 * \param[in]  matchKey      Variable name to wait on.
 * \param[out] changedTable  Pointer to the string that holds the name of the table which unblocked the wait
 *                           or which is set to NULL. The lease of the buffer is for the call only. The caller
 *                           should copy its content if persistent storage is required.
 * \param[in] timeout        (s) Timeout value. 0 or negative values result in an indefinite wait.
 *
 * \return      X_SUCCESS (0)       if the variable was updated on some host (or owner).
 *              X_NO_INIT           if the SMA-X sharing was not initialized via smaxConnect().
 *              X_NAME_INVALID      if the 'key' argument is NULL.
 *              X_REL_PREMATURE     if smaxReleaseWaits() was called.
 *
 * \sa smaxSubscribe()
 * @sa smaxWaitOnSubscribedGroup()
 * @sa smaxWaitOnSubscribed()
 * @sa smaxWaitOnAnySubscribed()
 * @sa smaxReleaseWaits()
 */
int smaxWaitOnSubscribedVar(const char *matchKey, char **changedTable, int timeout) {
  if(matchKey == NULL) return X_NAME_INVALID;
  return WaitOn(NULL, matchKey, timeout, changedTable);
}



/**
 * Waits for an update from the specified SMA-X table (optional) and/or specified variable (optional). For example:
 * \code
 *  sma_wait_on("myTable", "myVar");
 * \endcode
 * will wait until "myVar" is changed in "myTable".
 * \code
 *  char *fromTable;
 *  sma_wait_on(NULL, "myVar", &fromTable);
 * \endcode
 * will wait until "myVar" is published to any SMA-X table. The triggering table name will be stored in the supplied 3rd argument.
 * \code
 *  char *changedKey;
 *  sma_wait_on("myTable", NULL, &changedKey);
 * \endcode
 * will wait until any field is changed in "myTable". The triggering variable name will be store in the supplied 3rd argument.
 *
 * \param[in]  host      Host name on which to wait for updates, or NULL if any host.
 * \param[in]  key       Variable name to wait to be updated, or NULL if any variable.
 * \param[in]  timeout   (s) Timeout value. 0 or negative values result in an indefinite wait.
 * \param[out] ...       References to string pointers (char **) to which the triggering table name (if table is NULL) and/or
 *                       variable name (if key is NULL) is/are returned. These buffers have a lease for the call only.
 *                       The caller should copy their content if persistent storage is required.
 *
 * \return      X_SUCCESS (0) if successful, or else the error returned by smaxWaitOnAnySubscribed()
 *
 * \sa smaxWaitOnAnySubscribed()
 * @sa smaxReleaseWaits()
 */
static int WaitOn(const char *table, const char *key, int timeout, ...) {
  const static char *funcName = "WaitOn";
  char *gotTable, *gotKey;
  va_list args;

  va_start(args, timeout);         /* Initialize the argument list. */

  while(TRUE) {
    int status;
    char **ptr;

    status = smaxWaitOnAnySubscribed(&gotTable, &gotKey, timeout);
    if(status) {
      va_end(args);
      return status;
    }

    if(table != NULL) {
      if(!gotTable) {
        smaxError(funcName, X_NULL);
        continue;
      }
      if(strcmp(gotTable, table)) continue;
    }
    if(key != NULL) {
      if(!gotKey) {
        smaxError(funcName, X_NULL);
        continue;
      }
      if(strcmp(gotKey, key)) continue;
    }

    if(table == NULL) {
      ptr = va_arg(args, char **);
      *ptr = gotTable;
    }
    if(key == NULL) {
      ptr = va_arg(args, char **);
      *ptr = gotKey;
    }

    if(table == NULL || key == NULL) va_end(args);

    return X_SUCCESS;
  }
}




/**
 * Returns the first value in a structure's field as an integer, or the specified default
 * value if there is no such fiield in the structure, or the content cannot be parse into an integer.
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

  i = strtol(f->value, &end, 0);
  if(end == f->value) return defaultValue;
  if(errno == ERANGE) return defaultValue;
  return i;
}


/**
 * Returns the first value in a structure's field as a double precision float, or the specified
 * default value if there is no such fiield in the structure, or the content cannot be parse into an double.
 *
 * @param s                 Pointer to the XStructure.
 * @param name              Field name
 * @param defaultValue      Value to return if no corresponding integer field value.
 * @return                  The (first) field value as a double, or the specified default if there is no such field.
 *
 * @sa xGetField
 */
double smaxGetDoubleField(const XStructure *s, const char *name, double defaultValue) {
  double d;
  char *end;
  const XField *f = xGetField(s, name);

  if(!f) return defaultValue;

  d = strtod(f->value, &end);
  if(end == f->value) return defaultValue;
  if(errno == ERANGE) return defaultValue;
  return d;
}


/**
 * Returns the string value in a structure's field, or the specified default value if there is no
 * such fiield in the structure.
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
 * Sets the a field in a structure to a scalar (single) value. If there was a prior field that is
 * replaced, then the pointer to the prior field is returned.
 *
 * @param s                 Pointer to the XStructure.
 * @param name              Field name
 * @param type              Data type, such as X_INT (see xchange.h).
 * @param value             Value to return if no corresponding integer field value.
 *
 * @return                  Pointer to the prior XField by or NULL if new or there was an error.
 *
 * @sa smaxSet1DField()
 * @sa smaxCreateField()
 * @sa xSetField()
 *
 */
XField *smaxSetScalarField(XStructure *s, const char *name, XType type, const void *value) {
  XField *f;

  if(!s) return NULL;
  if(!name) return NULL;
  if(!name[0]) return NULL;

  f = smaxCreateField(name, type, 0, NULL, value);
  if(!f) return NULL;

  return xSetField(s, f);
}


/**
 * Sets the a field in a structure to the contents of a 1D array. If there was a prior field that is
 * replaced, then the pointer to the prior field is returned.
 *
 * @param s                 Pointer to the XStructure.
 * @param name              Field name
 * @param type              Data type, such as X_INT (see xchange.h).
 * @param n                 Number of elements
 * @param value             Value to return if no corresponding integer field value.
 *
 * @return                  Pointer to the prior XField by or NULL if new or there was an error.
 *
 * @sa smaxSetScalarField()
 * @sa smaxCreateField()
 * @sa xSetField()
 *
 */
XField *smaxSet1DField(XStructure *s, const char *name, XType type, int n, const void *value) {
  XField *f;

  if(!s) return NULL;
  if(!name) return NULL;
  if(!name[0]) return NULL;

  f = smaxCreateField(name, type, 1, &n, value);
  if(!f) return NULL;

  return xSetField(s, f);
}


