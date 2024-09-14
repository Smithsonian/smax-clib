/**
 * \file
 *
 * \date Jun 25, 2019
 * \author Attila Kovacs
 *
 * \brief
 *      Functions to support pipelined pull requests from SMA-X.
 *      Because they don't requite a sequence of round-trips, pipelined pulls can
 *      be orders of magnitude faster than staggered regular pull requests.
 *
 * ## Pipelined pulling (high volume queries)
 *
 *  - [Synchronization points and waiting](#lazy-synchronization)
 *  - [Callbacks](#lazy-callbacks)
 *  - [Finishing up](#lazy-finish)
 *
 * The regular pulling of data from SMA-X requires a separate round-trip for each and every request. That is, successive
 * pulls are sent only after the responses from the prior pull has been received. A lot of the time is spent on waiting
 * for responses to come back. With round trip times in the 100 &mu;s range, this means that this method of fetching data
 * from SMA-X is suitable for obtaining at most a a few thousand values per second.
 *
 * However, sometimes you want to get access to a large number of values faster. This is what pipelined pulling is for.
 * In pipelined mode, a batch of pull requests are sent to the SMA-X Redis server in quick succession, without waiting
 * for responses. The values, when received are processed by a dedicated background thread. And, the user has an option
 * of either waiting until all data is collected, or ask for as callback when the data is ready.
 *
 * Again it works similarly to the basic pulling, except that you submit your pull request to a queue with
 * `smaxQueue()`. For example:
 *
 * ```c
 *   double d; // A value we will fill
 *   XMeta meta;   // (optional) metadata to fill (for the above value).
 *
 *   int status = smaxQueue("some_table", "some_var", X_DOUBLE, 1, &d, &meta);
 *  ```
 *
 * Pipelined (batched) pulls have dramatic effects on performance. Rather than being limited by round-trip times, you will
 * be limited by the performance of the Redis server itself (or the network bandwidth on some older infrastructure). As
 * such, instead of thousand of queries per second, you can pull 2-3 orders of magnitude more in a given time, with hudreds
 * of thousands to even millions of pull per second this way.
 *
 * <a name="lazy-synchronization"></a>
 * ### Synchronization points and waiting
 *
 * After you have submitted a batch of pull request to the queue, you can create a synchronization point as:
 *
 * ```c
 *   XSyncPoint *syncPoint = smaxCreateSyncPoint();
 * ```
 *
 * A synchronization point is a marker in the queue that we can wait on. After the synchronization point is created, you
 * can sumbit more pull request to the same queue (e.g. for another processing block), or do some other things for a bit
 * (since it will take at least some microseconds before the data is ready). Then, when ready you can wait on the
 * specific synchronization point to ensure that data submitted prior to its creation is delivered from SMA-X:
 *
 * ```c
 *   // Wait for data submitted prior to syncPoint to be ready, or time out after 1000 ms.
 *   int status = smaxSync(syncPoint, 1000);
 *
 *   // Destroy the synchronization point if we no longer need it.
 *   xDestroySyncPoint(syncPoint);
 *
 *   // Check return status...
 *   if(status == X_TIMEOUT) {
 *     // We timed out
 *     ...
 *   }
 *   else if(status < 0) {
 *     // Some other error
 *     ...
 *   }
 * ```
 *
 * <a name="lazy-callbacks"></a>
 * ### Callbacks
 *
 * The alternative to synchronization points and waiting, is to provide a callback function, which will process your data
 * as soon as it is available, e.g.:
 *
 * ```c
 *   void my_pull_processor(void *arg) {
 *      // Say, we expect a string tag passed along to identify what we need to process...
 *      char *tag = (char *) arg;
 *
 *      // Do what we need to do...
 *      ...
 *   }
 * ```
 *
 * Then submit this callback routine to the queue after the set of variables it requires with:
 *
 * ```c
 *   // We'll call my_pull_processor, with the argument "some_tag", when prior data has arrived.
 *   smaxQueueCallback(my_pull_processor, "some_tag");
 * ```
 *
 * <a name="lazy-finish"></a>
 * ### Finishing up
 *
 * If you might still have some pending pipelined pulls that have not received responses yet, you may want to wait until
 * all previously sumbitted requests have been collected. You can do that with:
 *
 * ```c
 *   // Wait for up to 3000 ms for all pipelined pulls to collect responses from SMA-X.
 *   int status = smaxWaitQueueComplete(3000);
 *
 *   // Check return status...
 *   if(status == X_TIMEOUT) {
 *     // We timed out
 *     ...
 *   }
 *   else if(status < 0) {
 *     // Some other error
 *     ...
 *   }
 * ```
 *
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include "smax-private.h"

/// \cond PRIVATE
#define E6                      (1000000L)
#define E9                      (1000000000L)

#define X_SYNCPOINT             111111
#define X_CALLBACK              111112
/// \endcond

// Queued (pipelined) pulls ------------------------------>
typedef struct XQueue {
  void *first;                // Pointer to PullRequest list...
  void *last;                 // Pointer to the last PullRequest...
  int status;
} XQueue;

static pthread_mutex_t qLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qComplete = PTHREAD_COND_INITIALIZER;

// Local prototypes -------------------------------------->
static void InitQueueAsync();
static void QueueAsync(PullRequest *req);
static void ResubmitQueueAsync();
static int DrainQueueAsync(int maxRemaining, int timeoutMicros);
static void ProcessPipeResponseAsync(RESP *reply);
static void Sync();
static void RemoveQueueHead();
static void DiscardQueuedAsync();

// The variables below should be accessed only after an exclusive lock on pipeline->channelLock
// e.g. via lockChannel(REDISX_PIPELINE_CHANNEL);
static XQueue queued;
static int nQueued = 0;
static int maxQueued = SMAX_DEFAULT_MAX_QUEUED;

static boolean isQueueInitialized = FALSE;

/**
 * Creates a synchronization point that can be waited upon until all elements queued prior to creation
 * are processed (retrieved from the database.
 *
 *  \return     Pointer to a newly created synchronization point that can be waited upon.
 *
 *  @sa smaxSync()
 *  @sa smaxQueue()
 *  @sa smaxQueueCallback()
 */
