/**
 * \file
 *
 * \date Mar 24, 2020
 * \author Attila Kovacs
 *
 * \brief
 *      A set of utility functions for manipulating optional static metadata.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include "smax-private.h"

/**
 * Adds/updates metadata associated with an SMA-X variable. The data will be pushed via the
 * Redis pipeline channel.
 *
 * \param meta      Root meta table name, usually something like "<metaname>".
 * \param table     Hash table name.
 * \param key       Variable / field name in table.
 * \param value     Metadata string value.
 *
 * \return          X_SUCCESS (0)       if the metadata was successfully retrieved
 *                  X_INCOMPLETE        if the meatdata was successfully written but an update notification was not sent
 *                  or else the return value of redisxSetValue()
 *
 * \sa smaxPullMeta(), redisxSetValue()
 */
int smaxPushMeta(const char *meta, const char *table, const char *key, const char *value) {
  static const char *fn = "smaxPushMeta";

  int status;
  Redis *redis = smaxGetRedis();
  char *var, *channel;

  if(meta == NULL) return x_error(X_GROUP_INVALID, EINVAL, fn, "input 'meta' is NULL");
  if(!meta[0]) return x_error(X_GROUP_INVALID, EINVAL, fn, "input 'meta' is empty");
  if(value == NULL) return x_error(X_NULL, EINVAL, fn, "int value is NULL");

  if(redis == NULL) return smaxError(fn, X_NO_INIT);

  var = xGetAggregateID(table, key);
  if(var == NULL) return x_trace(fn, NULL, X_NULL);

  // Use the interactive channel to ensure the notification
  // strictly follows the update itself. The extra metadata should
  // never be a high-cadence update anyway...
  status = redisxSetValue(redis, meta, var, value, FALSE);

  // Try to send out an update notification...
  channel = smaxGetUpdateChannelPattern(meta, var);

  free(var);

  if(channel == NULL) return x_trace(fn, NULL, X_INCOMPLETE);

  if(!status) status = redisxNotify(redis, channel, smaxGetProgramID());
  free(channel);

  return status ? x_trace(fn, NULL, X_INCOMPLETE) : X_SUCCESS;
}

/**
 * Retrieves a metadata string value for a given variable from the database
 *
 * \param meta      Root meta table name, usually something like "<metaname>".
 * \param table     Hash table name.
 * \param key       Variable / field name in table.
 * \param status    Pointer to int in which to return a X_SUCCESS or an error code.
 *
 * \return          The string metadata value or NULL.
 *
 * \sa setPushMeta()
 */
char *smaxPullMeta(const char *meta, const char *table, const char *key, int *status) {
  static const char *fn = "smaxPullMeta";

  Redis *redis = smaxGetRedis();
  char *var, *value;

  if(meta == NULL) {
    x_error(X_GROUP_INVALID, EINVAL, fn, "meta name is NULL");
    return NULL;
  }

  if(!meta[0]) {
    x_error(X_GROUP_INVALID, EINVAL, fn, "meta name is empty");
    return NULL;
  }

  if(redis == NULL) {
    smaxError(fn, X_NO_INIT);
    return NULL;
  }

  var = xGetAggregateID(table, key);
  if(var == NULL) return x_trace_null(fn, NULL);

  value = redisxGetStringValue(redis, meta, var, status);
  free(var);

  if(status) x_trace_null(fn, NULL);

  return value;
}

/**
 * Retrieves the timestamp for a given variable from the database.
 *
 * \param[in]  table     Hash table name (or NULL if key is an aggregate ID).
 * \param[in]  key       Variable / field name in table.
 *
 * \return          (s) UNIX timestamp, as fractional seconds since 1 Jan 1970, or
 *                  NAN if there was an error.
 *
 * \sa setPushMeta()
 */
double smaxPullTime(const char *table, const char *key) {
  static const char *fn = "smaxPullTime";

  int status = X_SUCCESS;
  char *str = smaxPullMeta(SMAX_TIMESTAMPS, table, key, &status);
  double ts;

  if(status) {
    x_trace_null(fn, NULL);
    if(str) free(str);
    return NAN;
  }

  if(!str) return NAN;

  errno = 0;
  ts = strtod(str, NULL);
  if(errno) {
    x_error(0, errno, fn, "invalid time: %s", str);
    return NAN;
  }
  free(str);

  return ts;
}

