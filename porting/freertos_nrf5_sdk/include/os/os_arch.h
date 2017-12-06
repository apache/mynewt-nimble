#ifndef _OS_ARCH_H
#define _OS_ARCH_H

#include <stdint.h>
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int os_sr_t;
typedef int os_stack_t;

#define OS_ALIGNMENT (4)
#define OS_TICKS_PER_SEC (configTICK_RATE_HZ)

#define OS_ENTER_CRITICAL(unused) do { (void)unused; vPortEnterCritical(); } while (0)
#define OS_EXIT_CRITICAL(unused) do { (void)unused; vPortExitCritical(); } while (0)

#ifdef __cplusplus
}
#endif

#endif /* _OS_ARCH_H */