XSyncPoint *smaxCreateSyncPoint() {
  XSyncPoint *s = (XSyncPoint *) calloc(1, sizeof(XSyncPoint));
  x_check_alloc(s);

  s->lock = (pthread_mutex_t *) calloc(1, sizeof(pthread_mutex_t));
  x_check_alloc(s->lock);
  pthread_mutex_init(s->lock, NULL);

  s->isComplete = (pthread_cond_t *) calloc(1, sizeof(pthread_cond_t));
  x_check_alloc(s->isComplete);
  pthread_cond_init(s->isComplete, NULL);

  if(queued.first == NULL) {
    // If queue is empty then just set status accordingly...
    s->status = X_SUCCESS;
  }
  else {
    // Otherwise put the synchronization point onto the queue.
    PullRequest *req = (PullRequest *) calloc(1, sizeof(PullRequest));
    x_check_alloc(req);

    req->type = X_SYNCPOINT;
    req->value = s;

    s->status = X_INCOMPLETE;

    pthread_mutex_lock(&qLock);
    QueueAsync(req);
    pthread_mutex_unlock(&qLock);
  }

  return s;
}

/**
 * Destroys a synchronization point, releasing the memory space allocated to it.
 *
 * \param s     Pointer to the synchronization point to discard.
 *
 */
void smaxDestroySyncPoint(XSyncPoint *s) {
  if(s == NULL) return;

  if(s->lock != NULL) pthread_mutex_lock(s->lock);
  if(s->isComplete != NULL) {
    pthread_cond_destroy(s->isComplete);
    free(s->isComplete);
  }
  if(s->lock != NULL) {
    pthread_mutex_unlock(s->lock);
    pthread_mutex_destroy(s->lock);
    free(s->lock);
  }
  free(s);
}

