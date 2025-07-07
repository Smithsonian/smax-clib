/**
 *
 * @date Created on: Apr 8, 2019
 * @author Attila Kovacs
 *
 *  Simple command-line tool for querying the SMA-X database.
 */

#define _POSIX_C_SOURCE 199309L   ///< for nanosleep()

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <popt.h>

#include <redisx.h>
#include <xjson.h>

#include "smax.h"

/// \cond PRIVATE
#define NO_SUCH_KEY             1
#define NOT_ENOUGH_TOKENS       2

#define RED   "\x1B[31m"
#define GRN   "\x1B[32m"
#define YEL   "\x1B[33m"
#define BLU   "\x1B[34m"
#define MAG   "\x1B[35m"
#define CYN   "\x1B[36m"
#define WHT   "\x1B[37m"
#define RST   "\x1B[0m"
/// \endcond

static boolean showMeta = FALSE, showList = FALSE, printErrors = FALSE, json = FALSE;
static XType type = X_UNKNOWN;
static int count = -1;
static char *host;

static int printValue(const char *group, const char *key);
static int listEntries(const char *group, const char *key);

static void printVersion(const char *name) {
  printf("%s %s\n", name, SMAX_VERSION_STRING);
}

int main(int argc, const char *argv[]) {
  static const char *fn = "smaxValue";

  int port = REDISX_TCP_PORT;
  char *sType = NULL;
  char *user = NULL;
  char *password = NULL;
  int repeat = 1;
  double interval = 1.0;
  int verbose = FALSE;
  int debug = FALSE;

  struct poptOption options[] = { //
          {"host",       'h', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,    &host,     0, "Server hostname.", "<hostname>"}, //
          {"port",       'p', POPT_ARG_INT    | POPT_ARGFLAG_SHOW_DEFAULT,    &port,     0, "Server port.", "<port>"}, //
          {"user",       'u', POPT_ARG_STRING, &user,        0, "Used to send ACL style 'AUTH username pass'. Needs -a.", "<username>"}, //
          {"pass",       'a', POPT_ARG_STRING, &password,    0, "Password to use when connecting to the server.", "<password>"}, //
          {"type",       't', POPT_ARG_STRING, &sType,       0, "Print as <type>, e.g. 'int8', 'float', 'string', 'raw'.", "<type>"}, //
          {"count",      'n', POPT_ARG_INT,    &count,       0, "Print as <count> number of elements.", "<count>"}, //
          {"meta",       'm', POPT_ARG_NONE,   &showMeta,    0, "Print metadata.", NULL}, //
          {"list",       'l', POPT_ARG_NONE,   &showList,    0, "List field names contained in structures.", NULL}, //
          {"repeat",     'r', POPT_ARG_INT,    &repeat,      0, "Execute specified command this many times.", "<times>"}, //
          {"interval",   'i', POPT_ARG_DOUBLE | POPT_ARGFLAG_SHOW_DEFAULT, &interval,     0, "When -r is used, waits this many seconds before repeating.  " //
                  "It is possible to specify sub-second times like -i 0.1.", "<seconds>" //
          }, //
          {"json",         0, POPT_ARG_NONE,   &json,        0, "Output in JSON format", NULL}, //
          {"errors",       0, POPT_ARG_NONE,   &printErrors, 0, "Print errors.", NULL }, //
          {"verbose",      0, POPT_ARG_NONE,   &verbose,     0, "Verbose mode.", NULL }, //
          {"debug",        0, POPT_ARG_NONE   | POPT_ARGFLAG_DOC_HIDDEN,   &debug,       0, "Debug mode. Prints all network traffic.", NULL }, //
          {"version",      0, POPT_ARG_NONE,   NULL,       'v', "Output version and exit.", NULL }, //
          POPT_AUTOHELP POPT_TABLEEND //
  };

  int i, rc;
  const char *group = NULL;
  char *key = NULL, *id, **cmdargs;

  poptContext optcon = poptGetContext(fn, argc, argv, options, 0);

  poptSetOtherOptionHelp(optcon, "[OPTIONS] [table] key");

  if(argc < 2) {
    poptPrintUsage(optcon, stdout, 0);
    exit(1);
  }

  while((rc = poptGetNextOpt(optcon)) != -1) {
    if(rc < -1) {
      fprintf(stderr, "ERROR! Bad syntax. Try running with --help to see command-line options.\n");
      exit(1);
    }

    switch(rc) {
      case 'v': printVersion(fn); return 0;
    }
  }

  if(repeat < 1) repeat = 1;
  if(sType) type = smaxTypeForString(sType);

  cmdargs = (char **) poptGetArgs(optcon);
  if(cmdargs) {
    for(i = 0; i < 2 && cmdargs[i]; i++) {
      if(group) key = cmdargs[i];
      else group = cmdargs[i];
    }
  }

  smaxSetPipelined(FALSE);

  if(!host) host = getenv("SMAX_HOST");
  if(host || port > 0) smaxSetServer(host, port);
  if(verbose) smaxSetVerbose(TRUE);
  if(debug) xSetDebug(TRUE);
  if(user || password) smaxSetAuth(user, password);

  if(!group) {
    poptPrintUsage(optcon, stdout, 0);
    exit(1);
  }

  id = xGetAggregateID(group, key);
  if(!id) return -1;

  for(i = 0; i < repeat; i++) {
    if(i > 0 && interval > 0) {
      struct timespec sleeptime;
      sleeptime.tv_sec = (int) interval;
      sleeptime.tv_nsec = 1000000000 * (interval - sleeptime.tv_sec);
      nanosleep(&sleeptime, NULL);
    }

    if(showList) return printValue(id, NULL);
    else {
      if(xSplitID(id, &key)) return -1;
      return printValue(id, key);
    }
  }

  poptFreeContext(optcon);

  return 0;
}

static int printJSON(const char *group, const char *key, XMeta *meta) {
  char *id = xGetAggregateID(group, key);
  char *str;
  int status = X_SUCCESS;
  XField *f = smaxPullField(id, meta, &status);

  if(id) free(id);

  if(status != X_SUCCESS) {
    fprintf(stderr, "ERROR! %s\n", xErrorDescription(status));
    return status;
  }

  xReduceField(f);
  str = xjsonFieldToString(f);

  if(str) {
    printf("%s\n", str);
    free(str);
  }
  else printf("(nil)\n");

  xDestroyField(f);
  return 0;
}

static int printValue(const char *group, const char *key) {
  XMeta meta = X_META_INIT;
  char *value = NULL;
  int status = X_SUCCESS;

  if(key) {
    status = smaxConnect();

    if(status) {
      fprintf(stderr, "ERROR! SMA-X init: %s\n", smaxErrorDescription(status));
      return status;
    }

    smaxSetResilient(FALSE);

    if(json) status = printJSON(group, key, &meta);
    else value = smaxPullRaw(group, key, &meta, &status);

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

  if(json) {

  }

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
