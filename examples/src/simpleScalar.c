/*
 * simpleScalar.c
 *
 *  Created on: Feb 28, 2022
 *      Author: Attila Kovacs
 *
 *      This snipplet shows how to read or write a scalar value (in this case a single integer)
 *      from / to SMA-X in a simpler (non-generic) way.
 *
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
  if(status >= 0) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}

int main(int argc, const char *argv[]) {
  int value = 2022;             // the scalar value we will write to SMA-X

  // 1. Connect to SMA-X
  checkStatus("connect", smaxConnect());

  // 2A. Set the scalar value in SMA-X
  checkStatus("share", smaxShareInt("_test_:example", "my_value", value));

  // 2B. Or, read a scalar value from SMA-X, or set 0 as the default value
  //    if there is no data in SMA-X or it could not be retrieved, or was
  //    not an integer.
  value = smaxPullInt("_test_:example", "my_value", 0);

  // 3. When done, disconnect from SMA-X
  smaxDisconnect();

  return 0;
}


