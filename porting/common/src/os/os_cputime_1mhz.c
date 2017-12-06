#include "os/os_cputime.h"

/**
 * This module implements cputime functionality for timers whose frequency is
 * exactly 1 MHz.
 */

#if defined(OS_CPUTIME_FREQ_1MHZ)

/**
 * @addtogroup OSKernel Operating System Kernel
 * @{
 *   @defgroup OSCPUTime High Resolution Timers
 *   @{
 */

/**
 * os cputime nsecs to ticks
 *
 * Converts the given number of nanoseconds into cputime ticks.
 *
 * @param usecs The number of nanoseconds to convert to ticks
 *
 * @return uint32_t The number of ticks corresponding to 'nsecs'
 */
uint32_t
os_cputime_nsecs_to_ticks(uint32_t nsecs)
{
    return (nsecs + 999) / 1000;
}

/**
 * os cputime ticks to nsecs
 *
 * Convert the given number of ticks into nanoseconds.
 *
 * @param ticks The number of ticks to convert to nanoseconds.
 *
 * @return uint32_t The number of nanoseconds corresponding to 'ticks'
 */
uint32_t
os_cputime_ticks_to_nsecs(uint32_t ticks)
{
    return ticks * 1000;
}

/**
 *   @} OSCPUTime
 * @} OSKernel
 */

#endif
