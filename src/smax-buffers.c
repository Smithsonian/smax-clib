/**
 * @file
 *
 * @date Created  on Jul 30, 2021
 * @author Attila Kovacs
 *
 * @brief   A set of functions to provide interpolated values, sums, and averages etc., from any numerical SMA-X data.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <pthread.h>

#include "smax.h"
#include "smax-private.h"

#define INITIAL_BUFFERS 16      /// Initial storage size for buffers

#ifndef INFINITY
#define INFINITY HUGE_VAL
#endif

typedef struct {
  XMeta meta;
  Entry *entry;
  int bufferIndex;
  int bufferID;
} Incoming;


static Buffer *lookup[SMAX_LOOKUP_SIZE];
static Buffer **buffers;
static int firstBufferID;
static int nBuffers;
static int capacity;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


static void ProcessUpdate(const char *pattern, const char *channel, const char *msg, int len);


static int GetFloorIndexAsync(const Buffer *p, double t) {
  int i, step;

  if(!p) return -1;
  if(!p->n) return -1;

  if(p->entries[p->firstIndex].t > t) return -1;

  step = p->n >> 1;

  for(i = p->firstIndex; step >= 1; step >>= 1) if(p->entries[(i+step) % p->size].t < t) i += step;
  return i % p->size;
}

static int GetCeilIndexAsync(const Buffer *p, double t) {
  int i, step;

  if(!p) return -1;
  if(!p->n) return -1;

  i = p->firstIndex + p->n - 1;

  if(p->entries[i % p->size].t < t) return -1;

  step = p->n >> 1;

  for(; step >= 1; step >>= 1) if(p->entries[(i-step) % p->size].t > t) i -= step;
  return i % p->size;
}


static int GetIndexRangeAsync(const Buffer *p, double fromt, double tot, int *fromi, int *toi) {
  int status = X_SUCCESS;

  *fromi = GetFloorIndexAsync(p, fromt);
  if(*fromi < 0) {
    *fromi = p->firstIndex;
    status = X_INCOMPLETE;
  }

  *toi = GetCeilIndexAsync(p, tot);
  if(*toi < 0) {
    *toi = (p->firstIndex + p->n);
    status = X_INCOMPLETE;
  }
  else if(*toi < *fromi) *toi += p->size;

  return status;
}


static int GetInterpolatedAsync(Buffer *p, double t, double *result) {
  Entry *prev, *next;
  const double *rates;
  double dT, dt;
  int i, n;

  i = GetFloorIndexAsync(p, t);
  if(i < 0) return X_INCOMPLETE;

  // Make sure there is a 'next' datum.
  n = (i + 1) - p->firstIndex;
  if(n < 0) n += p->size;
  if(n >= p->n) return X_INCOMPLETE;

  rates = (double *) calloc(p->count, sizeof(double));
  if(!rates) return X_FAILURE;

  prev = &p->entries[i];
  next = &p->entries[(i + 1) % p->size];

  dT = next->t - prev->t;

  dt = t - prev->t;
  for(i=p->count; --i >= 0; ) result[i] = prev->values[i] + (next->values[i] - prev->values[i]) * dt / dT;

  return X_SUCCESS;
}


static void GetSumAsync(Buffer *p, int fromi, int toi, double *sum) {
  int i;

  if(toi < fromi) toi += p->size;
  memset(sum, 0, p->count * sizeof(double));

  for(i = fromi; i < toi; i++) {
    Entry *e = &p->entries[i % p->size];
    int k;
    for(k=p->count; --k >= 0; ) sum[k] += e->values[k];
  }
}


static void GetSquareSumAsync(Buffer *p, int fromi, int toi, double *sum2) {
  int i;

  if(toi < fromi) toi += p->size;
  memset(sum2, 0, p->count * sizeof(double));

  for(i = fromi; i < toi; i++) {
    Entry *e = &p->entries[i % p->size];
    int k;
    for(k=p->count; --k >= 0; ) sum2[k] += e->values[k] * e->values[k];
  }
}

static void GetAverageAsync(Buffer *p, int fromi, int toi, double *mean, double *rms) {
  int i, n;

  if(toi < fromi) toi += p->size;
  n = toi - fromi;

  if(rms) for(i=p->count; --i >= 0; ) rms[i] = NAN;

  GetSumAsync(p, fromi, toi, mean);

  for(i=p->count; --i >= 0; ) mean[i] /= n;

  if(rms && n > 1) {
    GetSquareSumAsync(p, fromi, toi, rms);
    for(i=p->count; --i >= 0; ) rms[i] = sqrt(rms[i] - mean[i] * mean[i]) / (n-1);
  }
}


static int GetRangeAsync(Buffer *p, int fromi, int toi, double *min, double *max) {
  int i, status = X_SUCCESS;

  if(!min && !max) return X_SUCCESS; // Nothing to do...

  if(min) for(i=p->count; --i >= 0; ) min[i] = INFINITY;
  if(max) for(i=p->count; --i >= 0; ) max[i] = -INFINITY;

  if(!p->n) return X_INCOMPLETE;

  for(i = fromi; i < toi; i++) {
    Entry *e = &p->entries[i % p->size];
    int k;
    for(k=p->count; --k >= 0; ) {
      if(min) if(e->values[k] < min[k]) min[k] = e->values[k];
      if(max) if(e->values[k] > max[k]) max[k] = e->values[k];
    }
  }

  return status;
}


static void AddEntry(Buffer *p, Entry *e) {
  if(!e) return;

  pthread_mutex_lock(&p->mutex);

  if(p->n < p->size) {
    p->entries[(p->firstIndex + p->n) % p->size] = *e;
    p->n++;
  }
  else {
    p->entries[p->firstIndex] = *e;
    if(++p->firstIndex >= p->size) p->firstIndex = 0;
  }

  pthread_mutex_unlock(&p->mutex);

  free(e);
}

/**
 * Returns the hash lookup index for a given update channel. It is the same index as what xGetHashLookupIndex()
 * would return for the variable that has been updated in the given notification channel
 *
 * \param channel       The redis notification channel (string) for the update.
 *
 * \return              An integer (0-255) just like for xGetHashLookupIndex().
 *
 * \sa xGetHashLookupIndex()
 *
 */
