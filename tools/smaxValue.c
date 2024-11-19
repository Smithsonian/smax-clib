/**
 * @file
 *
 * @date Created on: Apr 8, 2019
 * @author Attila Kovacs
 *
 *  Simple command-line tool for querying the SMA-X database.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "redisx.h"
#include "smax.h"

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RST   "\x1B[0m"

static boolean showMeta = FALSE, showList = FALSE, printErrors = FALSE;
static XType type = X_UNKNOWN;
static int count = -1;
static char *host = SMAX_DEFAULT_HOSTNAME;

static int printValue(const char *group, const char *key);
static int listEntries(const char *group, const char *key);
static void setOption(char *argv[], int *next);
static void usage();

#define NO_SUCH_KEY     1
#define NOT_ENOUGH_TOKENS       2

int main(int argc, char *argv[]) {
  const char *group = NULL;
  char *key = NULL, *id;
  int next = 1;

  if(argc < 2) usage();

  smaxSetPipelined(FALSE);

  while(next < argc) {
    if(argv[next][0] == '-') setOption(argv, &next);
    else if(key) {
      fprintf(stderr, "ERROR! Too many arguments.\n");
      usage();
    }
    else if(group) key = argv[next++];
    else group = argv[next++];
  }

  if(!group) usage();

  id = xGetAggregateID(group, key);
  if(!id) return -1;

  if(showList) return printValue(id, NULL);
  else {
    if(xSplitID(id, &key)) return -1;
    return printValue(id, key);
  }
}

static int printValue(const char *group, const char *key) {
  XMeta meta = X_META_INIT;
  char *value = NULL;
  int status = X_SUCCESS;

  if(key) {
    status = smaxConnectTo(host);

    if(status) {
      fprintf(stderr, "ERROR! SMA-X init: %d", status);
      return status;
    }

    smaxSetResilient(FALSE);
    value = smaxPullRaw(group, key, &meta, &status);
    smaxDisconnect();

    if(status) return smaxError("SMA-X", status);
  }
  else {
    type = X_STRUCT;
    showList = TRUE;
  }

  status = X_SUCCESS;

  if(key) {
    if(!value) {
      status = NO_SUCH_KEY;
      if(printErrors) fprintf(stderr, "WARNING! No such entry in SMA-X database.\n");
    }
    else if(showMeta) {
      char TS[X_TIMESTAMP_LENGTH], dims[X_MAX_STRING_DIMS], date[100];
      const struct tm *ts;

      xPrintDims(dims, meta.storeDim, meta.storeSizes);
      smaxTimeToString(&meta.timestamp, TS);

      ts = gmtime(&meta.timestamp.tv_sec);
      strftime(date, 100, "%Y-%m-%d %H:%M:%S", ts);

      printf("\n");
      printf(MAG " #" BLU " Type:   " RST "%s\n", smaxStringType(meta.storeType));
      printf(MAG " #" BLU " Size:   " RST "%s\n", dims);
      printf(MAG " #" BLU " Origin: " RST "%s\n", meta.origin[0] ? meta.origin : RED "<null>" RST);
      printf(MAG " #" BLU " Time:   " RST "%s (" GRN "%s.%03ld" CYN " UTC" RST ")\n", TS, date, (meta.timestamp.tv_nsec / 1000000));
      printf(MAG " #" BLU " Serial: " RST "%d\n", meta.serial);
      printf("\n");
    }
  }

  printf(" ");

  if(type == X_UNKNOWN) type = meta.storeType;

  if(type == X_STRUCT) {
    if(showList) return listEntries(group, key);
    type = X_STRING;
    count = 1;
  }

  if(count < 0) count = smaxGetMetaCount(&meta);
  if(count <= 0) type = X_RAW;

  if(type == X_RAW) {
    if(value == NULL) printf("(nil)\n");
    else {
      int i;
      for(i=0; i<meta.storeBytes; i++) putchar(value[i]);
      putchar('\n');
    }
  }
  else if(type == X_STRING) {
    // Print string arrays one string per row...
    int i, N, l=0, offset=0;

    N = count > 0 ? count : smaxGetMetaCount(&meta);

    for(i=0; i<N; i++) {
      if(value == NULL) printf("(nil)\n");
      if(offset >= meta.storeBytes) printf("(nil)");
      else printf("%s%n\n", &value[offset], &l);
      offset += (l+1);
    }
  }
  else {
    void *buf;
    int n;

    buf = malloc(count * xElementSizeOf(type));
    if(smaxStringToValues(value, buf, type, count, &n) < count) {
      status = NOT_ENOUGH_TOKENS;
      if(printErrors) fprintf(stderr, "WARNING! SMA-X data has fewer components.\n");
    }

    if(value) free(value);
    value = smaxValuesToString(buf, type, count, NULL, 0);

    printf("%s\n", value);
  }

  return status;
}

static int cmp(const void *a, const void *b) {
  return strcmp(*(const char **) a, *(const char **) b);
}

static int listEntries(const char *group, const char *key) {
  int i, n, status;
  char *id, **keys;

  status = smaxConnectTo(host);
  if(status) {
    fprintf(stderr, "ERROR! could not connect to SMA-X\n");
    return status;
  }

  id = xGetAggregateID(group, key);
  keys = redisxGetKeys(smaxGetRedis(), id, &n);

  smaxDisconnect();

  if(n < 0) {
    if(printErrors) fprintf(stderr, "WARNING! %s\n", smaxErrorDescription(n));
    printf("(nil)\n");
    return NO_SUCH_KEY;
  }

  qsort(keys, n, sizeof(char *), cmp);

  if(id) {
    printf(MAG "#" BLU " table '" RED "%s" BLU "' (%d fields) ----->\n" RST, id, n);
    free(id);
  }

  for(i=0; i<n; i++) printf(BLU " >" RST " %s\n", keys[i]);


  return X_SUCCESS;
}

static void setOption(char *argv[], int *next) {
  char *option = argv[(*next)++];
  option++;

  if(!strcmp(option, "m") || !strcmp(option, "-meta")) showMeta = TRUE;
  else if(!strcmp(option, "e") || !strcmp(option, "-errors")) printErrors = TRUE;
  else if(!strcmp(option, "l") || !strcmp(option, "-list")) showList = TRUE;
  else if(!strcmp(option, "t") || !strcmp(option, "-type")) type = smaxTypeForString(argv[(*next)++]);
  else if(!strcmp(option, "n") || !strcmp(option, "-count")) count = atoi(argv[(*next)++]);
  else if(!strcmp(option, "s") || !strcmp(option, "-server")) host = argv[(*next)++];
  else fprintf(stderr, "WARNING! no option: -%s\n", option);
}


static void usage() {
  printf("\n");
  printf("  Syntax: smaxValue [options] [table] key\n\n");
  printf("    [table]      hash table name.\n");
  printf("    key          The name of the variable.\n\n");
  printf("  Options:\n\n");
  printf("    -m           Print metadata.\n");
  printf("    -t <type>    Print as <type>, e.g. 'int8', 'float', 'string', 'raw'.\n");
  printf("    -n <count>   Print as <count> number of elements.\n");
  printf("    -l           List field names contained in structures.\n");
  printf("    -e           Print errors/warnings to stderr.\n");
  printf("    -s <host>    Use a specific host as the SMA-X database server.\n\n");
  printf("  This tool always returns a value (or the requested number of values),\n");
  printf("  It defaults to zero(es) for any elements not in the SMA-X database.\n");
  printf("  Check return value for errors:\n\n");
  printf("    0 - Normal return\n");
  printf("    1 - No such value in SMA-X database\n");
  printf("    2 - SMA-X has fewer than requested elements.\n\n");
  exit(0);
}