/**
 * Retrieves the timestamp for a given variable from the database.
 *
 * \param[in]  table     Hash table name (or NULL if key is an aggregate ID).
 * \param[in]  key       Variable / field name in table.
 * \param[out] ndim      Pointer to integer in which to return the dimensionality of the variable,
 *                       or NULL if not requested.
 * \param[out] sizes     Array to store sizes along each dimension, which should hold X_MAX_DIMS
 *                       integers, or NULL if dimensions are not requested.
 * \return               Type of data stored under the specified table/key ID.
 *
 * \sa setPushMeta()
 */
XType smaxPullTypeDimension(const char *table, const char *key, int *ndim, int *sizes) {
  static const char *fn = "smaxPullTYpeDimension";

  XType type;
  int status = X_SUCCESS;
  char *str = smaxPullMeta(SMAX_TYPES, table, key, &status);

  if(status) {
    type = x_trace(fn, NULL, X_UNKNOWN);
  }
  else {
    type = smaxTypeForString(str);
    if(type == X_UNKNOWN) x_trace(fn, NULL, X_UNKNOWN);
  }

  if(str) free(str);

  if(ndim && sizes) {
    str = smaxPullMeta(SMAX_DIMS, table, key, &status);
    if(status) *ndim = *sizes = 0;
    else *ndim = xParseDims(str, sizes);
    if(str) free(str);
  }

  return type;
}

/**
 * Sets the static description for a given SMA-X variable.
 *
 * \param table         Hash table name.
 * \param key           Variable / field name in table.
 * \param description   Concise but descriptive summary of the meaning of the variable.
 *
 * \return          X_SUCCESS (0)       If successful
 *                  or else the return value of smaxPushMeta()
 *
 * \sa smaxSetDescription(), smaxPushMeta()
 */
int smaxSetDescription(const char *table, const char *key, const char *description) {
  prop_error("smaxSetDescription", smaxPushMeta(META_DESCRIPTION, table, key, description));
  return X_SUCCESS;
}

/**
 * Returns a concise description of a variable.
 *
 * \param table     Hash table name.
 * \param key       Variable / field name in table.
 *
 * \return          Variable description or NULL or empty string if the variable has no description assiciated with it.
 *
 * \sa smaxSetDescription()
 */
char *smaxGetDescription(const char *table, const char *key) {
  int status = X_SUCCESS;
  char *desc = smaxPullMeta(META_DESCRIPTION, table, key, &status);
  if(status) x_trace_null("smaxGetDescription", NULL);
  return desc;
}

/**
 * Sets the physical unit name for a given SMA-X variable.
 *
 * \param table     Hash table name.
 * \param key       Variable / field name in table.
 * \param unit      Standard unit specification, e.g. "W / Hz" or "W Hz**{-1}".
 *
 * \return          X_SUCCESS (0)       If successful
 *                  or else the return value of smaxPushMeta()
 *
 * \sa smaxGetUnits(), smaxPushMeta()
 */
int smaxSetUnits(const char *table, const char *key, const char *unit) {
  prop_error("smaxSetUnits", smaxPushMeta(META_UNIT, table, key, unit));
  return X_SUCCESS;
}

/**
 * Returns the physical unit name, if any, for the given variable.
 *
 * \param table     Hash table name.
 * \param key       Variable / field name in table.
 *
 * \return          Unit name (e.g. "W / Hz"), or NULL or empty string  if the variable has no designated physical unit.
 *
 * \sa smaxSetUnits()
 */
char *smaxGetUnits(const char *table, const char *key) {
  int status = X_SUCCESS;
  char *unit = smaxPullMeta(META_UNIT, table, key, &status);
  if(status) x_trace_null("smaxGetUnits", NULL);
  return unit;
}

/**
 * Defines the n'th coordinate axis for a given SMA-X coordinate system table id.
 *
 * \param id        Fully qualified SMA-X coordinate system ID.
 * \param n         The (0-based) index of the coordinate axis
 * \param axis      Pointer to the structure describing the coordinate axis.
 *
 * \return          X_SUCCESS (0)   if the coordinate axis was successfully set in the database.
 *                  or else the return value of redisxMultiSet().
 *
 * \sa smaxSetCoordinateAxis(), redisxMultiSet()
 *
 */