static int GetChannelLookupIndex(const char *channel) {
  int lGroup;
  char *key;

  // Skip the table update prefix for the channel...
  if(!strncmp(channel, SMAX_UPDATES, SMAX_UPDATES_LENGTH)) channel += SMAX_UPDATES_LENGTH;

  key = xLastSeparator(channel);
  if(!key) return 0;

  lGroup = key - channel;
  key += X_SEP_LENGTH;
  return smaxGetHashLookupIndex(lGroup ? channel : NULL, lGroup, key, 0);
}


static __inline__ int GetBufferLookupIndex(const Buffer *p) {
  return smaxGetHashLookupIndex(p->table, 0, p->key, 0);
}

static int AddBufferAsync(Buffer *p) {
  static int serial;
  int i;

  if(!p) return X_NULL;

  p->channel = NULL;

  if(!capacity) {
    capacity = INITIAL_BUFFERS;
    buffers = (Buffer **) calloc(capacity, sizeof(Buffer *));
    if(!buffers) return X_FAILURE;

    p->channel = xGetAggregateID(p->table, p->key);
    smaxAddSubscriber("", ProcessUpdate);
  }

  if(nBuffers >= capacity) {
    Buffer **old = buffers;
    int newcap = 2 * capacity;
    buffers = (Buffer **) realloc(buffers, newcap * sizeof(Buffer *));
    if(!buffers) {
      free(old);
      return X_FAILURE;
    }
    memset(&buffers[capacity], 0, (newcap - capacity) * sizeof(Buffer *));
    capacity = newcap;
  }

  p->id = ++serial;
  buffers[nBuffers++] = p;

  // Add to lookup table.
  i = GetBufferLookupIndex(p);
  p->next = lookup[i];
  lookup[i] = p;

  smaxSubscribe(p->table, p->key);

  return X_SUCCESS;
}

static Buffer *FindBufferAsync(const char *table, const char *key) {
  Buffer *p = lookup[smaxGetHashLookupIndex(table, 0, key, 0)];
  for(; p != NULL; p = p->next) if(!strcmp(p->table, table)) if(!strcmp(p->key, key)) return p;
  return NULL;
}

static void ClearBufferAsync(Buffer *p) {
  if(!p) return;
  p->firstIndex = p->n = 0;
}

static void DestroyBuffer(Buffer *p) {
  if(!p) return;

  smaxUnsubscribe(p->table, p->key);

  pthread_mutex_lock(&p->mutex);

  ClearBufferAsync(p);
  if(p->channel) free(p->channel);
  if(p->table) free(p->table);
  if(p->key) free(p->key);

  pthread_mutex_unlock(&p->mutex);
  pthread_mutex_destroy(&p->mutex);

  free(p);
}

