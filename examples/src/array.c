/*
 * array.c
 *
 *  Created on: Feb 28, 2022
 *      Author: Attila Kovacs
 *
 *      This snipplet shows how to read or write a 1D array of values (in this case integers)
 *      from / to SMA-X. This example reads values into a fixed-sized array supplied by the
 *      caller. Data in the array will be padded (with zeroes) or truncated as necessary
 *      if SMA-X contains fewer or more than the expected number of elements. If you don't
 *      want fixed sized read values, but rather get dynamically allocated arrays from read
 *      you should check the 'simpleArray.c' example instead.
 *
 *      We will read/write under the ID of "_test_:example:my_array" in the most generic way.
 *      We use the '_test_' top-level stem here because this here is not an operational value, and
 *      won't be logged and can be deleted any time.
 *
 *      For real SMA-X values your ID would not start with underscore, and follow the SMA-X
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
  int nValues = sizeof(values) / sizeof(int);   // The number of values we send (3).
  XMeta meta = X_META_INIT;                     // We will read back metadata as well, but it's optional.

  // 1. Connect to SMA-X
  checkStatus("connect", smaxConnect());

  // 2A. Set the array of values in SMA-X
  checkStatus("share", smaxShare("_test_:example", "my_array", values, X_INT, nValues));

  // 2B. Or, read the array of values from SMA-X into the buffer...
  checkStatus("pull", smaxPull("_test_:example", "my_array", X_INT, nValues, values, &meta));

  // 3. When done, disconnect from SMA-X
  smaxDisconnect();

  return 0;

}


