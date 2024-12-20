/*
 * benchmark.c
 *
 *  Created on: Dec 20, 2024
 *      Author: Attila Kovacs
 *
 *  A simple tool for benchmarking SMA-X performance from a client to a designated server.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <popt.h>
#include <bsd/readpassphrase.h>

#include "smax.h"

#define PI 3.14159265358979

static void readBenchmark(int n, boolean pipelined, boolean withMeta);
static int readBunch(XMeta *meta);
static int queueBunch(XMeta *meta);

static void writeBenchmark(int n);
static int writeBunch();

static void printVersion(const char *name) {
  printf("%s %s\n", name, SMAX_VERSION_STRING);
}

int main(int argc, const char *argv[]) {
  const char *fn = "benchmark";

  char *host = "smax";
  int port = 6379;
  char *user = NULL;
  char *password = NULL;
  int askpass = FALSE;
  int cycles = 100;
  int dbIndex = 0;
  int verbose = FALSE;
  int debug = FALSE;

  struct poptOption options[] = { //
          {"host",       'h', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,    &host,     0, "Server hostname.", "<hostname>"}, //
          {"port",       'p', POPT_ARG_INT    | POPT_ARGFLAG_SHOW_DEFAULT,    &port,     0, "Server port.", "<port>"}, //
          {"pass",       'a', POPT_ARG_STRING, &password,  0, "Password to use when connecting to the server.", "<password>"}, //
          {"user",         0, POPT_ARG_STRING, &user,      0, "Used to send ACL style 'AUTH username pass'. Needs -a.", "<username>"}, //
          {"askpass",      0, POPT_ARG_NONE,   &askpass,   0, "Force user to input password with mask from STDIN.  " //
                  "If this argument is used, '-a' will be ignored.", NULL //
          }, //
          {"repeat",    'r', POPT_ARG_INT    | POPT_ARGFLAG_SHOW_DEFAULT,   &cycles,     0, "Repeat this many times.", "<times>"}, //
          {"db",        'n', POPT_ARG_INT     | POPT_ARGFLAG_SHOW_DEFAULT, &dbIndex,     0, "Database number.", "<index>"}, //
          {"verbose",     0, POPT_ARG_NONE,   &verbose,    0, "Verbose mode.", NULL }, //
          {"debug",       0, POPT_ARG_NONE   | POPT_ARGFLAG_DOC_HIDDEN,   &debug,       0, "Debug mode. Prints all network traffic.", NULL }, //
          {"version",     0, POPT_ARG_NONE,   NULL,      'v', "Output version and exit.", NULL }, //
          POPT_AUTOHELP POPT_TABLEEND //
  };

  int rc;

  poptContext optcon = poptGetContext(fn, argc, argv, options, 0);

  while((rc = poptGetNextOpt(optcon)) != -1) {
    if(rc < -1) {
      fprintf(stderr, "ERROR! Bad syntax. Try running with --help to see command-line options.\n");
      exit(1);
    }

    switch(rc) {
      case 'v': printVersion(fn); return 0;
    }
  }

  if(askpass) {
    password = (char *) malloc(1024);
    if(readpassphrase("Enter password: ", password, 1024, 0) == NULL) {
      free(password);
      password = NULL;
    }
  }

  smaxSetPipelined(TRUE);

  if(user || password) smaxSetAuth(user, password);
  if(verbose) smaxSetVerbose(TRUE);
  if(debug) xSetDebug(TRUE);

  smaxSetServer(host, port);
  if(dbIndex > 0) smaxSetDB(dbIndex);

  if(smaxConnect() != X_SUCCESS) {
    fprintf(stderr, "ERROR connecting to Redis. Exiting...\n");
    exit(1);
  }

  printf("Benchmarking pipelined writes...\n");
  writeBenchmark(cycles);

  printf("benchmarking reads (with meta)...\n");
  readBenchmark(cycles, FALSE, TRUE);

  printf("benchmarking reads (without meta)...\n");
  readBenchmark(cycles, FALSE, FALSE);

  printf("benchmarking piped reads with meta\n");
  readBenchmark(cycles, TRUE, TRUE);

  printf("benchmarking piped reads without meta\n");
  readBenchmark(cycles, TRUE, FALSE);

  printf("closing...\n");
  smaxDisconnect();

  return 0;
}


static void readBenchmark(int n, boolean pipelined, boolean withMeta) {
  struct timespec start, end;
  double dt;
  int k, N = 0;
  XMeta meta;
  XMeta *m = withMeta ? &meta : NULL;

  clock_gettime(CLOCK_REALTIME, &start);

  if(pipelined) {
    XSyncPoint *s;

    for(k=n; --k >= 0; ) N += queueBunch(m);
    s = smaxCreateSyncPoint();

    if(smaxSync(s, 10000)) printf("WARNING! timed out...\n");
  }
  else for(k=n; --k >= 0; ) N += readBunch(m);

  clock_gettime(CLOCK_REALTIME, &end);

  dt = end.tv_sec - start.tv_sec + 1e-9 * (end.tv_nsec - start.tv_nsec);
  printf(">>> read: %.1f reads/s\n", N / dt);
}

static void writeBenchmark(int n) {
  struct timespec start, end;
  double dt;
  int k, N=0;

  clock_gettime(CLOCK_REALTIME, &start);

  for(k=n; --k >= 0; ) N += writeBunch();

  clock_gettime(CLOCK_REALTIME, &end);

  dt = end.tv_sec - start.tv_sec + 1e-9 * (end.tv_nsec - start.tv_nsec);
  printf(">>> write: %.1f writes/s\n", N / dt);
}

// These variables must be persistent, or else we get segfaults
// (probably because they aren't being used actually...)
static boolean bval;
static int ival;
static float fval;
static double dval;
static float f10x2[10][2];
static char name[100];

static int queueBunch(XMeta *meta) {
  smaxQueue("_test_", "single_boolean_value", X_BOOLEAN, 1, &bval, meta);
  smaxQueue("_test_", "single_int_value", X_INT, 1, &ival, meta);
  smaxQueue("_test_", "single_float_value", X_FLOAT, 1, &fval, meta);
  smaxQueue("_test_", "single_double_value", X_DOUBLE, 1, &dval, meta);
  smaxQueue("_test_", "single_string_value", X_STRING, 1, &name, meta);
  smaxQueue("_test_", "small_float_array", X_FLOAT, 20, f10x2, meta);
  return 6;
}

static int readBunch(XMeta *meta) {
  smaxPull("_test_", "single_boolean_value", X_BOOLEAN, 1, &bval, meta);
  smaxPull("_test_", "single_int_value", X_INT, 1,  &ival, meta);
  smaxPull("_test_", "single_float_value", X_FLOAT, 1, &fval, meta);
  smaxPull("_test_", "single_double_value", X_DOUBLE, 1, &name, meta);
  smaxPull("_test_", "single_string_value", X_STRING, 1, &name, meta);
  smaxPull("_test_", "small_float_array", X_FLOAT, 20, f10x2, meta);
  return 6;
}

static int writeBunch() {
  static int count;

  float f = PI;

  smaxShareBoolean("_test_", "single_boolean_value", count % 2);
  smaxShareInt("_test_", "single_int_value", (count % 2 ? -1 : 1) * (1 << (count % 30)) + count);
  smaxShare("_test_", "single_float_value", &f, X_FLOAT, 1);
  smaxShareDouble("_test_", "single_double_value", PI);
  smaxShareString("_test_", "single_string_value", "Hello world! I'm a string value right here.");
  smaxShareFloats("_test_", "small_float_array", (float *) f10x2, 20);

  return 6;
}