static void ProcessIncoming(char *arg) {
  Incoming *in = (Incoming *) arg;
  Buffer *p;

  if(!in) return;

  pthread_mutex_lock(&mutex);

  if(in->bufferIndex >= nBuffers) {
    pthread_mutex_unlock(&mutex);
    return;
  }

  p = buffers[in->bufferIndex];
  if(in->bufferID != p->id) {
    pthread_mutex_unlock(&mutex);
    return;
  }

  in->entry->t = in->meta.timestamp.tv_sec + 1e-9 * in->meta.timestamp.tv_nsec;

  AddEntry(p, in->entry);

  pthread_mutex_unlock(&mutex);

  free(in);
}

static void ProcessUpdate(const char *pattern, const char *channel, const char *msg, int len) {
  Buffer *p = lookup[GetChannelLookupIndex(channel)];

  pthread_mutex_lock(&mutex);

  for( ; p != NULL; p = p->next) if(!strcmp(channel, p->channel)) {
    Incoming *in;
    Entry *entry = (Entry *) calloc(1, sizeof(Entry));

    if(!entry) break;

    entry->values = (double *) calloc(p->count, sizeof(double));
    if(!entry->values) {
      free(entry);
      break;
    }

    in = (Incoming *) calloc(1, sizeof(Incoming));
    if(!in) {
      free(entry->values);
      free(entry);
      break;
    }

    in->bufferIndex = p->id - firstBufferID;
    in->bufferID = p->id;
    in->entry = entry;

    smaxQueue(p->table, p->key, X_DOUBLE, p->count, entry->values, &in->meta);
    smaxQueueCallback(ProcessIncoming, (char *) in);

    break;
  }

  pthread_mutex_unlock(&mutex);
}


static Buffer *GetBufferAsync(int id) {
  if(id < firstBufferID) return NULL;
  if(id > firstBufferID + nBuffers) return NULL;
  return buffers[id - firstBufferID];
}


/**
 * Starts buffering an SMA-X variable, e.g. for calculating interpolated values; sums, averages, or
 * min/max over time windows. The SMA-X variable must be a numberical type (any integer or float type)
 * with any number of elements.
 *
 * @param table         Redis hash table name
 * @param key           Field name
 * @param count         Number of values we will be requesting for this variable.
 * @param lookbackTime  (s) Maximum time range of data to hold in store. Old data will be
 *                      discarded as appropriate when new measurements become available.
 *
 * @return              A unique buffer ID (>= 0), that shall be used for accessing data in the buffer,
 *                      or else an error code (< 0), such as:
 *
 *                        X_NAME_INVALID if they key argument is NULL,
 *                        X_SIZE_INVALID if the count and/or lookbackTime argument is 0 or negative,
 *                        X_TYPE_INVALID if the SMA-X data for the specified table/key is non-numerical.
 *                        X_FAILURE if the buffer could not be allocated.
 *
 * @sa smaxFlushBuffer()
 * @sa smaxEndBuffers()
 * @sa smaxGetWindowAverage()
 * @sa smaxGetWindowSum()
 * @sa smaxGetWindowRange()
 * @sa smaxGetInterpolated()
 * @sa smaxGetBufferedRange()
 * @sa smaxGetBufferSize()
 */
int smaxBufferData(const char *table, const char *key, int count, int size) {
  Buffer *p;

  if(!key) return X_NAME_INVALID;
  if(count <= 0) return X_SIZE_INVALID;
  if(size <= 0) return X_SIZE_INVALID;

  // Check to make sure SMA-X has numerical data under the specified table/key
  switch(smaxPullTypeDimension(table, key, NULL, NULL)) {
    case X_BYTE:
    case X_SHORT:
    case X_INT:
    case X_LONG:
    case X_FLOAT:
    case X_DOUBLE: break;
    default: return X_TYPE_INVALID;
  }

  pthread_mutex_lock(&mutex);

  // check existing buffer and update as needed.
  p = FindBufferAsync(table, key);

  if(!p) {
    p = (Buffer *) calloc(1, sizeof(Buffer));
    if(!p) {
      pthread_mutex_unlock(&mutex);
      return X_FAILURE;
    }

    pthread_mutex_init(&p->mutex, NULL);
    pthread_mutex_lock(&p->mutex);

    AddBufferAsync(p);
    pthread_mutex_unlock(&mutex);

    p->table = xStringCopyOf(table);
    p->key = xStringCopyOf(key);
    p->entries = (Entry *) calloc(size, sizeof(Entry));
  }
  else if(size > p->size) {
    pthread_mutex_lock(&p->mutex);
    pthread_mutex_unlock(&mutex);

    p->entries = (Entry *) realloc(p->entries, size * sizeof(Entry));
    if(count > p->count) ClearBufferAsync(p);   // ... If count increases wipe buffer
  }

  if(!p->entries) {
    p->size = 0;
    pthread_mutex_unlock(&p->mutex);
    return X_FAILURE;
  }

  p->size = size;
  p->count = count;
  pthread_mutex_unlock(&p->mutex);

  return p->id;
}


