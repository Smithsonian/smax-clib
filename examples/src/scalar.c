/*
 * scalar.c
 *
 *  Created on: Feb 28, 2022
 *      Author: Attila Kovacs
 *
 *      This snippler shows how to read or write a scalar value (in this case a single integer)
 *      from / to SMA-X.
 *
 *      We will read/write under the ID of "_test_:example:my_value" in the most generic way.
 *      We use the '_test_' top-level stem here because this here is not an operational value, and
 *      won't be logged and can be deleted any time.
 *
 *      For real SMA-X values your ID would not start with underscore,
 *      and follow the SMA-X naming specification and (system:subsystem:...) hierarchy
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "smax.h"


// Just a simple function for checking for SMA-X errors. This one simply prints the
// error message, and exits the program with -1 if there was an error.
static void checkStatus(char *op, int status) {
  if(status >= 0) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}

int main(int argc, const char *argv[]) {
  int value = 2022;             // the scalar value we will write to SMA-X
  XMeta meta = X_META_INIT;     // (optional) if we want to read back metadata also.

  // 1. Connect to SMA-X
  checkStatus("connect", smaxConnect());

  // 2A. Set the scalar value in SMA-X
  checkStatus("share", smaxShare("_test_:example", "my_value", &value, X_INT, 1));    // We send 1 integer from the buffer 'value'...

  // 2B. Or, read the scalar value from SMA-X
  checkStatus("pull", smaxPull("_test_:example", "my_value", X_INT, 1, &value, &meta));  // Read 1 integer to the buffer 'value'...

  // 4. When done, disconnect from SMA-X
  smaxDisconnect();

  return 0;
}


