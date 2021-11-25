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

#include <hal/hal_bsp.h>

/* put these in the data section so they are not cleared by _start */
static char *sbrkBase __attribute__ ((section (".data")));
static char *sbrkLimit __attribute__ ((section (".data")));
static char *brk __attribute__ ((section (".data")));

void
_sbrkInit(char *base, char *limit) {
    sbrkBase = base;
    sbrkLimit = limit;
    brk = base;
}

void *
_sbrk(int incr)
{
    void *prev_brk;

    if (incr < 0) {
        /* Returning memory to the heap. */
        incr = -incr;
        if (brk - incr < sbrkBase) {
            prev_brk = (void *)-1;
        } else {
            prev_brk = brk;
            brk -= incr;
        }
    } else {
        /* Allocating memory from the heap. */
        if (sbrkLimit - brk >= incr) {
            prev_brk = brk;
            brk += incr;
        } else {
            prev_brk = (void *)-1;
        }
    }

    return prev_brk;
}
