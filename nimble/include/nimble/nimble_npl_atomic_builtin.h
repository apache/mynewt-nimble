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

#ifndef _NIMBLE_NPL_ATOMIC_BUILTIN_H_
#define _NIMBLE_NPL_ATOMIC_BUILTIN_H_

/*
 * Generic NPL atomics on GCC/Clang __atomic builtins. A port may include
 * this from its nimble_npl_os.h instead of providing its own atomics.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t ble_npl_atomic_t;
typedef int32_t ble_npl_atomic_val_t;

static inline ble_npl_atomic_val_t
ble_npl_atomic_get(const ble_npl_atomic_t *target)
{
    return __atomic_load_n(target, __ATOMIC_SEQ_CST);
}

static inline ble_npl_atomic_val_t
ble_npl_atomic_set(ble_npl_atomic_t *target, ble_npl_atomic_val_t value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static inline ble_npl_atomic_val_t
ble_npl_atomic_add(ble_npl_atomic_t *target, ble_npl_atomic_val_t value)
{
    return __atomic_fetch_add(target, value, __ATOMIC_SEQ_CST);
}

static inline ble_npl_atomic_val_t
ble_npl_atomic_sub(ble_npl_atomic_t *target, ble_npl_atomic_val_t value)
{
    return __atomic_fetch_sub(target, value, __ATOMIC_SEQ_CST);
}

static inline ble_npl_atomic_val_t
ble_npl_atomic_or(ble_npl_atomic_t *target, ble_npl_atomic_val_t value)
{
    return __atomic_fetch_or(target, value, __ATOMIC_SEQ_CST);
}

static inline ble_npl_atomic_val_t
ble_npl_atomic_and(ble_npl_atomic_t *target, ble_npl_atomic_val_t value)
{
    return __atomic_fetch_and(target, value, __ATOMIC_SEQ_CST);
}

#ifdef __cplusplus
}
#endif

#endif /* _NIMBLE_NPL_ATOMIC_BUILTIN_H_ */
