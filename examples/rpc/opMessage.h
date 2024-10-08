/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#ifndef _OPMESSAGE_H_RPCGEN
#define _OPMESSAGE_H_RPCGEN

#include <rpc/rpc.h>


#ifdef __cplusplus
extern "C" {
#endif

#define OPMSG_NODENAME_LENGTH 20
#define OPMSG_PROCNAME_LENGTH 33
#define OPMSG_TEXT_LENGTH 60
#define OPMSG_WARNING 0
#define OPMSG_SEVERE 1
#define OPMSG_EMERGENCY 2
#define OPMSG_SEND_MESSAGE 1
#define OPMSG_CLEAR_MESSAGE 2

struct oFMessage {
	short severity;
	short priority;
	int duration;
	char text[OPMSG_TEXT_LENGTH];
	short pID;
	short tag;
	char procName[OPMSG_PROCNAME_LENGTH];
	int action;
	long unixTime;
};
typedef struct oFMessage oFMessage;

#define OPMSGPROG 0x20000202
#define OPMSGVERS 1

#if defined(__STDC__) || defined(__cplusplus)
#define POSTOPMSG 1
extern  int * postopmsg_1(oFMessage *, CLIENT *);
extern  int * postopmsg_1_svc(oFMessage *, struct svc_req *);
extern int opmsgprog_1_freeresult (SVCXPRT *, xdrproc_t, caddr_t);

#else /* K&R C */
#define POSTOPMSG 1
extern  int * postopmsg_1();
extern  int * postopmsg_1_svc();
extern int opmsgprog_1_freeresult ();
#endif /* K&R C */

/* the xdr functions */

#if defined(__STDC__) || defined(__cplusplus)
extern  bool_t xdr_oFMessage (XDR *, oFMessage*);

#else /* K&R C */
extern bool_t xdr_oFMessage ();

#endif /* K&R C */

#ifdef __cplusplus
}
#endif

#endif /* !_OPMESSAGE_H_RPCGEN */
