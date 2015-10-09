/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <stdint.h>
#include "nimble/ble.h"

void
htole16(uint8_t *buf, uint16_t x)
{
    buf[0] = (uint8_t)x;
    buf[1] = (uint8_t)(x >> 8);
}

void
htole32(uint8_t *buf, uint32_t x)
{
    buf[0] = (uint8_t)x;
    buf[1] = (uint8_t)(x >> 8);
    buf[2] = (uint8_t)(x >> 16);
    buf[3] = (uint8_t)(x >> 24);
}

void
htole64(uint8_t *buf, uint64_t x)
{
    buf[0] = (uint8_t)x;
    buf[1] = (uint8_t)(x >> 8);
    buf[2] = (uint8_t)(x >> 16);
    buf[3] = (uint8_t)(x >> 24);
    buf[4] = (uint8_t)(x >> 32);
    buf[5] = (uint8_t)(x >> 40);
    buf[6] = (uint8_t)(x >> 48);
    buf[7] = (uint8_t)(x >> 56);
}

uint16_t
le16toh(uint8_t *buf)
{
    uint16_t x;

    x = buf[0];
    x |= (uint16_t)buf[1] << 8;

    return x;
}

uint32_t
le32toh(uint8_t *buf)
{
    uint32_t x;

    x = buf[0];
    x |= (uint32_t)buf[1] << 8;
    x |= (uint32_t)buf[2] << 16;
    x |= (uint32_t)buf[3] << 24;

    return x;
}

uint64_t
le64toh(uint8_t *buf)
{
    uint64_t x;

    x = buf[0];
    x |= (uint64_t)buf[1] << 8;
    x |= (uint64_t)buf[2] << 16;
    x |= (uint64_t)buf[3] << 24;
    x |= (uint64_t)buf[4] << 32;
    x |= (uint64_t)buf[5] << 40;
    x |= (uint64_t)buf[6] << 48;
    x |= (uint64_t)buf[7] << 54;

    return x;
}

