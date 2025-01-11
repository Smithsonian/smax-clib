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
#include <popt.h>
#include <redisx.h>
#include <xjson.h>

#include "smax.h"

static XField f;
static char *host = SMAX_DEFAULT_HOSTNAME;
static char *delims = ",;";
static char *sType;
static boolean printErrors = FALSE, json = FALSE;

static void printVersion(const char *name) {
  printf("%s %s\n", name, SMAX_VERSION_STRING);
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

int main(int argc, const char *argv[]) {
  static const char *fn = "smaxWrite";

  int port = REDISX_TCP_PORT;
  char *dims = "";
  char *user = NULL;
  char *password = NULL;
  int verbose = FALSE;
  int debug = FALSE;

  struct poptOption options[] = { //
          {"host",       'h', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT,    &host,     0, "Server hostname.", "<hostname>"}, //
          {"port",       'p', POPT_ARG_INT    | POPT_ARGFLAG_SHOW_DEFAULT,    &port,     0, "Server port.", "<port>"}, //
          {"user",       'u', POPT_ARG_STRING, &user,        0, "Used to send ACL style 'AUTH username pass'. Needs -a.", "<username>"}, //
          {"pass",       'a', POPT_ARG_STRING, &password,    0, "Password to use when connecting to the server.", "<password>"}, //
          {"type",       't', POPT_ARG_STRING, &sType,       0, "Print as <type>, e.g. 'int8', 'float', 'string', 'raw'.  "
                  "Required if --json is not used.", "<type>"}, //
          {"dims",       'd', POPT_ARG_STRING, &dims,        0, "Dimensions (comma separated).  E.g. \"3,8\" for a 3 x 8 array.  "
                  "(default: empty string for scalars)", "<dimensions>"}, //
          {"delims",     'D', POPT_ARG_STRING | POPT_ARGFLAG_SHOW_DEFAULT, &delims,      0, "Delimiter characters separating array elements.  "
                  "You may use JSON escaping notation", "<delims>"}, //
          {"json",         0, POPT_ARG_NONE,   &json,        0, "Value is specified as a JSON fragment", NULL}, //
          {"errors",       0, POPT_ARG_NONE,   &printErrors, 0, "Print errors.", NULL }, //
          {"verbose",      0, POPT_ARG_NONE,   &verbose,     0, "Verbose mode.", NULL }, //
          {"debug",        0, POPT_ARG_NONE   | POPT_ARGFLAG_DOC_HIDDEN,   &debug,       0, "Debug mode. Prints all network traffic.", NULL }, //
          {"version",      0, POPT_ARG_NONE,   NULL,       'v', "Output version and exit.", NULL }, //
          POPT_AUTOHELP POPT_TABLEEND //
  };


  const char *id = NULL;
  char **cmdargs, *table = NULL, *value = NULL;
  int rc, n, status = X_SUCCESS;

  poptContext optcon = poptGetContext(fn, argc, argv, options, 0);

  poptSetOtherOptionHelp(optcon, "[OPTIONS] table:key value");

  while((rc = poptGetNextOpt(optcon)) != -1) {
    if(rc < -1) {
      fprintf(stderr, "ERROR! Bad syntax. Try running with --help to see command-line options.\n");
      exit(1);
    }

    switch(rc) {
      case 'v': printVersion(fn); return 0;
    }
  }

  cmdargs = (char **) poptGetArgs(optcon);
  if(cmdargs) {
    int i;
    for(i = 0; i < 2 && cmdargs[i]; i++) {
      if(id) value = cmdargs[i];
      else id = cmdargs[i];
    }
  }

  if(!json && !sType) {
    fprintf(stderr, "ERROR! Type must be set via -t <type>.\n");
    exit(1);
  }

  if(!id) {
    fprintf(stderr, "ERROR! Missing table:key and value arguments.\n");
    exit(1);
  }

  if(!value) {
    fprintf(stderr, "ERROR! value was not defined.\n");
    exit(1);
  }

  delims = xjsonUnescape(delims);

  smaxSetPipelined(FALSE);

  if(verbose) smaxSetVerbose(TRUE);
  if(debug) xSetDebug(TRUE);
  if(user || password) smaxSetAuth(user, password);
  if(port > 0) smaxSetServer(host, port);

  if(printErrors) xjsonSetErrorStream(stderr);

  if(json) {
    int line = 0;
    XField *f1 = xjsonParseFieldAt(&value, &line);
    if(!f1) {
      fprintf(stderr, "ERROR! JSON parse error.\n");
      exit(1);
    }
    f = *f1;
    free(f1);
    table = (char *) id;
  }
  else {
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
  }

  status = smaxConnectTo(host);
  if(status) {
    fprintf(stderr, "ERROR! SMA-X connection error: %s\n", smaxErrorDescription(status));
    return status;
  }

  status = smaxShareField(table, &f);
  smaxDisconnect();

  if(status) fprintf(stderr, "ERROR! SMA-X error: %s\n", smaxErrorDescription(status));

  poptFreeContext(optcon);

  return status;
}



