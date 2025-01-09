/**
 *
 * @date Created on: May 12, 2020
 * @author Attila Kovacs
 *
 *  Obtain the process name on platforms where the `__progname` is not defined by libc
 *  such as on LynxOS 3.1 / PowerPC...
 */

#ifndef LYNXOS_PROCNAME_H_
#define LYNXOS_PROCNAME_H_

#define DEFAULT_PROCESS_NAME    "anonymous"

#if __Lynx__ && __powerpc__
/**
 * Gets the process name for a given pid on LynxOS, by parsing process tables from /dev/mem.
 *
 * \param[in]   pid       Process ID, such as returned by getpid().
 * \param[out]  procName  String to return process name in (terminated, and with path stripped)
 * \param[in]   length    The maximum number of characters to print into procName.
 *
 * \return      0   if the process was successfully identified (i.e. alive)
 *              1   if there is no live process with that ID (returns default name)
 *             -1   if there was an error trying to look up the process name (return redault name).
 */
int getProcessName(int pid, char *procName, const int length);

#endif

#endif /* LYNXOS_PROCNAME_H_ */
