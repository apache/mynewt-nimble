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

#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>

#include "os/mynewt.h"

#include "hal/hal_flash_int.h"
#include "mcu/mcu_sim.h"

char *native_flash_file;
static int file = -1;
static void *file_loc;

static int native_flash_init(const struct hal_flash *dev);
static int native_flash_read(const struct hal_flash *dev, uint32_t address,
        void *dst, uint32_t length);
static int native_flash_write(const struct hal_flash *dev, uint32_t address,
        const void *src, uint32_t length);
static int native_flash_erase_sector(const struct hal_flash *dev,
        uint32_t sector_address);
static int native_flash_sector_info(const struct hal_flash *dev, int idx,
        uint32_t *address, uint32_t *size);

static const struct hal_flash_funcs native_flash_funcs = {
    .hff_read = native_flash_read,
    .hff_write = native_flash_write,
    .hff_erase_sector = native_flash_erase_sector,
    .hff_sector_info = native_flash_sector_info,
    .hff_init = native_flash_init
};

#if MYNEWT_VAL(MCU_FLASH_STYLE_ST)
static const uint32_t native_flash_sectors[] = {
    0x00000000, /* 16 * 1024 */
    0x00004000, /* 16 * 1024 */
    0x00008000, /* 16 * 1024 */
    0x0000c000, /* 16 * 1024 */
    0x00010000, /* 64 * 1024 */
    0x00020000, /* 128 * 1024 */
    0x00040000, /* 128 * 1024 */
    0x00060000, /* 128 * 1024 */
    0x00080000, /* 128 * 1024 */
    0x000a0000, /* 128 * 1024 */
    0x000c0000, /* 128 * 1024 */
    0x000e0000, /* 128 * 1024 */
};
#elif MYNEWT_VAL(MCU_FLASH_STYLE_NORDIC)
static uint32_t native_flash_sectors[1024 * 1024 / 2048];
#else
#error "Need to specify either MCU_FLASH_STYLE_NORDIC or MCU_FLASH_STYLE_ST"
#endif

#define FLASH_NUM_AREAS   (int)(sizeof native_flash_sectors /           \
                                sizeof native_flash_sectors[0])

const struct hal_flash native_flash_dev = {
    .hf_itf = &native_flash_funcs,
    .hf_base_addr = 0,
    .hf_size = 1024 * 1024,
    .hf_sector_cnt = FLASH_NUM_AREAS,
    .hf_align = MYNEWT_VAL(MCU_FLASH_MIN_WRITE_SIZE),
    .hf_erased_val = 0xff,
};

static void
flash_native_erase(uint32_t addr, uint32_t len)
{
    memset(file_loc + addr, 0xff, len);
}

static void
flash_native_file_open(char *name)
{
    int created = 0;
    char tmpl[] = "/tmp/native_flash.XXXXXX";

    extern int ftruncate(int fd, off_t length);

    if (file != -1) {
        close(file);
        file = -1;
    }

    if (name) {
        file = open(name, O_RDWR);
        if (file < 0) {
            file = open(name, O_RDWR | O_CREAT, 0660);
            assert(file > 0);
            created = 1;
        }
    } else {
        file = mkstemp(tmpl);
        assert(file > 0);
        created = 1;
    }

    if (created) {
        if (ftruncate(file, native_flash_dev.hf_size) < 0) {
            assert(0);
        }
    }

    if (file_loc != NULL) {
        munmap(file_loc, native_flash_dev.hf_size);
    }

    file_loc = mmap(0, native_flash_dev.hf_size,
          PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
    assert(file_loc != MAP_FAILED);
    if (created) {
        flash_native_erase(0, native_flash_dev.hf_size);
    }

    /* If using a temporary file, unlink it immediately. */
    if (name == NULL) {
        remove(tmpl);
    }
}

static void
flash_native_ensure_file_open(void)
{
    if (file == 0) {
        flash_native_file_open(NULL);
    }
}

static int
flash_native_write_internal(uint32_t address, const void *src, uint32_t length,
                            int allow_overwrite)
{
    static uint8_t buf[256];
    uint32_t cur;
    uint32_t end;
    int chunk_sz;
    int rc;
    int i;

    if (length == 0) {
        return 0;
    }

    end = address + length;

    flash_native_ensure_file_open();

    cur = address;
    while (cur < end) {
        if (end - cur < sizeof buf) {
            chunk_sz = end - cur;
        } else {
            chunk_sz = sizeof buf;
        }

        /* Ensure data is not being overwritten. */
        if (!allow_overwrite) {
            rc = native_flash_read(NULL, cur, buf, chunk_sz);
            assert(rc == 0);
            for (i = 0; i < chunk_sz; i++) {
                assert(buf[i] == 0xff);
            }
        }

        cur += chunk_sz;
    }

    memcpy((char *)file_loc + address, src, length);

    return 0;
}

static int
native_flash_write(const struct hal_flash *dev, uint32_t address,
        const void *src, uint32_t length)
{
    assert(address % native_flash_dev.hf_align == 0);
    return flash_native_write_internal(address, src, length, 0);
}

int
flash_native_memset(uint32_t offset, uint8_t c, uint32_t len)
{
    memset(file_loc + offset, c, len);
    return 0;
}

static int
native_flash_read(const struct hal_flash *dev, uint32_t address, void *dst,
        uint32_t length)
{
    flash_native_ensure_file_open();
    memcpy(dst, (char *)file_loc + address, length);

    return 0;
}

static int
find_area(uint32_t address)
{
    int i;

    for (i = 0; i < FLASH_NUM_AREAS; i++) {
        if (native_flash_sectors[i] == address) {
            return i;
        }
    }

    return -1;
}

static int
flash_sector_len(int sector)
{
    uint32_t end;

    if (sector == FLASH_NUM_AREAS - 1) {
        end = native_flash_dev.hf_size + native_flash_sectors[0];
    } else {
        end = native_flash_sectors[sector + 1];
    }
    return end - native_flash_sectors[sector];
}

static int
native_flash_erase_sector(const struct hal_flash *dev, uint32_t sector_address)
{
    int area_id;
    uint32_t len;

    flash_native_ensure_file_open();

    area_id = find_area(sector_address);
    if (area_id == -1) {
        return -1;
    }
    len = flash_sector_len(area_id);
    flash_native_erase(sector_address, len);
    return 0;
}

static int
native_flash_sector_info(const struct hal_flash *dev, int idx,
        uint32_t *address, uint32_t *size)
{
    assert(idx < FLASH_NUM_AREAS);

    *address = native_flash_sectors[idx];
    *size = flash_sector_len(idx);
    return 0;
}

static int
native_flash_init(const struct hal_flash *dev)
{
    //if (native_flash_file) {
        flash_native_file_open(native_flash_file);
    //}
#if MYNEWT_VAL(MCU_FLASH_STYLE_NORDIC)
    int i;

    for (i = 0; i < FLASH_NUM_AREAS; i++) {
        native_flash_sectors[i] = i * 2048;
    }
#endif
    return 0;
}
