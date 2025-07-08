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
#include <errno.h>

#include "smax.h"

#define TABLE           "_test_" X_SEP "control"
#define NAME            "value"
#define CONTROL_NAME    NAME "_control"
#define CONTROL_TIMEOUT 5

static void checkStatus(char *op, int status) {
  if(!status) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}

int ControlFunction(const char *table, const char *key, void *parg) {
  const char *replyKey = (const char *) parg;
  int value = smaxPullInt(table, key, -1);
  return smaxShareInt(table, replyKey, value);
}

int main() {
  int reply;

  checkStatus("connect", smaxConnect());

  // Initialize the value that we will control, and change at some later time...
  checkStatus("share", smaxShareInt(TABLE, CONTROL_NAME, 0));

  checkStatus("setControlCall", smaxSetControlFunction(TABLE, CONTROL_NAME, ControlFunction, NAME));

  // We'll update the value here...
  // The waiting thread should set gotUpdate when it unblocks...
  errno = 0;
  reply = smaxControlInt(TABLE, CONTROL_NAME, 1, NULL, NAME, -1, CONTROL_TIMEOUT);
  if(reply != 1) {
    fprintf(stderr, "ERROR! Unexpected reply: expected %d, got %d.\n", 1, reply);
    if(errno) fprintf(stderr, "      errno = %d (%s)\n", errno, strerror(errno));
    printf("control: FAILED\n");
    return -1;
  }

  printf("control: OK\n");

  return 0;
}


