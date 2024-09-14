/*
 * benchmark.c
 *
 *  Created on: Feb 5, 2018
 *      Author: Attila Kovacs
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "rm.h"
#include "smax.h"
#include "smax-rm.h"

#define PI 3.14159265358979

static void readBenchmark(int n, boolean pipelined);
static int readBunch(int ant);
static int queueBunch(int ant);

static void writeBenchmark(int n);
static int writeBunch(int ant);

int main(int argc, const char *argv[]) {
  int antlist[11];
  int cycles = 100;

  if(argc > 1) cycles = atoi(argv[1]);

  smaxSetPipelined(TRUE);
  smaxSetVerbose(FALSE);

  if(rm_open(antlist)) {
    fprintf(stderr, "ERROR connecting to Redis. Exiting...\n");
    exit(1);
  }

  printf("Benchmarking pipelined writes...\n");
  writeBenchmark(cycles);

  printf("benchmarking reads...\n");
  readBenchmark(cycles, FALSE);

  printf("benchmarking piped reads...\n");
  readBenchmark(cycles, TRUE);

  printf("closing...\n");
  rm_close();

  exit(0);      // a return statement here segfaults on LynxOS at exit.
}


static void readBenchmark(int n, boolean pipelined) {
  struct timespec start, end;
  double dt;
  int k, ant, N = 0;

  clock_gettime(CLOCK_REALTIME, &start);

  if(pipelined) {
    for(k=n; --k >= 0; ) for(ant=1; ant <= 8; ant++) N += queueBunch(ant);
    if(smaxWaitQueueComplete(10000)) printf("WARNING! timed out...\n");
  }
  else for(k=n; --k >= 0; ) for(ant=1; ant <= 8; ant++) N += readBunch(ant);

  clock_gettime(CLOCK_REALTIME, &end);

  dt = end.tv_sec - start.tv_sec + 1e-9 * (end.tv_nsec - start.tv_nsec);
  printf(">>> read: %.1f reads/s\n", N / dt);
}

static void writeBenchmark(int n) {
  struct timespec start, end;
  double dt;
  int k, ant, N=0;

  clock_gettime(CLOCK_REALTIME, &start);

  for(k=n; --k >= 0; ) for(ant=1; ant <= 8; ant++) N += writeBunch(ant);

  clock_gettime(CLOCK_REALTIME, &end);

  dt = end.tv_sec - start.tv_sec + 1e-9 * (end.tv_nsec - start.tv_nsec);
  printf(">>> write: %.1f writes/s\n", N / dt);
}

// These variables must be persistent, or else we get segfaults
// (probably because they aren't being used actually...)
static double d;
static float f10x2[10][2];
static char name[35];

static int readBunch(int ant) {
  rm_read(ant, "RM_PMAZTILT_ELOFF_D", &d);
  rm_read(ant, "RM_CMD_EPOCH_YEAR_D", &d);
  rm_read(ant, "RM_CMD_SVEL_KMPS_D", &d);
  rm_read(ant, "RM_CMD_EPOCH_YEAR_D", &d);
  rm_read(ant, "RM_CMD_RA_HOURS_D", &d);
  rm_read(ant, "RM_CMD_SOURCE_C34", name);
  rm_read(ant, "RM_SYNCDET2_ALLAN_VARIANCE_V2_V10_F", f10x2);
  return 7;
}

static int queueBunch(int ant) {
  rm_queue_read(ant, "RM_PMAZTILT_ELOFF_D", &d);
  rm_queue_read(ant, "RM_CMD_EPOCH_YEAR_D", &d);
  rm_queue_read(ant, "RM_CMD_SVEL_KMPS_D", &d);
  rm_queue_read(ant, "RM_CMD_EPOCH_YEAR_D", &d);
  rm_queue_read(ant, "RM_CMD_RA_HOURS_D", &d);
  rm_queue_read(ant, "RM_CMD_SOURCE_C34", name);
  rm_queue_read(ant, "RM_SYNCDET2_ALLAN_VARIANCE_V2_V10_F", f10x2);
  return 7;
}


#define X 1.74635564757444e3

static int writeBunch(int ant) {
  static double d = X;
  static float f10[10] = { 1.0 * PI, -2.0 * PI, 3.0 * PI, -4.0 * PI, 5.0 * X, -6.0 * X, -7.0 * PI, 8.0 * X, -9.0 * X, 10.0 * PI};
  char name[34] = "MERCURY-VENUS-MARS-JUPITER";

  d += PI;

  rm_write(ant, "RM_PMAZTILT_ELOFF_D", &d);
  rm_write(ant, "RM_CMD_EPOCH_YEAR_D", &d);
  rm_write(ant, "RM_CMD_SVEL_KMPS_D", &d);

  d *= -1.0;

  rm_write(ant, "RM_CMD_EPOCH_YEAR_D", &d);
  rm_write(ant, "RM_CMD_RA_HOURS_D", &d);
  rm_write(ant, "RM_CMD_SOURCE_C34", name);

  d *= -1.0;

  rm_write(ant, "RM_SYNCDET2_ALLAN_VARIANCE_V2_V10_F", f10);

  return 7;
}