/**
 * Stops local buffering of SMA-X data, flushing all buffered data and discarding all buffer
 * resources.
 *
 * @return  X_SUCCESS (0).
 *
 * @sa smaxBufferData()
 */
int smaxEndBuffers() {
  int n;
  Buffer **list;

  smaxRemoveSubscribers(ProcessUpdate);

  pthread_mutex_lock(&mutex);
  list = buffers;
  n = firstBufferID = nBuffers;

  buffers = NULL;
  nBuffers = capacity = 0;

  memset(lookup, 0, SMAX_LOOKUP_SIZE * sizeof(Buffer *));

  pthread_mutex_unlock(&mutex);

  while(--n >= 0) DestroyBuffer(list[n]);
  if(list) free(list);

  return X_SUCCESS;
}

/**
 * Flushes all existing data from the specified data buffer.
 *
 * @param id    Buffer ID, as returned by smaxBufferData().
 * @return      X_SUCCESS (0) if successful or else X_NAME_INVALID if there is no buffer currently with the specified ID.
 *
 * @sa smaxBufferData()
 */
int smaxFlushBuffer(int id) {
  Buffer *p;

  pthread_mutex_lock(&mutex);
  p = GetBufferAsync(id);
  if(p) pthread_mutex_lock(&p->mutex);
  pthread_mutex_unlock(&mutex);

  if(!p) return X_NAME_INVALID;

  ClearBufferAsync(p);
  pthread_mutex_unlock(&p->mutex);

  return X_SUCCESS;
}

/**
 * Gets (linear) interpolated data from a specific local data buffer, for a specific time.
 *
 * @param[in]  id       Buffer ID, as returned by smaxBufferData().
 * @param[in]  t        (s) UNIX time value for which we want interpolated data.
 * @param[out] data     Array of doubles to hold the interpolkated values. The data should
 *                      be sized to hold the same number of doubles as was specified when
 *                      starting the data buffer with smax_buffer_data().
 *
 * @return      X_SUCCESS (0)   if successful or else an error code, such as:
 *              X_NAME_INVALID  if there is no buffer currently with the specified ID,
 *              X_INCOMPLETE    if the time is not bracketed by existing data.
 */
int smaxGetInterpolated(int id, double t, double *data) {
  Buffer *p;
  int s;

  pthread_mutex_lock(&mutex);
  p = GetBufferAsync(id);
  if(p) pthread_mutex_lock(&p->mutex);
  pthread_mutex_unlock(&mutex);

  if(!p) return X_NAME_INVALID;

  s = GetInterpolatedAsync(p, t, data);
  pthread_mutex_unlock(&p->mutex);

  return s;
}

/**
 * Calculates the sum of data from a specific local data buffer, for a specific time window.
 *
 * @param[in]  id       Buffer ID, as returned by smaxBufferData().
 * @param[in]  fromt    (s) UNIX time for beginning of time window.
 * @param[in]  tot      (s) UNIX time for end of time window.
 * @param[out] sum      Array of doubles to hold the sums. The array should
 *                      be sized to hold the same number of doubles as was specified when
 *                      starting the data buffer with smax_buffer_data().
 * @param[out] n        Pointer to integer to hold the number of points summed, ot NULL if
 *                      not requested.
 *
 * @return            X_SUCCESS (0)   if successful or else an error code, such as:
 *                    X_NAME_INVALID  if there is no buffer currently with the specified ID,
 *                    X_INCOMPLETE    if the time window is not bracketed by existing data.
 */
int smaxGetWindowSum(int id, double fromt, double tot, double *data, int *n) {
  Buffer *p;
  int s, fromi, toi;

  pthread_mutex_lock(&mutex);
  p = GetBufferAsync(id);
  if(p) pthread_mutex_lock(&p->mutex);
  pthread_mutex_unlock(&mutex);

  if(!p) return X_NAME_INVALID;

  s = GetIndexRangeAsync(p, fromt, tot, &fromi, &toi);
  GetSumAsync(p, fromi, toi, data);
  pthread_mutex_unlock(&p->mutex);

  return s;
}

