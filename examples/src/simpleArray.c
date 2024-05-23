/*
 * simpleArray.c
 *
 *  Created on: Feb 28, 2022
 *      Author: Attila Kovacs
 *
 *      This snippler shows how to read or write a 1D array of values (in this case integers)
 *      from / to SMA-X using a dynamically allocated (non-fixed sized) return value. If
 *      your program expects a fixed number of values, please check the more generic example
 *      provided by 'array.c'.
 *
 *      We use the '_test_' top-level stem here because this here is not an operational value, and
 *      won't be logged and can be deleted any time.
 *
 *      For real SMA-X values your D would not start with underscore, and follow the SMA-X
 *      naming specification and (system:subsystem:...) hierarchy
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "smax.h"

// Just a simple function for checking for SMA-X errors. This one simply prints the
// error message, and exits the program with -1 if there was an error.
static void checkStatus(char *op, int status) {
  if(!status) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}

int main(int argc, const char *argv[]) {
  int values[] = { 1, 2, 3 };                   // These are the values we'll send to SMA-X
  int nOut = sizeof(values) / sizeof(int);      // The number of values we send (3).
  int *readback;                                // These will be the values we'll read back
  int nIn;                                      // The number of values we got back.
  XMeta meta = X_META_INIT;                     // We will read back metadata as well, but it's optional.

  // 1. Connect to SMA-X
  checkStatus("connect", smaxConnect());

  // 2A. Set the array of values in SMA-X
  checkStatus("share", smaxShareInts("_test_:example", "my_array", values, nOut));

  // 2B. Or, read the array of values from SMA-X
  readback = smaxPullInts("_test_:example", "my_array", &meta, &nIn);
  if(!readback) {
    fprintf(stderr, "ERROR! pull returned NULL.\n");
    exit(-1);
  }

  // 3. When done, disconnect from SMA-X
  smaxDisconnect();

  // 5. Clean up. The smaxPullInts() returns a dynamically allocated int[] buffer. So we should
  //    free it up, once we don't need it any more...
  free(readback);

  return 0;

}