/**
 * Adds a callback function to the queue to be called with the specified argument once all prior
 * requests in the queue have been fullfilled (retrieved from the database).
 *
 * As a general rule callbacks added to the pipeline should return very fast, and avoid blocking
 * operations for the most part (using mutexes that may block for very short periods only may be
 * excepted). If the user needs to do more processing, or make blocking calls (e.g. IO operartions)
 * that may not return for longer periods, the callback should fire off processing in a separate
 * thread, or else simply move the result into another asynchronous processing queue.
 *
 * \param f         The callback function that takes a pointer argument
 * \param arg       Argument to call the specified function with.
 *
 * \return          X_SUCCESS (0) or else X_NULL if the function parameter is NULL.
 *
 * @sa smaxCreateSyncPoint()
 * @sa smaxQueue()
 */
int smaxQueueCallback(void (*f)(void *), void *arg) {
  if(!f) return x_error(X_NULL, EINVAL, "smaxQueueCallback", "function parameter is NULL");

  if(queued.first == NULL) {
    // If nothing is queued, just call back right away...
    f(arg);
  }
  else {
    // Otherwise, place the callback request onto the queue...
    PullRequest *req = (PullRequest *) calloc(1, sizeof(PullRequest));

    x_check_alloc(req);

    req->type = X_CALLBACK;
    req->value = f;
    req->key = (char *) arg;

    pthread_mutex_lock(&qLock);
    QueueAsync(req);
    pthread_mutex_unlock(&qLock);
  }

  return X_SUCCESS;
}

/**
 * Start pipelined read operations. Pipelined reads are much faster but change the behavior slightly.
 *
 */
static void InitQueueAsync() {
  int status;

  if(isQueueInitialized) return;

  xvprintf("SMA-X> Initializing queued pulls.\n");

  status = smaxSetPipelineConsumer(ProcessPipeResponseAsync);
  if(!status) {
    if(SMAX_RESTORE_QUEUE_ON_RECONNECT) smaxAddConnectHook(ResubmitQueueAsync);
    else smaxAddDisconnectHook(DiscardQueuedAsync);
    isQueueInitialized = TRUE;
  }
  else fprintf(stderr, "WARNING! SMA-X : failed to set pipeline consumer.\n");
}

/**
 * Configures how many pull requests can be queued in when piped pulls are enabled. If the
 * queue reaches the specified limit, no new pull requests can be submitted until responses
 * arrive, draining the queue somewhat.
 *
 * \param n     The maximum number of pull requests that can be queued.
 *
 * \return      TRUE if the argument was valid, and the queue size was set to it, otherwise FALSE
 */
int smaxSetMaxPendingPulls(int n) {
  if(n < 1) return x_error(X_FAILURE, EINVAL, "smaxSetMaxPendingPulls", "invalid limit: %d", n);
  maxQueued = n;
  return X_SUCCESS;
}



static void ResubmitQueueAsync() {
  PullRequest *p;

  for(p = queued.first; p != NULL; p = p->next) {
    int status;

    if(p->type == X_SYNCPOINT) continue;
    if(p->type == X_CALLBACK) continue;

    status = smaxRead(p, REDISX_PIPELINE_CHANNEL);
    if(status) {
      //smaxZero(p->value, p->type, p->count);
      smaxError("xResubmitQueueAsync()", status);
    }
  }
}

/**
 * Waits for the queue to reach the specified sync point, up to an optional timeout limit.
 *
 * \param sync              Pointer to a queued synchronization point.
 * \param timeoutMillis     An optional timeout in milliseconds. When set to a positive value
 *                          The call will be guaranteed to return in the specified interval,
 *                          whether or not the pipelined reads all succeeded. The return value
 *                          can be used to check for errors or if the call timed out before
 *                          all data were collected. If X_TIMEDOUT is returned, smax_end_bulk_pulls()
 *                          may be called again to allow more time for the queued read operations
 *                          to complete.
 *                          0 or negative timeout values will cause the call to wait indefinitely
 *                          until reads are complete.
 *
 * \return        X_SUCCESS (0)   if all reads have completed successfully, or the first
 *                                read error that was enountered (e.g. RM_INVALID_KEY), or:
 *                X_TIMEDOUT      if the call timed out while still awaiting data for the queued
 *                                read requests.
 *                X_NULL          if the SyncPoint argument is NULL, or its mutex/condition field
 *                                have not been initialized.
 *                X_FAILURE       if the SyncPoint's mutex has not been initialized.
 *
 *                or the first pull error encountered in the queue since the current batch began.
 *
 * @sa smaxCreateSyncPoint()
 * @sa smaxWaitQueueComplete()
 */
