/** @file common.h
 *
 *  @author Sasaji
 *  @date   2023/04/07
 *
 * 	@brief  common
 */

#ifndef COMMON_H
#define	COMMON_H

#include <xc.h>

#if defined(__PIC32MX__)
 #ifndef __PIC32__
  #define __PIC32__ 1
 #endif
#endif

#ifdef __PIC32_MEMORY_SIZE__
 #if __PIC32_MEMORY_SIZE__ > 64
  #define DEBUG_ENABLE
 #endif
#endif

#endif	/* COMMON_H */

