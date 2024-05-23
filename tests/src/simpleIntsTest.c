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
#define NAME    "ints"
#define VALUE

static void checkStatus(char *op, int status) {
  if(!status) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}

int main(int argc, const char *argv[]) {
  int out[] = { 1, 2, 3 };
  int nOut = sizeof(out) / sizeof(int), nIn;
  int i, *in;
  XMeta meta = X_META_INIT;

  smaxSetPipelined(FALSE);

  checkStatus("connect", smaxConnect());

  checkStatus("share", smaxShareInts(TABLE, NAME, out, nOut));

  in = smaxPullInts(TABLE, NAME, &meta, &nIn);
  if(!in) {
    fprintf(stderr, "ERROR! pull returned NULL.\n");
    exit(-1);
  }

  checkStatus("disconnect", smaxDisconnect());

  if(nIn != nOut) {
    fprintf(stderr, "ERROR! readback dimension mismatch\n");
    exit(-1);
  }

  for(i=0; i<nOut; i++) if(in[i] != out[i]) {
    fprintf(stderr, "ERROR! data[%d] mismatch\n", i);
    exit(-1);
  }

  printf("OK\n");
  return 0;

}


