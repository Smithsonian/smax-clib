/*
 * smaxScrub.c
 *
 *  Created on: Jul 4, 2020
 *      Author: Attila Kovacs
 */


// I. Obsolete data
//   1. Iterate through keys using SCAN command
//     - Ignore metadata and 'scripts'
//     - Iterate through fields using HSCAN command
//       > Check corresponding time stamp. If no timestamp or too old, UNLINK
//
// II. Orphaned metadata
//   1. Iterate through meta tables using SCAN
//     - Iterate through keys using HSCAN
//       > Check for corresponding entry in DB. If not UNLINK
