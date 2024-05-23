/**
 * \file
 *
 * \date Jun 25, 2019
 * \author Attila Kovacs
 *
 * \brief
 *      A set of private SMA-X routines used by the API library but should not be exposed outside.
 *
 */


#ifndef SMAX_PRIVATE_H_
#define SMAX_PRIVATE_H_

#include "smax.h"
#include "redisx.h"

#define RELEASEID       "<release>"     ///< Redis PUB/SUB channel prefix for wait release notifications.

/// \cond PROTECTED


typedef struct PullRequest {
  char *group;
  char *key;
  void *value;      ///< Pointer to storage (such as double*), or pointer to pointers (char**) for X_STRING
  XType type;
  int count;
  XMeta *meta;
  struct PullRequest *next;
} PullRequest;

/**
 * An single entry (array of doubles) in a SMA-X buffer,
 */
typedef struct Entry {
  double t;
  double *values;
} Entry;

/**
 * A buffered sequence of SMA-X numerical data.
 *
 */
typedef struct Buffer {
  pthread_mutex_t mutex;
  int id;
  char *channel;
  char *table;
  char *key;
  int count;
  int size;
  int firstIndex;
  int n;
  Entry *entries;
  struct Buffer *next;
} Buffer;

int smaxRead(PullRequest *req, int channel);
int smaxWrite(const char *group, const XField *f);
void smaxDestroyPullRequest(PullRequest *p);
int smaxProcessReadResponse(RESP *reply, PullRequest *req);
void smaxProcessPipedWritesAsync(RESP *reply);
unsigned char smaxGetHashLookupIndex(const char *group, int lGroup, const char *key, int lKey);
char *smaxGetUpdateChannelPattern(const char *table, const char *key);
int smaxStorePush(const char *table, const XField *field);
void smaxTransmitErrorHandler(Redis *r, int channel, const char *op);
int smaxScriptError(const char *name, int status);
int smaxScriptErrorAsync(const char *name, int status);
boolean smaxIsDisabled();

/// \endcond

#endif /* SMAX_PRIVATE_H_ */
