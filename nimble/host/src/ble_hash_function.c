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

#include <string.h>
#include "host/ble_hash_function.h"

void
ble_hash_function_blob(const unsigned char *s, unsigned int len, ble_hash_key h)
{
    size_t j;

    while (len--) {
        j = sizeof(ble_hash_key)-1;

        while (j) {
            h[j] = ((h[j] << 7) | (h[j-1] >> 1)) + h[j];
            --j;
        }

        h[0] = (h[0] << 7) + h[0] + *s++;
    }
}
