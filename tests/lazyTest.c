/**
 * @file
 *
 * @date Created on: May 6, 2020
 * @author Attila Kovacs
 *
 *      This simple program demonstrates and tests the use of lazy pulling from the SMA-X database.
 *      Lazy polling allows frequent checking on (i.e. polling) an infrequently changing variable's content
 *      without causing excessive network traffic. Data is pulled from SMA-X only on the first call
 *      to smaxLazyPull(), and then only when an update notification is received for the lazy value.
 */

#define _POSIX_C_SOURCE 199309L       ///< for nanosleep()

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "smax.h"

#define TABLE   "_test_" X_SEP "lazy"
#define NAME    "value"

// Variables updated by the polling thread and checked/reported by main()
static int gotUpdate = FALSE;
static long long nQueries = 0;
static int nUpdates = 0;

static void checkStatus(char *op, int status) {
  if(!status) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}


// This thread will be running in the background, pounding on a variable
// without causing unnecessary network traffic. It will exit normally
// when it detects a change of the checked value.
static void *PollingThread(void *arg) {
  XMeta meta;
  int initial;

  (void) arg; // unused

  // Lazy pull including metadata, but meta argument may be NULL if we don't need it.
  smaxLazyPull(TABLE, NAME, X_INT, 1, &initial, &meta);

  while(1) {
    int status, value;

    // We don't care to update the meta over and over again (but we could...)
    // We can pund on the data as much as we want locally without having
    // to worry about network traffic. There will be no network traffic as long
    // as the data is unchanged in SMA-X.
    status = smaxLazyPull(TABLE, NAME, X_INT, 1, &value, NULL);
    if(status) {
      smaxError("PollingThread", status);
      continue;
    }

    nQueries++;

    if(value != initial) break;
  }

  gotUpdate = TRUE;

  nUpdates = smaxGetLazyUpdateCount(TABLE, NAME);

  // Stop tracking lazy updates for our variable when we do not need the data any more...
  smaxLazyEnd(TABLE, NAME);

  return NULL;
}

int main() {
  pthread_t tid;
  int timeoutLoops = 100;

  xSetDebug(TRUE);

  smaxSetPipelined(TRUE);

  checkStatus("connect", smaxConnect());

  // Initialize the value that we will poll, and change at some later time...
  checkStatus("share", smaxShareInt(TABLE, NAME, 0));

  // Wait until we are sure the starting value is in the database.
  while(smaxPullInt(TABLE, NAME, -1) != 0) continue;

  // Start the thread that will pound on lazy pulls...
  if(pthread_create(&tid, NULL, PollingThread, NULL)) {
    perror("create PollingThread");
    exit(-1);
  }

  // Let the polling thread pound on the value before we change it...
  sleep(1);

  // We'll update the value here...
  checkStatus("update", smaxShareInt(TABLE, NAME, 1));

  // Give the PollingThread a bit of time to detect the change and exit normally
  while(--timeoutLoops >= 0) {
    struct timespec interval = { 0, 10000000 }; // Check every 10ms

    if(gotUpdate) {
      printf("lazy: OK (%lld queries, %d update[s])\n", nQueries, nUpdates);
      exit(0);
    }

    nanosleep(&interval, NULL);
  }

  // Once we are done with a set of lazy pulling, we can flush all lazy caches
  // and pause updates for all lazy variables. This means though that any new lazy pull
  // will necessarily pull from SMA-X again, so we need to use this with caution...
  smaxLazyFlush();

  // If we go this far, then the polling thread did not get the update
  // So we return with an error.
  fprintf(stderr, "ERROR! Update was not detected.\n");
  return -1;
}


