/**
 * @file
 *
 * @author Attila Kovacs
 * @date   Created on 12 December 2020
 *
 * @brief
 *          Simple API for sending and receiving program broadcast messages through SMA-X.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "redisx.h"
#include "smax.h"

#define MESSAGES_ID         "messages"          ///< Redis PUB_SUB channel head used for program messages
#define MESSAGES_PREFIX     MESSAGES_ID X_SEP   ///< Prefix for Redis PUB/SUB channel for program messages (e.g. "messages:")

typedef struct MessageProcessor {
  int id;
  char *pattern;
  char *host;
  char *prog;
  char *type;
  void (*call)(XMessage *);
  struct MessageProcessor *next, *prior;
} MessageProcessor;


static char *senderID;

static MessageProcessor *firstProc;
static pthread_mutex_t listMutex = PTHREAD_MUTEX_INITIALIZER;
static int nextID;


static void ProcessMessage(const char *pattern, const char *channel, const char *msg, int length);


static int SendMessage(const char *type, const char *text) {
  const char *id = senderID ? senderID : smaxGetProgramID();
  char *channel;
  char *tsmsg;
  int n;

  if(!type) return X_NULL;
  if(!text) return X_NULL;

  channel = malloc(sizeof(MESSAGES_PREFIX) + strlen(id) + X_SEP_LENGTH + strlen(type));
  if(!channel) return X_NULL;

  tsmsg = malloc(strlen(text) + X_TIMESTAMP_LENGTH + 3);
  if(!tsmsg) {
    free(channel);
    return X_NULL;
  }

  sprintf(channel, MESSAGES_PREFIX "%s" X_SEP "%s", id, type);
  n = sprintf(tsmsg, "%s @", text);
  smaxTimestamp(&tsmsg[n]);

  n = redisxNotify(smaxGetRedis(), channel, tsmsg);

  free(channel);
  free(tsmsg);

  return n;
}

/**
 * Sets the sender ID for outgoing program messages. By default the sender ID is &lt;host&gt;:&lt;program&gt;
 * for the program that calls this function, but it can be modified to use some other
 * SMA-X style hierarchical ID also.
 *
 *
 * @param id        The new sender ID for outgoing program messages, or NULL to reinstate the
 *                  default &lt;host&gt;:&lt;program&gt; style ID. The argument is not referenced and
 *                  can be deallocated as desired after the call without affecting the newly
 *                  defined message ID.
 */
void smaxSetMessageSenderID(const char *id) {
  pthread_mutex_lock(&listMutex);

  if(senderID) free(senderID);
  senderID = xStringCopyOf(id);

  pthread_mutex_unlock(&listMutex);
}

/**
 * Broadcast a program status update via SMA-X.
 *
 * @param msg       Message text
 * @return          X_SUCCESS (0), or else an X error.
 *
 * @sa sendInfo()
 */
int smaxSendStatus(const char *msg) {
  return SendMessage(SMAX_MSG_STATUS, msg);
}

/**
 * Broadcast an informational message via SMA-X. These should be confirmations or essential
 * information reported back to users. Non-essential information should be sent with
 * sendDetail() instead.
 *
 * @param msg       Message text
 * @return          X_SUCCESS (0), or else an X error.
 *
 * @sa sendDetail()
 * @sa sendStatus()
 */
int smaxSendInfo(const char *msg) {
  return SendMessage(SMAX_MSG_INFO, msg);
}

/**
 * Broadcast non-essential verbose informational detail via SMA-X.
 *
 * @param msg       Message text
 * @return          X_SUCCESS (0), or else an X error.
 */
int smaxSendDetail(const char *msg) {
  return SendMessage(SMAX_MSG_DETAIL, msg);
}


/**
 * Broadcast a debugging message via SMA-X (e.g. program traces).
 *
 * @param msg       Message text
 * @return          X_SUCCESS (0), or else an X error.
 */
int smaxSendDebug(const char *msg) {
  return SendMessage(SMAX_MSG_DEBUG, msg);
}