int smaxSetCoordinateAxis(const char *id, int n, const XCoordinateAxis *axis) {
  static const char *fn = "smaxSetCoordinateAxis";

  RedisEntry fields[5];
  char cidx[30], ridx[30], rval[30], step[30];
  int status;

  sprintf(cidx, "%d", n+1);
  id = xGetAggregateID(id, cidx);
  if(!id) return x_trace(fn, NULL, X_FAILURE);

  sprintf(ridx, "%g", axis->refIndex);
  sprintf(rval, "%g", axis->refValue);
  sprintf(step, "%g", axis->step);

  fields[0].key = "name";
  fields[0].value = axis->name ? axis->name : "";

  fields[1].key = "unit";
  fields[1].value = axis->unit ? axis->unit : "";

  fields[2].key = "refIndex";
  fields[2].value = ridx;

  fields[3].key = "refValue";
  fields[3].value = rval;

  fields[4].key = "step";
  fields[4].value = step;

  status = redisxMultiSet(smaxGetRedis(), id, fields, 5, FALSE);
  free((char *) id);

  prop_error(fn, status);
  return X_SUCCESS;
}

/**
 * Returns the n'th coordinate axis for a given SMA-X coordinate system table id.
 *
 * \param id        Fully qualified SMA-X coordinate system ID.
 * \param n         The (0-based) index of the coordinate axis
 *
 * \return          Pointer to a newly allocated XCoordinateAxis structure or NULL if
 *                  the axis is undefined, or could not be retrieved from the database.
 *
 * \sa smaxSetCoordinateAxis()
 *
 */
XCoordinateAxis *smaxGetCoordinateAxis(const char *id, int n) {
  static const char *fn = "smaxGetCoordinateAxis";

  Redis *r = smaxGetRedis();
  RedisEntry *fields;
  XCoordinateAxis *axis;
  char *axisName, idx[20];
  int i;

  if(!r) {
    smaxError(fn, X_NO_INIT);
    return NULL;
  }

  if(n < 0) {
    x_error(0, EINVAL, fn, "invalid coordinate index: %d", n);
    return NULL;
  }

  sprintf(idx, "%d", (n+1));
  axisName = xGetAggregateID(id, idx);
  if(!axisName) return x_trace_null(fn, NULL);

  fields = redisxGetTable(r, axisName, &n);
  free(axisName);

  if(n <= 0) {
    if(fields) free(fields);
    return x_trace_null(fn, NULL);
  }

  if(fields == NULL) return x_trace_null(fn, NULL);

  axis = (XCoordinateAxis *) calloc(1, sizeof(XCoordinateAxis));
  x_check_alloc(axis);

  axis->step = 1.0;

  for(i=0; i<n; i++) {
    RedisEntry *f = &fields[i];

    if(strcmp(f->key, "name")) {
      axis->name = f->value;
      f->value = NULL;
    }

    if(strcmp(f->key, "unit")) {
      axis->unit = f->value;
      f->value = NULL;
    }

    if(f->value == NULL)
      fprintf(stderr, "WARNING! (nil) value for %s in database. Skipping.\n", f->key);

    else if(strcmp(f->key, "refIndex")) {
      errno = 0;
      axis->refIndex = strtod(f->value, NULL);
      if(errno)
        fprintf(stderr, "WARNING! Invalid coordinate refIndex '%s' in database. Assuming %g\n", f->value, axis->refIndex);
    }

    else if(strcmp(f->key, "refValue")) {
      errno = 0;
      axis->refValue = strtod(f->value,NULL);
      if(errno)
        fprintf(stderr, "WARNING! Invalid coordinate refValue '%s' in database. Assuming %g\n", f->value, axis->refValue);
    }

    else if(strcmp(f->key, "step")) {
      errno = 0;
      axis->step = strtod(f->value, NULL);
      if(errno || axis->step == 0.0) {
        axis->step = 1.0;
        fprintf(stderr, "WARNING! Invalid coordinate step '%s' in database. Assuming %g\n", f->value, axis->step);
      }
    }
  }

  return axis;
}

