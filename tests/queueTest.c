/**
 * @file
 *
 * @date Created on: May 5, 2020
 * @author Attila Kovacs
 *
 *      This simple example program demonstrates and tests the use of queues to achieve high-throughput pulls
 *      from the SMA-X database, in the background. It shows how to use both waiting for a set of transfers to
 *      complete (using a synchronization point), or by supplying a callback routine for when the data is in,
 *      and also a catch-all wait for all queued transaction to complete (such as an exit cleanup routine might
 *      need).
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "smax.h"

#define TABLE   "_test_" X_SEP "queued"
#define NAME1   "integer"
#define NAME2   "float"
#define IVALUE  2020
#define FVALUE  3.14159265F

#define DEBUG   FALSE

static int testSyncPoint();
static int testWaitComplete();
static int testCallback();

static void checkStatus(char *op, int status) {
  if(!status) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}


int main() {
  checkStatus("connect", smaxConnect());

  // Put some data into SMA-X we can pull and check on...
  checkStatus("share1", smaxShareInt(TABLE, NAME1, IVALUE));
  checkStatus("share2", smaxShareDouble(TABLE, NAME2, FVALUE));

  // The above are sent via the pipeline, and we will pull via the pipeline
  // So the pulls will necessarily follow the push in this case. No need to wait...

  if(testSyncPoint()) exit(-1);
  if(testWaitComplete()) exit(-1);
  if(testCallback()) exit(-1);

  checkStatus("disconnect", smaxDisconnect());

  return 0;
}


// This test demonstrates waiting on a synchronization points when queueing
// pull requests for high-throughput pulls from SMA-X.
static int testSyncPoint() {
  XSyncPoint *gotMyData;
  XMeta meta1;
  int i;
  float f;

  // Queue up a bunch of pull requests. These will be pipelined
  // 1. Want an integer value pulled into the variable 'i' and associated metadata
  smaxQueue(TABLE, NAME1, X_INT, 1, &i, &meta1);

  // 2. Want a float value pulled into the variable 'f', without metadata
  smaxQueue(TABLE, NAME2, X_FLOAT, 1, &f, NULL);

  // After we queued a set of pull requests that we need to be fulfilled
  // before we proceed, we create a synchronization point after the
  // requests to mark the place in the queue we need to wait on.
  gotMyData = smaxCreateSyncPoint();

  // Now we can do something else (if we want to) before we need those pull
  // results
  if(DEBUG) printf("Hello! I've just sent a bunch of SMA-X requests. I'm going to chill a bit...\n");

  // Once we run out of other things to do and we really need those pulls to be ready
  // to go on, we will wait on the synchronization point we created after the
  // pull request. The call below will block until the synchronization point
  // is reached in the queue. If the second argument is non-zero it sets a timeout
  // int milliseconds.
  checkStatus("sync", smaxSync(gotMyData, 3000));

  // We no longer need that synchronization point, so destroy it.
  smaxDestroySyncPoint(gotMyData);
  gotMyData = NULL;

  // Let's check to see if the values are what we expect
  if(i != IVALUE) {
    fprintf(stderr, "ERROR! sync: Integer value mismatch (%d vs %d).\n", i, IVALUE);
    exit(-1);
  }

  if(f != FVALUE) {
    fprintf(stderr, "ERROR! sync: Float value mismatch (%f vs %f).\n", f, FVALUE);
    exit(-1);
  }

  printf("sync: OK\n");

  return 0;
}


// This test demonstrates waiting for all queued transactions to complete. It is
// somewhat similat to waiting on a synchrozination point, except that is does not
// wait for reaching a specific plane on the queue, only waiting for the queue to
// be emptied. If new transactions are qeueued while this call is waiting, they
// will have to be completed also. This is suitable when more surgical approaches
// are not necessary or to ensure an orderly exit before closing the program.
static int testWaitComplete() {
  XMeta meta1;
  int i;
  float f;

  // Queue up a bunch of pull requests. These will be pipelined
  // 1. Want an integer value pulled into the variable 'i' and associated metadata
  smaxQueue(TABLE, NAME1, X_INT, 1, &i, &meta1);

  // 2. Want a float value pulled into the variable 'f', without metadata
  smaxQueue(TABLE, NAME2, X_FLOAT, 1, &f, NULL);

  // Now we can do something else (if we want to) before we need those pull
  // results
  if(DEBUG) printf("Hello! I've just sent a bunch of SMA-X requests. I'm going to chill a bit...\n");

  // Now wait for all queued transaction to finish, including ones that may be
  // submitted in the meantime.
  // A non-zero argument sets a timeout in milliseconds.
  checkStatus("wait complete", smaxWaitQueueComplete(3000));

  // Let's check to see if the values are what we expect
  if(i != IVALUE) {
    fprintf(stderr, "ERROR! wait: Integer value mismatch (%d vs %d).\n", i, IVALUE);
    exit(-1);
  }

  if(f != FVALUE) {
    fprintf(stderr, "ERROR! wait: Float value mismatch (%f vs %f).\n", f, FVALUE);
    exit(-1);
  }

  printf("wait complete: OK\n");

  return 0;
}




// For the callback example, we need variables that will exists beyond and outside
// of the function that queues them, so they are valid even if that function finishes
// before the callback happens. In this example, we shall use global variables, but
// they could als be dynamically allocated data that are then passed to the callback
// routine (which probably needs to destroy it when it's done with using them)
struct {
  int i;
  float f;
  XMeta meta1;
} myData;

// In this example we'll use a simple string to pass to the callback routine.
#define CALLBACKARG "Hello!"

// This is an example callback function that we want to be called when
// out data has been queued for high-throughput pulls from SMA-X...
static void checkPull(void *arg) {
  char *str = (char *) arg;

  // Check the argument that was passed to us...
  if(strcmp(str, CALLBACKARG)) {
    fprintf(stderr, "ERROR! callback: Unexpected callback argument.\n");
    exit(-1);
  }

  // Let's check to see if the values are what we expect
  if(myData.i != IVALUE) {
    fprintf(stderr, "ERROR! callback: Integer value mismatch (%d vs %d).\n", myData.i, IVALUE);
    exit(-1);
  }

  if(myData.f != FVALUE) {
    fprintf(stderr, "ERROR! callback: Float value mismatch (%f vs %f).\n", myData.f, FVALUE);
    exit(-1);
  }

  printf("callback: OK\n");

  exit(0);
}


static int testCallback() {
  // Queue up a bunch of pull requests. These will be pipelined
  // 1. Want an integer value pulled into the variable 'i' and associated metadata
  smaxQueue(TABLE, NAME1, X_INT, 1, &myData.i, &myData.meta1);

  // 2. Want a float value pulled into the variable 'f' without metadata
  smaxQueue(TABLE, NAME2, X_FLOAT, 1, &myData.f, NULL);

  // Another way to know if the pulls have come through is to add a  callback
  // function to the queue after the pull requests. Our function will be called
  // upon as soon as all requests queued prior have been fulfilled.
  // We also supply a pointer as an argument we want to pass our callback routine
  // (which can be a pointer to anything, but in this example we just pass a string...)
  smaxQueueCallback(checkPull, CALLBACKARG);

  // Now we can go on with other business, unperturbed
  if(DEBUG) printf("Hello! I've just sent a bunch of SMA-X requests. I'm going to go on with my business...\n");

  sleep(3);

  // our callback would exit. So if we got this far, then it means it
  // did not get called on, so exit with an error....
  fprintf(stderr, "ERROR! Callback was not called back.\n");
  return -1;
}




