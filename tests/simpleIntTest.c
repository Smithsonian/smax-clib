/**
 * @file
 *
 * @date Created on: May 5, 2020
 * @author Attila Kovacs
 *
 *      This program demonstrates and tests simple shares and pulls to/from the SMA-X database
 *      Which may be suitable for casual accessing the primitive data without the associated
 *      metadata.
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "smax.h"

#define TABLE   "_test_" X_SEP "simple"
#define NAME    "integer"
#define VALUE   2020

static void checkStatus(char *op, int status) {
  if(status >= 0) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}

int main() {
  int i;

  xSetDebug(TRUE);

  smaxSetPipelined(FALSE);

  checkStatus("connect", smaxConnect());

  checkStatus("share", smaxShareInt(TABLE, NAME, VALUE));

  i = smaxPullInt(TABLE, NAME, 0);

  checkStatus("disconnect", smaxDisconnect());

  if(i == 0) {
    fprintf(stderr, "ERROR! pull returned default value.\n");
    exit(-1);
  }

  if(i != VALUE) {
    fprintf(stderr, "ERROR! readback value mismatch\n");
    exit(-1);
  }

  printf("OK\n");
  return 0;
}