int smaxSync(XSyncPoint *sync, int timeoutMillis) {
  static const char *fn = "smaxSync";

  struct timespec end;
  int status = 0;

  if(sync == NULL) return x_error(X_NULL, EINVAL, fn, "synchronization point argument is NULL");
  if(sync->lock == NULL) return x_error(X_NULL, EINVAL, fn, "sync->lock is NULL");
  if(sync->isComplete == NULL) return x_error(X_NULL, EINVAL, fn, "sync->isComplete is NULL");

  if(timeoutMillis > 0) {
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += timeoutMillis / 1000;
    end.tv_nsec += E6 * (timeoutMillis % 1000);
    if(end.tv_nsec > E9) end.tv_nsec -= E9;
  }

  if(pthread_mutex_lock(sync->lock)) {
    xvprintf("SMA-X> Sync lock error.\n");
    return x_error(X_FAILURE, errno, fn, "mutex lock error");
  }

  // Check if there is anything to actually wait for...
  if(sync->status != X_INCOMPLETE && queued.first == NULL) {
    xvprintf("SMA-X> Already synchronized.\n");
    pthread_mutex_unlock(sync->lock);
    return x_error(sync->status, EALREADY, fn, "already synched");
  }

  xvprintf("SMA-X> Waiting to reach synchronization...\n");

  while(!status && sync->status == X_INCOMPLETE) {
    if(timeoutMillis > 0) status = pthread_cond_timedwait(sync->isComplete, sync->lock, &end);
    else status = pthread_cond_wait(sync->isComplete, sync->lock);

    if(queued.first == NULL) sync->status = X_SUCCESS;  // If the queue is empty, then we are synchronized
  }

  pthread_mutex_unlock(sync->lock);

  xvprintf("SMA-X> End wait for synchronization.\n");

  // If timeout with an incomplete sync, then return X_TIMEDOUT
  if(status == ETIMEDOUT) return x_error(X_TIMEDOUT, status, fn, "timed out");

  // If the queue is in an erroneous state, then set the sync status to indicate potential issues.
  // (The error may not have occured at any point prior to the synchronization, and even prior to
  // the batch of pulls we care about...
  if(queued.status) x_trace(fn, NULL, queued.status);

  prop_error(fn, sync->status);
  return X_SUCCESS;
}

/**
 * Waits until all queued pull requests have been retrieved from the database, or until the specified
 * timeout it reached.
 *
 * \param timeoutMillis     An optional timeout in milliseconds. When set to a positive value
 *                          The call will be guaranteed to return in the specified interval,
 *                          whether or not the pipelined reads all succeeded. The return value
 *                          can be used to check for errors or if the call timed out before
 *                          all data were collected. If X_TIMEDOUT is returned, smax_end_bulk_pulls()
 *                          may be called again to allow more time for the queued read operations
 *                          to complete.
 *                          0 or negative timeout values will cause the call to wait indefinitely
 *                          until reads are complete.
 *
 * \return        X_SUCCESS (0)   if all reads have completed successfully, or the first
 *                                read error that was enountered (e.g. RM_INVALID_KEY), or:
 *                X_TIMEDOUT      if the call timed out while still awaiting data for the queued
 *                                read requests.
 *
 * @sa smaxSync()
 */
int smaxWaitQueueComplete(int timeoutMillis) {
  XSyncPoint sync;

  if(queued.first == NULL) return X_SUCCESS;

  sync.status = X_INCOMPLETE;
  sync.isComplete = &qComplete;
  sync.lock = &qLock;

  prop_error("smaxWaitQueueComplete", smaxSync(&sync, timeoutMillis));
  return X_SUCCESS;
}

/**
 * Wait until no more than the specified number of pending reads remain in the queue, or until the timeout limit is reached.
 *
 * \param maxRemaining      The maximum number of pending reads that may remain before the call returns.
 * \param timeoutMicros     The timeout limit for draining the queue, in microseconds.
 *
 * \return                  X_SUCCESS      if the successfully drained to below the specified limit.
 *                          X_TIMEDOUT     if the timeout limit was reached before the queue drained.
 *                          X_NO_SERVICE   if the connection closed while waiting...
 */
