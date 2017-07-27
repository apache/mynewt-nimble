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

#ifndef H_BLE_UUID_
#define H_BLE_UUID_

#include <inttypes.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct os_mbuf;

enum {
    BLE_UUID_TYPE_16 = 16,
    BLE_UUID_TYPE_32 = 32,
    BLE_UUID_TYPE_128 = 128,
};

/* Generic UUID type, to be used only as a pointer */
typedef struct {
    uint8_t type;
} ble_uuid_t;

typedef struct {
    ble_uuid_t u;
    uint16_t value;
} ble_uuid16_t;

typedef struct {
    ble_uuid_t u;
    uint32_t value;
} ble_uuid32_t;

typedef struct {
    ble_uuid_t u;
    uint8_t value[16];
} ble_uuid128_t;

/* Universal UUID type, to be used for any-UUID static allocation */
typedef union {
    ble_uuid_t u;
    ble_uuid16_t u16;
    ble_uuid32_t u32;
    ble_uuid128_t u128;
} ble_uuid_any_t;

#define BLE_UUID16_INIT(uuid16)         \
    {                                   \
        .u.type = BLE_UUID_TYPE_16,     \
        .value = (uuid16),              \
    }

#define BLE_UUID32_INIT(uuid32)         \
    {                                   \
        .u.type = BLE_UUID_TYPE_32,     \
        .value = (uuid32),              \
    }

#define BLE_UUID128_INIT(uuid128...)    \
    {                                   \
        .u.type = BLE_UUID_TYPE_128,    \
        .value = { uuid128 },           \
    }

#define BLE_UUID16_DECLARE(uuid16) \
    ((ble_uuid_t *) (&(ble_uuid16_t) BLE_UUID16_INIT(uuid16)))

#define BLE_UUID32_DECLARE(uuid32) \
    ((ble_uuid_t *) (&(ble_uuid32_t) BLE_UUID32_INIT(uuid32)))

#define BLE_UUID128_DECLARE(uuid128...) \
    ((ble_uuid_t *) (&(ble_uuid128_t) BLE_UUID128_INIT(uuid128)))

#define BLE_UUID16(u) \
    ((ble_uuid16_t *) (u))

#define BLE_UUID32(u) \
    ((ble_uuid32_t *) (u))

#define BLE_UUID128(u) \
    ((ble_uuid128_t *) (u))

/** Includes trailing \0. */
#define BLE_UUID_STR_LEN (37)

int ble_uuid_init_from_buf(ble_uuid_any_t *uuid, const void *buf, size_t len);
int ble_uuid_cmp(const ble_uuid_t *uuid1, const ble_uuid_t *uuid2);
void ble_uuid_copy(ble_uuid_any_t *dst, const ble_uuid_t *src);
char *ble_uuid_to_str(const ble_uuid_t *uuid, char *dst);
uint16_t ble_uuid_u16(const ble_uuid_t *uuid);

int ble_uuid_to_s(char *dst, int dst_sz, const void *uuid128);

#ifdef __cplusplus
}
#endif

#endif /* _BLE_HOST_UUID_H */