/**
 * Broadcast a warning message via SMA-X. Warnings should be used for any potentially
 * problematic issues that nonetheless do not impair program functionality.
 *
 * @param msg       Message text
 * @return          X_SUCCESS (0), or else an X error.
 *
 * @sa smaxSendError();
 * @sa smaxSendDebug();
 *
 */
int smaxSendWarning(const char *msg) {
  return SendMessage(SMAX_MSG_WARNING, msg);
}

/**
 * Broadcast an error message via SMA-X. Errors should be used for an issues
 * that impair program functionality.
 *
 * @param msg       Message text
 * @return          X_SUCCESS (0), or else an X error.
 *
 * @sa smaxSendWarning();
 * @sa smaxSendDebug();
 *
 */
int smaxSendError(const char *msg) {
  return SendMessage(SMAX_MSG_ERROR, msg);
}

/**
 * Broadcast a progress update over SMA-X.
 *
 * @param fraction  (0.0:1.0) Completion fraction.
 * @param msg       Message text
 * @return          X_SUCCESS (0), or else an X error.
 */
int smaxSendProgress(double fraction, const char *msg) {
  char *progress;
  int result;

  if(!msg) return X_NULL;

  progress = malloc(10 + strlen(msg));
  if(!progress) return X_NULL;

  sprintf(progress, "%.1f %s", (100.0 * fraction), msg);

  result = SendMessage(SMAX_MSG_DETAIL, progress);
  free(progress);

  return result;
}


/**
 * Adds a message processor function for a specific host (or all hosts), a specific program
 * (or all programs), and a specific message type (or all message types).
 *
 * @param host      Host name where messages originate from, or "*" or NULL if any.
 * @param prog      Program name of message originator, or "*" or NULL if any.
 * @param type      Message type, or "*" or NULL if any.
 * @param f         Callback function
 * @return          Serial ID number (> 0) of the message processor, or
 *                  X_NULL if callback function is null, or
 *                  X_FAILURE if malloc failed.
 *
 * @sa smaxRemoveMessageProcessor()
 */
int smaxAddMessageProcessor(const char *host, const char *prog, const char *type, void (*f)(XMessage *)) {
  MessageProcessor *p;
  int L = sizeof(MESSAGES_PREFIX) + 2 * X_SEP_LENGTH + 1;   // Empty pattern, e.g. "messages:::"
  int result;

  if(!f) return X_NULL;

  p = (MessageProcessor *) calloc(1, sizeof(MessageProcessor));
  if(!p) return X_FAILURE;

  p->call = f;
  p->id = ++nextID;
  p->prior = NULL;

  pthread_mutex_lock(&listMutex);

  if(firstProc) firstProc->prior = p;
  else redisxAddSubscriber(smaxGetRedis(), MESSAGES_PREFIX, ProcessMessage);

  p->next = firstProc;
  firstProc = p;

  pthread_mutex_unlock(&listMutex);

  if(host) if(strcmp(host, "*")) p->host = xStringCopyOf(host);
  if(prog) if(strcmp(prog, "*")) p->prog = xStringCopyOf(prog);
  if(type) if(strcmp(type, "*")) p->type = xStringCopyOf(type);

  if(!host) host = "*";
  if(!prog) prog = "*";
  if(!type) type = "*";

  L += strlen(host) + strlen(prog) + strlen(type);
  p->pattern = malloc(L);
  if(!p->pattern) {
    free(p);
    return X_NULL;
  }

  sprintf(p->pattern, MESSAGES_PREFIX "%s" X_SEP "%s" X_SEP "%s", host, prog, type);
  result = redisxSubscribe(smaxGetRedis(), p->pattern);

  return result < 0 ? result : p->id;
}

