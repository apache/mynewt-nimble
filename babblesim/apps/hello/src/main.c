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

#include <stdio.h>
#include "os/mynewt.h"

int
mynewt_main(int argc, char **argv)
{
    int i;

    sysinit();

    printf("hello: started\n");

    for (i = 0;; i++) {
        os_time_delay(OS_TICKS_PER_SEC);
        printf("hello: %d s (os time %lu)\n", i + 1, (unsigned long)os_time_get());
    }

    return 0;
}
