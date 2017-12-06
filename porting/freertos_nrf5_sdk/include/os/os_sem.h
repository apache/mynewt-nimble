#ifndef _OS_SEM_H_
#define _OS_SEM_H_

#include "os/os_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS.h"
#include "semphr.h"

struct os_sem
{
    SemaphoreHandle_t handle;
};

os_error_t os_sem_init(struct os_sem *sem, uint16_t tokens);
os_error_t os_sem_release(struct os_sem *sem);
os_error_t os_sem_pend(struct os_sem *sem, uint32_t timeout);
uint16_t os_sem_get_count(struct os_sem *sem);

#ifdef __cplusplus
}
#endif

#endif  /* _OS_SEM_H_ */
