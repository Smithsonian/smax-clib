/**
 * @file
 *
 * @date Created on: Jun 20, 2020
 * @author Attila Kovacs
 *
 *      Simple program to test sharing and pulling nexted structures, and other structure functions.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smax.h"

#define TABLE   "_test_" X_SEP "simple"
#define NAME    "struct"


static void checkStatus(char *op, int status) {
  if(status >= 0) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}


static int cmpStruct(XStructure *a, XStructure *b) {
  XField *A;

  for(A = a->firstField; A != NULL; A = A->next) {
    XField *B = xGetField(b, A->name);

    if(!B) return X_NULL;
    if(A->type != B->type) return X_TYPE_INVALID;

    if(A->type == X_STRUCT) {
      if(cmpStruct((XStructure *) A->value, (XStructure *) B->value)) return X_STRUCT_INVALID;
    }
    else if(strcmp(A->value, B->value)) return X_PARSE_ERROR;
  }

  return 0;
}


static int testStructFunc(XStructure *s) {
  XField *f;
  char *override = "override";

  // Removing a field from the structure...
  f = xRemoveField(s, "field1");
  if(f) free(f);

  // Accessing non-existent field...
  if(xGetField(s, "noSuchField")) checkStatus("get(noSuchField)", X_FAILURE);
  if(xRemoveField(s, "noSuchField")) checkStatus("get(noSuchField)", X_FAILURE);

  // Overwriting a field.
  f = smaxCreateScalarField("field2", X_STRING, &override);
  if(!f) checkStatus("create field", X_FAILURE);

  f = xSetField(s, f);
  if(!f) checkStatus("overwrite", X_FAILURE);

  // Destoying the structure(s)
  xDestroyStruct(s);
  s = NULL;

  return 0;
}


int main() {
  XStructure *s, *ss, *in;
  XMeta m = X_META_INIT;
  int status;
  float fValues[] = {1.0, 2.0, 3.0};

  //smaxSetVerbose(TRUE);

  // Set the fields of the parent structure.
  s = xCreateStruct();
  xSetField(s, smaxCreateIntField("field1", 1));
  xSetField(s, smaxCreate1DField("field2", X_FLOAT, 3, fValues));

  // Set the fields of the nested substructure.
  ss = xCreateStruct();
  xSetField(ss, smaxCreateStringField("field3", "hello!"));
  xSetSubstruct(s, "substruct", ss);

  smaxSetPipelined(FALSE);

  checkStatus("connect", smaxConnect());

  checkStatus("share", smaxShareStruct(TABLE X_SEP NAME, s));

  in = smaxPullStruct(TABLE X_SEP NAME, &m, &status);

  checkStatus("disconnect", smaxDisconnect());

  checkStatus("compare", cmpStruct(s, in));

  // Destoying the structure
  xDestroyStruct(in);
  in = NULL;

  // Now, test some various other structure functions...
  testStructFunc(s);

  printf("OK\n");
  return 0;
}



