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
#include <string.h>
#include <unistd.h>

#include "smax.h"

#ifndef SMAX_TEST_TIMEOUT
#  define SMAX_TEST_TIMEOUT 3     ///< [s] Default timeout
#endif

#define TABLE   "_test_" X_SEP "wait"
#define NAME    "value"


// Variables updated by the polling thread and checked/reported by main()
static int gotUpdate = FALSE;

static void checkStatus(char *op, int status) {
  if(!status) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}


// This thread will be running in the background, pounding on a variable
// without causing unnecessary network traffic. It will exit normally
// when it detects a change of the checked value.
static void *WaitingThread(void *arg) {
  XMeta meta = X_META_INIT;
  int initial;

  (void) arg; // unused

  // Lazy pull including metadata, but meta argument may be NULL if we don't need it.
  smaxPull(TABLE, NAME, X_INT, 1, &initial, &meta);

  while(TRUE) {
    char *key;
    int status;

    status = smaxWaitOnSubscribedGroup(TABLE, &key, SMAX_TEST_TIMEOUT, NULL);
    if(status) smaxError("WaitingThread", status);
    else if(!strcmp(key, NAME)) {                   // Check that it was indeed the key we are expecting that updated.
      int final;
      // Lazy pull including metadata, but meta argument may be NULL if we don't need it.
      smaxPull(TABLE, NAME, X_INT, 1, &final, &meta);
      if(final != initial) gotUpdate = TRUE;
      break;
    }
    else fprintf(stderr, "ERROR! Got unexpected update for key=%s\n", key);
  }

  return NULL;
}

int main() {
  pthread_t tid;
  int timeoutLoops = 100 * SMAX_TEST_TIMEOUT;

  xSetDebug(TRUE);

  smaxSetPipelined(TRUE);

  checkStatus("connect", smaxConnect());

  // Initialize the value that we will poll, and change at some later time...
  checkStatus("share", smaxShareInt(TABLE, NAME, 0));

  // Wait until we are sure the starting value is in the database.
  while(smaxPullInt(TABLE, NAME, -1) != 0) continue;

  checkStatus("subscribe", smaxSubscribe(TABLE, NAME));

  // Start the thread that will wait on a change...
  if(pthread_create(&tid, NULL, WaitingThread, NULL)) {
    perror("create WaitingThread");
    exit(-1);
  }

  // Let the polling thread pound on the value before we change it...
  sleep(1);

  // We'll update the value here...
  // The waiting thread should set gotUpdate when it unblocks...
  checkStatus("update", smaxShareInt(TABLE, NAME, 1));

  // Give the WaitingThread a bit of time to detect the change and exit normally
  while(--timeoutLoops >= 0) {
    struct timespec interval = { 0, 10000000 }; // Check every 10ms

    if(gotUpdate) {
      printf("wait: OK\n");
      exit(0);
    }

    nanosleep(&interval, NULL); // Check every 10ms
  }

  // If we go this far, then the polling thread did not get the update
  // So we return with an error.
  fprintf(stderr, "ERROR! Update was not detected.\n");
  return -1;
}


