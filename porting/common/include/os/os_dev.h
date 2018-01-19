/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef _OS_DEV_H
#define _OS_DEV_H

#include <os/os.h>

#include "os/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

struct os_dev;

/*
 * Initialization order, defines when a device should be initialized
 * by the Mynewt kernel.
 *
 */
#define OS_DEV_INIT_PRIMARY   (1)
#define OS_DEV_INIT_SECONDARY (2)
#define OS_DEV_INIT_KERNEL    (3)

#define OS_DEV_INIT_F_CRITICAL (1 << 0)


#define OS_DEV_INIT_PRIO_DEFAULT (0xff)

/**
 * Device status, so functions can ensure device is called in a
 * consistent state.
 */
#define OS_DEV_F_STATUS_READY     (1 << 0)
#define OS_DEV_F_STATUS_OPEN      (1 << 1)
#define OS_DEV_F_STATUS_SUSPENDED (1 << 2)
#define OS_DEV_F_INIT_CRITICAL    (1 << 3)

typedef int (*os_dev_init_func_t)(struct os_dev *, void *);
typedef int (*os_dev_open_func_t)(struct os_dev *, uint32_t,
        void *);
typedef int (*os_dev_suspend_func_t)(struct os_dev *, os_time_t, int);
typedef int (*os_dev_resume_func_t)(struct os_dev *);
typedef int (*os_dev_close_func_t)(struct os_dev *);

struct os_dev_handlers {
    os_dev_open_func_t od_open;
    os_dev_suspend_func_t od_suspend;
    os_dev_resume_func_t od_resume;
    os_dev_close_func_t od_close;
};

/*
 * Device structure.
 */
struct os_dev {
    struct os_dev_handlers od_handlers;
    os_dev_init_func_t od_init;
    void *od_init_arg;
    uint8_t od_stage;
    uint8_t od_priority;
    uint8_t od_open_ref;
    uint8_t od_flags;
    char *od_name;
    STAILQ_ENTRY(os_dev) od_next;
};

#define OS_DEV_SETHANDLERS(__dev, __open, __close)          \
    (__dev)->od_handlers.od_open = (__open);                \
    (__dev)->od_handlers.od_close = (__close);

static inline int
os_dev_suspend(struct os_dev *dev, os_time_t suspend_t, uint8_t force)
{
    if (dev->od_handlers.od_suspend == NULL) {
        return (0);
    } else {
        return (dev->od_handlers.od_suspend(dev, suspend_t, force));
    }
}

static inline int
os_dev_resume(struct os_dev *dev)
{
    if (dev->od_handlers.od_resume == NULL) {
        return (0);
    } else {
        return (dev->od_handlers.od_resume(dev));
    }
}

int os_dev_create(struct os_dev *dev, char *name, uint8_t stage,
        uint8_t priority, os_dev_init_func_t od_init, void *arg);
struct os_dev *os_dev_lookup(char *name);
int os_dev_initialize_all(uint8_t stage);
int os_dev_suspend_all(os_time_t, uint8_t);
int os_dev_resume_all(void);
struct os_dev *os_dev_open(char *devname, uint32_t timo, void *arg);
int os_dev_close(struct os_dev *dev);
void os_dev_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* _OS_DEV_H */
