/*
 * multidim.c
 *
 *  Created on: Feb 28, 2022
 *      Author: Attila Kovacs
 *
 *      This snipplet shows how to read or write multi-dimensional arrays of values (in this case
 *      integers) from / to SMA-X.
 *
 *      We shall read/write under the ID of "_test_:example:my_multi_array" in the most generic way.
 *      We use the '_test_' top-level stem here because this here is not an operational value, and
 *      won't be logged and can be deleted any time.
 *
 *      For real SMA-X values your ID would not start with underscore, and follow the SMA-X
 *      naming specification and (system:subsystem:...) hierarchy
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include "smax.h"

// Just a simple function for checking for SMA-X errors. This one simply prints the
// error message, and exits the program with -1 if there was an error.
static void checkStatus(char *op, int status) {
  if(!status) return;
  fprintf(stderr, "ERROR! %s: %s\n", op, smaxErrorDescription(status));
  exit(-1);
}

int main(int argc, const char *argv[]) {
  int values[3][4][5] = {0};                    // A 3x4x5 in array, containing 60 elements in total.

  int ndims = 3;                                // The dimensionality of our data.
  // The shape of the data. While not strictly necessary, it's safest to use an array of X_MAX_DIMS in size
  int sizes[X_MAX_DIMS] = { 3, 4, 5 };          // We initialize the first 3 elements, but they could be set later too..
  XField *f;                                    // We'll wrap the data into an XField object that has its shape...
  XMeta meta = X_META_INIT;                     // We will read back metadata as well, but it's optional.

  // 1. Connect to SMA-X
  checkStatus("connect", smaxConnect());

  // 2A.1. Create the fields with the specified array data and sizes
  // Note, that the field will contain a copy of 'values', so changes
  // to our values array after this call won't change the field's data.
  //
  // If you want the field to reference your array directly,
  // you can pass NULL as the last argument to xCreateField(),
  // and set f->value to point to your array after. Note, however
  // that you should remember to de-reference your data (i.e. set
  // f->value = NULL) before destroying the field later, or else
  // it will free() your array as well...
  f = xCreateField("my_multi_array", X_INT, 3, sizes, values);
  if(f == NULL) {
    perror("ERROR! Could not create field");
    exit(-1);
  }

  // 2A.2. Set the field in SMA-X, including the shape of the multi-dimensional array
  checkStatus("share", smaxShareField("_test_:example", f));

  // 2B. Or, read the array of values from SMA-X into the buffer...
  checkStatus("pull", smaxPull("_test_:example", "my_multi_array", X_INT, xGetElementCount(ndims, sizes), values, &meta));

  // 3. When done, disconnect from SMA-X
  smaxDisconnect();

  // 4. clean up. When done using the field, destroy it.
  //    (don't forget to change the value to NULL if the field's value is referenced elsewhere!)
  xDestroyField(f);

  return 0;

}


