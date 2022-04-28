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


/**
 * @addtogroup HAL
 * @{
 *   @defgroup HALSystem HAL System
 *   @{
 */

#ifndef H_HAL_SYSTEM_
#define H_HAL_SYSTEM_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * System reset.
 */
void hal_system_reset(void) __attribute((noreturn));

/**
 * Called by bootloader to start loaded program.
 */
void hal_system_start(void *img_start) __attribute((noreturn));

/**
 * Called by split app loader to start the app program.
 */
void hal_system_restart(void *img_start) __attribute((noreturn));

/**
 * Returns non-zero if there is a HW debugger attached.
 */
int hal_debugger_connected(void);

/**
 * Reboot reason
 */
enum hal_reset_reason {
    /** Power on Reset */
    HAL_RESET_POR = 1,
    /** Caused by Reset Pin */
    HAL_RESET_PIN = 2,
    /** Caused by Watchdog */
    HAL_RESET_WATCHDOG = 3,
    /** Soft reset, either system reset or crash */
    HAL_RESET_SOFT = 4,
    /** Low supply voltage */
    HAL_RESET_BROWNOUT = 5,
    /** Restart due to user request */
    HAL_RESET_REQUESTED = 6,
    /** System Off, wakeup on external interrupt*/
    HAL_RESET_SYS_OFF_INT = 7,
    /** Restart due to DFU */
    HAL_RESET_DFU = 8,
};

/**
 * Return the reboot reason
 *
 * @return A reboot reason
 */
enum hal_reset_reason hal_reset_cause(void);

/**
 * Return the reboot reason as a string
 *
 * @return String describing previous reset reason
 */
const char *hal_reset_cause_str(void);

/**
 * Starts clocks needed by system
 */
void hal_system_clock_start(void);

/**
 * Reset callback to be called before an reset happens inside hal_system_reset()
 */
void hal_system_reset_cb(void);

#ifdef __cplusplus
}
#endif

#endif /* H_HAL_SYSTEM_ */

/**
 *   @} HALSystem
 * @} HAL
 */