static int DrainQueueAsync(int maxRemaining, int timeoutMicros) {
  static const char *fn = "xDrainQueue";
  int totalSleep = 0;

  xvprintf("SMA-X> read queue full. Waiting to drain...\n");
  while(nQueued > maxRemaining) {
    int sleepMicros;
    if(!redisxHasPipeline(smaxGetRedis())) return x_error(X_NO_SERVICE, ENOTCONN, fn, "no pipeline client");
    if(timeoutMicros > 0) if(totalSleep > timeoutMicros) return x_error(X_TIMEDOUT, ETIMEDOUT, fn, "timed out");
    sleepMicros = 1 + nQueued - maxRemaining;
    totalSleep += sleepMicros;
    usleep(sleepMicros);
  }
  xvprintf("SMA-X> read queue drained, resuming pipelined reads.\n");

  return X_SUCCESS;
}

/**
 * The listener function that processes pipelined responses in the background.
 *
 * \param reply     The RESP structure containing a response received on the pipeline channel to some earlier query.
 *
 */
static void ProcessPipeResponseAsync(RESP *reply) {
  if(reply->type == RESP_BULK_STRING || reply->type == RESP_ARRAY) {
    static int lastError = 0;
    int status;

    // Peek at the head of the queue (no lock needed as we aren't modifying the queue).
    PullRequest *req = (PullRequest *) queued.first;

    xdprintf("pipe RESP: %s.\n", (char *) reply->value);

    if(req == NULL) {
      fprintf(stderr, "ERROR! SMA-X : No pending read request for piped bulk string RESP.\n");
      return;
    }

    status = smaxProcessReadResponse(reply, req);           // parse into the pull request

    if(status) {
      if(status != lastError) fprintf(stderr, "ERROR! SMA-X : piped read value error %d on %s:%s.\n", status, req->group == NULL ? "" : req->group, req->key);
      if(!queued.status) queued.status = status;
    }
    lastError = status;

    RemoveQueueHead();
    Sync();
  }
  else smaxProcessPipedWritesAsync(reply);
}

/**
 * Processes timely synchronizations, whether callbacks, or synchronization points.
 *
 */
static void Sync() {
  // While the next request is a synchronization point or callback then act on them...
  while(TRUE) {
    // Peek at the head of the queue (no lock needed as we aren't modifying the queue).
    PullRequest *req = (PullRequest *) queued.first;

    if(req == NULL) return;
    if(req->value == NULL) return;

    if(req->type == X_SYNCPOINT) {
      XSyncPoint *s = (XSyncPoint *) req->value;
      pthread_mutex_lock(s->lock);
      s->status = X_SUCCESS;
      pthread_cond_broadcast(s->isComplete);
      pthread_mutex_unlock(s->lock);
      req->value = NULL;            // Dereference SyncPoint before destroying.
    }

    else if(req->type == X_CALLBACK) {
      void (*f)(char *) = (void (*)(char *)) req->value;
      f(req->key);
      req->key = req->value = NULL;  // Dereference the callback function and argument before destroying.
    }

    else return;

    RemoveQueueHead();
  }
}

/**
 *  Discard all piped reads, setting values to zeroes.
 *
 */
static void DiscardQueuedAsync() {
  PullRequest *p = queued.first;
  int n = 0;

  while(p != NULL) {
    PullRequest *next = p->next;
    smaxDestroyPullRequest(p);
    p = next;
    n++;
  }

  queued.status = n > 0 ? X_INTERRUPTED : 0;
  nQueued = 0;

  pthread_cond_broadcast(&qComplete);
}

