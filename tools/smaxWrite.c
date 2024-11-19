/**
 * @file
 *
 * @date Created on: Oct 4, 2022
 * @author Attila Kovacs
 *
 *  Simple command-line tool for setting SMA-X database values.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "smax.h"
#include "redisx.h"

static XField f;
static char *host = SMAX_DEFAULT_HOSTNAME;
static char *delims = ",;";
static char *sType;

static void syntax() {
  printf("\n");
  printf("  Syntax: smaxWrite -t <type> [options] table:key value\n\n");
}

static void usage() {
  syntax();
  printf("    -t <type>    Specify type, e.g. 'int8', 'float', 'string', 'raw'.\n");
  printf("    table:key    SMA-X variable ID.\n");
  printf("    value        value(s), separated by delimiter(s). You may enclose in quotes\n");
  printf("                 and/or use escaped characters.\n\n");
  printf("  Options:\n\n");
  printf("    -d <dims>    Set dimensions, e.g. '3,8' for 3x8 array.\n");
  printf("    -D <delims>  Set delimiter chars (default are comma and semicolon).\n");
  printf("    -s <host>    Use a specific host as the SMA-X database server.\n");
  exit(1);
}

static void setOption(char *argv[], int *next) {
  char *option = argv[(*next)++];
  option++;

  if(!strcmp(option, "t") || !strcmp(option, "-type")) {
    sType = argv[*(next++)];
    f.type = smaxTypeForString(sType);
    if(f.type == X_UNKNOWN || f.type == X_STRUCT) {
      fprintf(stderr, "ERROR! Invalid type: %s\n", sType);
      exit(1);
    }
  }

  else if(!strcmp(option, "d") || !strcmp(option, "-dims")) {
    f.ndim = xParseDims(argv[*(next++)], f.sizes);
  }

  else if(!strcmp(option, "D") || !strcmp(option, "-delims")) {
    delims = argv[*(next++)];
  }

  else if(!strcmp(option, "s") || !strcmp(option, "-server")) {
    host = argv[*(next++)];
  }
}

static int replaceDelims(char *str, char sep) {
  int n = 0;
  int N = strlen(delims);
  for(; *str; str++) {
    int i;
    for(i=0; i < N; i++) if(*str == delims[i]) {
      n++;
      *str = sep;
    }
  }
  return n;
}

int main(int argc, char *argv[]) {
  const char *id = NULL;
  char *table, *value = NULL;
  int next, n, status;

  if(argc < 5) usage();
  smaxSetPipelined(FALSE);

  for(next = 1; next < argc; next++) {
    if(argv[next][0] == '-') setOption(argv, &next);
    else if(value) {
      fprintf(stderr, "ERROR! Too many arguments.\n");
      exit(1);
    }
    else if(id) value = argv[next];
    else id = argv[next];
  }

  if(!id) {
    fprintf(stderr, "ERROR! Missing table:key and value arguments.\n");
    exit(1);
  }

  if(!value) {
    fprintf(stderr, "ERROR! Value was not defined.\n");
    exit(1);
  }

  if(!sType) {
    fprintf(stderr, "ERROR! Type must be set via -t <type>.\n");
    exit(1);
  }

  table = xStringCopyOf(id);
  status = xSplitID(table, &f.name);
  if(status) {
    fprintf(stderr, "ERROR! Invalid table:key argument: %s\n", id);
    exit(1);
  }

  n = replaceDelims(value, f.type == X_STRING ? '\r' : ' ');
  if(!f.sizes[0]) {
    f.ndim = 1;
    f.sizes[0] = n + 1;
  }

  f.value = calloc(xGetFieldCount(&f), xElementSizeOf(f.type));
  if(!f.value) {
    fprintf(stderr, "ERROR! alloc error (%d x %d): %s\n", xGetFieldCount(&f), xElementSizeOf(f.type), strerror(errno));
  }

  if(f.type == X_STRING || f.type == X_RAW) f.value = (char *) &value;
  else {
    status = smaxStringToValues(value, f.value, f.type, xGetFieldCount(&f), &n);
    if(status < 0) {
      fprintf(stderr, "ERROR! SMA-X invalid value: %s\n", smaxErrorDescription(status));
      return status;
    }
  }

  status = smaxConnectTo(host);
  if(status) {
    fprintf(stderr, "ERROR! SMA-X connection error: %s\n", smaxErrorDescription(status));
    return status;
  }

  status = smaxShareField(table, &f);
  smaxDisconnect();

  if(status) fprintf(stderr, "ERROR! SMA-X error: %s\n", smaxErrorDescription(status));

  return status;
}