static void DefaultProcessor(XMessage *m) {
  if(!strcmp(m->type, SMAX_MSG_ERROR)) fprintf(stderr, "ERROR! %s(%s): %s.\n", m->prog, m->host, m->text);
  if(!strcmp(m->type, SMAX_MSG_WARNING)) fprintf(stderr, "WARNING! %s(%s): %s.\n", m->prog, m->host, m->text);
  if(!strcmp(m->type, SMAX_MSG_INFO)) printf(" %s(%s): %s.\n", m->prog, m->host, m->text);
  if(!strcmp(m->type, SMAX_MSG_DETAIL)) printf(" ... %s(%s): %s.\n", m->prog, m->host, m->text);
  if(!strcmp(m->type, SMAX_MSG_DEBUG)) printf("DEBUG> %s(%s): %s.\n", m->prog, m->host, m->text);
  if(!strcmp(m->type, SMAX_MSG_PROGRESS)) {
    char *tail;
    double d = strtod(m->text, &tail);
    if(errno == ERANGE || tail == m->text) printf(" %s(%s): %s\r", m->prog, m->host, m->text);
    else printf(" %s(%s) [%5.1f] %s\r", m->prog, m->host, d, m->text);
  }
}

/**
 * Report messages to stdout/stderr in default formats.
 *
 * @param host      Host name where messages originate from, or "*" or NULL if any.
 * @param prog      Program name of message originator, or "*" or NULL if any.
 * @param type      Message type, or "*" or NULL if any.
 *
 * @return          Serial ID number (> 0) of the message processor, or X_NULL.
 */
int smaxAddDefaultMessageProcessor(const char *host, const char *prog, const char *type) {
  return smaxAddMessageProcessor(host, prog, type, DefaultProcessor);
}


/**
 * Stops a running message processor.
 *
 * @param id    Message processor ID, as returned by smaxAddMessageProcessor()
 * @return      X_SUCCESS (0) if successful, or X_NULL if no message processor is running by that ID.
 *
 * @sa smaxAddMessageProcessor()
 */
int smaxRemoveMessageProcessor(int id) {
  MessageProcessor *p;

  pthread_mutex_lock(&listMutex);

  if(!firstProc) {
    pthread_mutex_unlock(&listMutex);
    return X_SUCCESS;
  }

  for(p = firstProc; p; p = p->next) if(p->id == id) {
    if(p->prior) p->prior->next = p->next;
    else firstProc = p->next;
    break;
  }

  if(!firstProc) redisxRemoveSubscribers(smaxGetRedis(), ProcessMessage);

  pthread_mutex_unlock(&listMutex);

  if(!p) return X_NULL;

  redisxUnsubscribe(smaxGetRedis(), p->pattern);

  if(p->pattern) free(p->pattern);
  if(p->host) free(p->host);
  if(p->prog) free(p->prog);
  if(p->type) free(p->type);

  free(p);

  return X_SUCCESS;
}

/**
 * RedisX subscriber function, which calls the appropriate message processor(s).
 *
 * @sa redisxAddSubscriber()
 */
static void ProcessMessage(const char *pattern, const char *channel, const char *msg, int length) {
  MessageProcessor *p;
  XMessage m;
  char *ts;

  if(!channel) return;

  if(xMatchNextID(MESSAGES_ID, channel) != X_SUCCESS) return;


  // Mark the start of each message property in the channel specification...
  m.host = xNextIDToken(channel);
  if(!m.host) return;

  m.prog = xNextIDToken(m.host);
  if(!m.prog) return;

  m.type = xNextIDToken(m.prog);
  if(!m.type) return;

  //if(xNextIDToken(m.type)) return;    // The channel has additional (unexpected) ID tokens...

  ts = strrchr(msg, '@');
  if(ts) {
    m.timestamp = smaxGetTime(&ts[1]);
    if(m.timestamp) *ts = '\0';
  }

  m.text = xStringCopyOf(msg);

  pthread_mutex_lock(&listMutex);

  if(!firstProc) {
    pthread_mutex_unlock(&listMutex);
    return;
  }

  // Now, create properly-terminated message fields...
  m.host = xCopyIDToken(m.host);
  m.prog = xCopyIDToken(m.prog);
  m.type = xCopyIDToken(m.type);

  for(p = firstProc; p != NULL; p = p->next) {
    if(p->host) if(strcmp(p->host, m.host)) continue;
    if(p->prog) if(strcmp(p->prog, m.prog)) continue;
    if(p->type) if(strcmp(p->type, m.type)) continue;

    p->call(&m);
  }

  pthread_mutex_unlock(&listMutex);

  // Release locally allocated resources...
  if(m.host) free(m.host);
  if(m.prog) free(m.prog);
  if(m.type) free(m.type);
}