/**
 * Calculates the averages of data from a specific local data buffer, for a specific time window.
 *
 * @param[in]  id       Buffer ID, as returned by smaxBufferData().
 * @param[in]  fromt    (s) UNIX time for beginning of time window.
 * @param[in]  tot      (s) UNIX time for end of time window.
 * @param[out] mean     Array of doubles to hold the averages values. The array should
 *                      be sized to hold the same number of doubles as was specified when
 *                      starting the data buffer with smax_buffer_data().
 * @param[out] rms      Optional array of doubles to hold the rms values, or NULL if RMS
 *                      is not requested. The array should be sized to hold the same number
 *                      of doubles as was specified when starting the data buffer with
 *                      smax_buffer_data().
 *
 * @return            X_SUCCESS (0)   if successful or else an error code, such as:
 *                    X_NAME_INVALID  if there is no buffer currently with the specified ID,
 *                    X_INCOMPLETE    if the time window is not bracketed by existing data.
 */
int smaxGetWindowAverage(int id, double fromt, double tot, double *mean, double *rms) {
  Buffer *p;
  int s, fromi, toi;

  pthread_mutex_lock(&mutex);
  p = GetBufferAsync(id);
  if(p) pthread_mutex_lock(&p->mutex);
  pthread_mutex_unlock(&mutex);

  if(!p) return X_NAME_INVALID;

  s = GetIndexRangeAsync(p, fromt, tot, &fromi, &toi);
  GetAverageAsync(p, fromi, toi, mean, rms);
  pthread_mutex_unlock(&p->mutex);

  return s;
}


/**
 * Determines the range of data from a specific local data buffer, for a specific time window.
 *
 * @param[in]  id       Buffer ID, as returned by smaxBufferData().
 * @param[in]  fromt    (s) UNIX time for beginning of time window.
 * @param[in]  tot      (s) UNIX time for end of time window.
 * @param[out] min      Optional array of doubles to hold the minimum values, or NULL if minima
 *                      are not requested. The array should be sized to hold the same number
 *                      of doubles as was specified when starting the data buffer with
 *                      smax_buffer_data().
 * @param[out] max      Optional array of doubles to hold the maximum values, or NULL if maxima
 *                      are not requested. The array should be sized to hold the same number
 *                      of doubles as was specified when starting the data buffer with
 *                      smax_buffer_data().
 *
 * @return            X_SUCCESS (0)   if successful or else an error code, such as:
 *                    X_NAME_INVALID  if there is no buffer currently with the specified ID,
 *                    X_INCOMPLETE    if the time window is not bracketed by existing data.
 */
int smaxGetWindowRange(int id, double fromt, double tot, double *min, double *max) {
  Buffer *p;
  int s, fromi, toi;

  pthread_mutex_lock(&mutex);
  p = GetBufferAsync(id);
  if(p) pthread_mutex_lock(&p->mutex);
  pthread_mutex_unlock(&mutex);

  if(!p) return X_NAME_INVALID;

  s = GetIndexRangeAsync(p, fromt, tot, &fromi, &toi);
  GetRangeAsync(p, fromi, toi, min, max);
  pthread_mutex_unlock(&p->mutex);

  return s;
}


/**
 * Gets the time range of data currently available in a specific local data buffer.
 *
 * @param[in]  id       Buffer ID, as returned by smaxBufferData().
 * @param[out] fromt    (s) UNIX time for earliest buffered data.
 * @param[out] tot      (s) UNIX time for last buffered data.
 *
 * @return            X_SUCCESS (0)   if successful or else an error code, such as:
 *                    X_NAME_INVALID  if there is no buffer currently with the specified ID,
 */
int smaxGetBufferedTimeRange(int id, double *fromt, double *tot) {
  Buffer *p;

  pthread_mutex_lock(&mutex);
  p = GetBufferAsync(id);
  if(p) pthread_mutex_lock(&p->mutex);
  pthread_mutex_unlock(&mutex);

  if(!p) return X_NAME_INVALID;

  if(!p->n) *fromt = *tot = NAN;
  else {
    *fromt = p->entries[p->firstIndex].t;
    *tot = p->entries[(p->firstIndex + p->n - 1) % p->size].t;
  }
  pthread_mutex_unlock(&p->mutex);

  return X_SUCCESS;
}


/**
 * Gets the number of data entries currently available in a specific local buffer.
 *
 * @param[in]  id     Buffer ID, as returned by smaxBufferData().
 *
 * @return            The number of buffered entries (>=0) or else
 *                    X_NAME_INVALID if there is no buffer currently with the specified ID.
 */
int smaxGetBufferSize(int id) {
  Buffer *p;
  int n = 0;

  pthread_mutex_lock(&mutex);
  p = GetBufferAsync(id);
  if(p) pthread_mutex_lock(&p->mutex);
  pthread_mutex_unlock(&mutex);

  if(!p) return X_NAME_INVALID;

  n = p->n;

  pthread_mutex_unlock(&p->mutex);

  return n;
}
