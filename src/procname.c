/**
 *
 * @date Created on: May 12, 2020
 * @author Attila Kovacs
 *
 *  Obtain the process name on LynxOS 3.1 / PowerPC, where the `__progname` macro does
 *  not exists...
 */

#include "procname.h"

#if __Lynx__ && __powerpc__

#include <errno.h>
#include <info.h>
#include <mem.h>
#include <proc.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>


// We'll parse the process table to match the PID to a process name...
int getProcessName(int pid, char *procName, const int length) {
  int fd, status;
  char *shortName;
  __pentry proc;

  // Default process name
  strncpy(procName, DEFAULT_PROCESS_NAME, length-1);
  procName[length-1] = '\0';

  if(pid <= 0 || pid > info(_I_NPROCTAB)) {
    errno = EINVAL;
    return -1;
  }

  fd = open("/dev/mem", O_RDONLY);
  if (fd < 0) return -1;

  lseek(fd, info(_I_PROCTAB) + pid * sizeof(struct pentry), 0);
  status = read(fd, &proc, sizeof(struct pentry));
  close(fd);

  if(status < 0) return -1;

  /* Ignore if slot is marked as free   */
  if(proc.pstate & PRFREE) return 1;

  shortName = strrchr(proc.pname, '/');
  if(shortName == NULL) shortName = proc.pname;
  else shortName++;

  strncpy(procName, shortName, length-1);
  procName[length-1] = '\0';    // Failsafe terminate.

  return 0;
}


#endif