/**
 * Queues a pull requests for pipelined data retrieval. Because pipelined pulls are executed on
 * a separate Redis client from the one used for sharing values, e.g. via smaxShare(), there is
 * no guarantee as to the order of this pull operation and previously initiated shares from the
 * same thread. This would only be an issue if you are trying to use queued read to read back a
 * value you have just shared -- which is not really a good use case anyway, as it generates
 * network traffic for not real reason. But, if you must read back a value you have shared, you
 * probably should use a regular smaxPull() call to ensure ordering.
 *
 * \param table     Hash table name.
 * \param key       Variable name under which the data is stored.
 * \param type      SMA-X variable type, e.g. X_FLOAT or X_CHARS(40), of the buffer.
 * \param count     Number of points to retrieve into the buffer.
 * \param[out] value     Pointer to the buffer to which the data is to be retrieved.
 * \param[out] meta      Pointer to the corresponding metadata structure, or NULL.
 *
 * \return      X_SUCCESS (0)       if successful
 *              X_NAME_INVALID      if the table and key are both NULL
 *              X_NULL              if the value field is NULL
 *              or the return value of xQueue().
 *
 * @sa smaxPull()
 * @sa smaxLazyPull()
 * @sa smaxCreateSyncPoint()
 * @sa smaxQueueCallback()
 *
 */
int smaxQueue(const char *table, const char *key, XType type, int count, void *value, XMeta *meta) {
  static const char *fn = "smaxQueue";

  PullRequest *req, *last;
  int status;

  if(table == NULL) return x_error(X_GROUP_INVALID, EINVAL, fn, "table is NULL");
  if(!table[0]) return x_error(X_GROUP_INVALID, EINVAL, fn, "table is empty");
  if(key == NULL) return x_error(X_NAME_INVALID, EINVAL, fn, "key is NULL");
  if(!key[0]) return x_error(X_NAME_INVALID, EINVAL, fn, "key is empty");
  if(value == NULL) return x_error(X_NULL, EINVAL, fn, "outut value is NULL");

  req = (PullRequest *) calloc(1, sizeof(PullRequest));
  x_check_alloc(req);

  req->group = xStringCopyOf(table);
  req->key = xStringCopyOf(key);
  req->value = value;
  req->type = type;
  req->count = count;
  req->meta = meta;

  // If the queue is full, then drain it to ~50% capacity...
  if(nQueued > maxQueued) {
    status = DrainQueueAsync(maxQueued >> 1, 1000 * SMAX_PIPE_READ_TIMEOUT_MILLIS);
    if(status) {
      if(status != X_NO_SERVICE)
         fprintf(stderr, "ERROR! SMA-X : piped read timed out on %s:%s.\n", (req->group == NULL ? "" : req->group), req->key);
      free(req);
      return x_trace(fn, NULL, status);
    }
  }

  pthread_mutex_lock(&qLock);

  last = queued.last;
  QueueAsync(req);

  // Send the pull request to Redis for this queued entry
  status = smaxRead(req, REDISX_PIPELINE_CHANNEL);

  // If the pull request was not submitted to SMA-X, then undo the queuing...
  if(status) queued.last = last;

  pthread_mutex_unlock(&qLock);

  if(status) {
    smaxDestroyPullRequest(req);
    // smaxZero(req->value, req->type, req->count);
  }

  prop_error(fn, status);

  return X_SUCCESS;
}

/**
 * Queues a pull request for pipelined retrieval. Apart from retrieving regular variables, the pull
 * request may specify a synchronization point or callback function.
 *
 * \param req   Pointer to the pull request to queue.
 *
 * \return      X_SUCCESS (0)       if successful
 *              or else the  return value of xDrainQueue().
 */
static void QueueAsync(PullRequest *req) {
  if(!isQueueInitialized) InitQueueAsync();

  req->next = NULL;

  // Add the item to the queue.
  if(queued.last != NULL) ((PullRequest *) queued.last)->next = req;
  queued.last = req;
  if(queued.first == NULL) {
    queued.first = queued.last;
    queued.status = X_SUCCESS;     // Reset the queue status when starting a new batch of pulls...
  }

  nQueued++;
}

static void RemoveQueueHead() {
  PullRequest *req = NULL;

  pthread_mutex_lock(&qLock);

  if(queued.first != NULL) {
    req = queued.first;
    queued.first = req->next;

    if(queued.first == NULL) {
      queued.last = NULL;
      nQueued = 0;
      pthread_cond_broadcast(&qComplete);
    }
    else nQueued--;
  }

  pthread_mutex_unlock(&qLock);

  if(req != NULL) smaxDestroyPullRequest(req);
}

