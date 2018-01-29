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

#include <stdbool.h>
#include <stdint.h>

#include <pthread.h>

int main(void)
{
    void start_nimble(void);
    start_nimble();

    // os_sched_start();

    /* Start FreeRTOS scheduler. */
    //vTaskStartScheduler();

    int ret = 0;
    pthread_exit(&ret);

    while (true)
    {
        pthread_yield();
        // FreeRTOS should not be here...
	    // FreeRTOS goes back to the start of stack
        // in vTaskStartScheduler function.
    }
}
