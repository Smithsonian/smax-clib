/**
 * @file
 *
 * @date Created  on Jul 31, 2023
 * @author Attila Kovacs
 *
 *   This program just keeps updating _test_:unix_time in Redis once per second, and can be used to
 *   check resiliency, e.g. by shutting down the SMA-X server for a period and before restarting it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "smax.h"

#define TABLE   "_test_"
#define NAME    "unix_time"

static void checkStatus(char *op, int status) {
  if(status >= 0) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}

int main(int argc, const char *argv[]) {
  char *server = "smax";

  //smaxSetPipelined(FALSE);
  smaxSetResilient(TRUE);

  if(argc > 1) server = (char *) argv[1];

  checkStatus("connect", smaxConnectTo(server));

  while(TRUE) {
    time_t t = time(NULL);
    int status = smaxShareInt(TABLE, NAME, t);
    fprintf(stderr, " . %ld: status = %d, connected %d, pipe = %d, res = %d\n", (long) t, status, smaxIsConnected(), smaxIsPipelined(), smaxIsResilient());
    sleep(1);
  }

  return 0; /* NOT REACHED */
}