/**
 * Sets the coordinate system metadata for data in the database.
 *
 * \param table     Hash table name.
 * \param key       Variable / field name in table.
 * \param coords    Pointer to the coordinate system structure associated to this variable.
 *
 * \return          X_SUCCESS (0)       if the coordinate system was successfully sent to SMA-X
 *                  or else the first error encountered by xSetCoordinateAxis()
 *
 * \sa smaxGetCoordinateSystem()
 * \sa smaxSetCoordinateAxis()
 */
int smaxSetCoordinateSystem(const char *table, const char *key, const XCoordinateSystem *coords) {
  static const char *fn = "smaxSetCoordinateSystem";

  int i;
  int firstError = 0;
  char *var, *id;

  var = xGetAggregateID(table, key);
  if(!var) return x_trace(fn, NULL, X_NULL);

  id = xGetAggregateID(META_COORDS, var);
  free(var);

  if(!id) return x_trace(fn, NULL, X_NULL);

  for(i=0; i<coords->nAxis; i++) {
    int status = smaxSetCoordinateAxis(id, i, &coords->axis[i]);
    if(status) if(!firstError) firstError = status;
  }

  free(id);

  prop_error(fn, firstError);

  return X_SUCCESS;
}

/**
 * Returns the coordinate system, if any, associated to a given SMA-X variable.
 *
 * \param table     Hash table name.
 * \param key       Variable / field name in table.
 *
 * \return          A newly allocated coordinate system structure, or NULL.
 *
 * \sa smaxSetCoordinateSystem()
 * \sa smaxGetCoordinateAxis()
 */
XCoordinateSystem *smaxGetCoordinateSystem(const char *table, const char *key) {
  static const char *fn = "smaxGetCoordinateSystem";

  XCoordinateSystem *s;
  XCoordinateAxis *a[X_MAX_DIMS];
  char *var, *id;
  int n;

  var = xGetAggregateID(table, key);
  if(!var) return x_trace_null(fn, NULL);

  id = xGetAggregateID(META_COORDS, var);
  free(var);
  if(!id) return x_trace_null(fn, NULL);

  for(n=0; n<X_MAX_DIMS; n++) {
    a[n] = smaxGetCoordinateAxis(id, n);
    if(a[n] == NULL) {
      x_trace_null(fn, NULL);
      break;
    }
  }

  free(id);

  if(n == 0) return NULL;

  s = (XCoordinateSystem *) calloc(1, sizeof(XCoordinateSystem));
  x_check_alloc(s);

  s->nAxis = n;
  s->axis = (XCoordinateAxis *) calloc(n, sizeof(XCoordinateAxis));
  x_check_alloc(s->axis);

  while(--n >= 0) s->axis[n] = *a[n];

  return s;
}

/**
 * Creates a coordinate system with the desired dimension, and standard Cartesian coordinates
 * with no labels, or units specified (NULL).
 *
 * \param nAxis     Dimension of the coordiante system, i.e. number of axes.
 *
 * \return          Pointer to the new coordinate system structure, or NULL if the coordiate
 *                  system could not be created as specified.
 *
 * \sa smaxDestroyCoordinateSystem()
 */
XCoordinateSystem *smaxCreateCoordinateSystem(int nAxis) {
  XCoordinateSystem *coords;

  if(nAxis <= 0 || nAxis > X_MAX_DIMS) {
    x_error(0, EINVAL, "smaxCreateCoordinateSystem", "invalid dimension: %d", nAxis);
    return NULL;
  }

  coords = (XCoordinateSystem *) calloc(1, sizeof(XCoordinateSystem));
  x_check_alloc(coords);

  coords->axis = (XCoordinateAxis *) calloc(nAxis, sizeof(XCoordinateAxis));
  x_check_alloc(coords->axis);

  coords->nAxis = nAxis;

  while(--nAxis >= 0) {
    XCoordinateAxis *a = &coords->axis[nAxis];
    a->step = 1.0;
  }

  return coords;
}

/**
 * Deallocates a coordinate system structure.
 *
 * \param coords        Pointer to the coordinate system to discard.
 *
 * \sa smaxCreateCoordinateSystem()
 *
 */
void smaxDestroyCoordinateSystem(XCoordinateSystem *coords) {
  if(!coords) return;
  if(coords->axis) free(coords->axis);
  free(coords);
}
