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

#include <assert.h>
#include "os/os.h"
#include "mem/mem.h"
#include "sysinit/sysinit.h"

static STAILQ_HEAD(os_mbuf_list, os_mbuf_pool) g_msys_pool_list =
    STAILQ_HEAD_INITIALIZER(g_msys_pool_list);

#if MYNEWT_VAL(MSYS_1_BLOCK_COUNT) > 0
#define SYSINIT_MSYS_1_MEMBLOCK_SIZE                \
    OS_ALIGN(MYNEWT_VAL(MSYS_1_BLOCK_SIZE), 4)
#define SYSINIT_MSYS_1_MEMPOOL_SIZE                 \
    OS_MEMPOOL_SIZE(MYNEWT_VAL(MSYS_1_BLOCK_COUNT),  \
                    SYSINIT_MSYS_1_MEMBLOCK_SIZE)
static os_membuf_t os_msys_1_data[SYSINIT_MSYS_1_MEMPOOL_SIZE];
static struct os_mbuf_pool os_msys_1_mbuf_pool;
static struct os_mempool os_msys_1_mempool;
#endif

#if MYNEWT_VAL(MSYS_2_BLOCK_COUNT) > 0
#define SYSINIT_MSYS_2_MEMBLOCK_SIZE                \
    OS_ALIGN(MYNEWT_VAL(MSYS_2_BLOCK_SIZE), 4)
#define SYSINIT_MSYS_2_MEMPOOL_SIZE                 \
    OS_MEMPOOL_SIZE(MYNEWT_VAL(MSYS_2_BLOCK_COUNT),  \
                    SYSINIT_MSYS_2_MEMBLOCK_SIZE)
static os_membuf_t os_msys_2_data[SYSINIT_MSYS_2_MEMPOOL_SIZE];
static struct os_mbuf_pool os_msys_2_mbuf_pool;
static struct os_mempool os_msys_2_mempool;
#endif

#define OS_MSYS_SANITY_ENABLED                  \
    (MYNEWT_VAL(MSYS_1_SANITY_MIN_COUNT) > 0 || \
     MYNEWT_VAL(MSYS_2_SANITY_MIN_COUNT) > 0)

#if OS_MSYS_SANITY_ENABLED
static struct os_sanity_check os_msys_sc;
#endif

int
os_msys_register(struct os_mbuf_pool *new_pool)
{
    struct os_mbuf_pool *pool;
    struct os_mbuf_pool *prev;

    /* We want to have order from smallest to biggest mempool. */
    prev = NULL;
    pool = NULL;
    STAILQ_FOREACH(pool, &g_msys_pool_list, omp_next) {
        if (new_pool->omp_databuf_len < pool->omp_databuf_len) {
            break;
        }
        prev = pool;
    }

    if (prev) {
        STAILQ_INSERT_AFTER(&g_msys_pool_list, prev, new_pool, omp_next);
    } else {
        STAILQ_INSERT_HEAD(&g_msys_pool_list, new_pool, omp_next);
    }

    return (0);
}

void
os_msys_reset(void)
{
    STAILQ_INIT(&g_msys_pool_list);
}

static struct os_mbuf_pool *os_msys_find_pool(uint16_t dsize);

static struct os_mbuf_pool *
os_msys_find_biggest_pool(void)
{
    return os_msys_find_pool(0xFFFF);
}

static struct os_mbuf_pool *
os_msys_find_pool(uint16_t dsize)
{
    struct os_mbuf_pool *pool;
    struct os_mbuf_pool *pool_with_free_blocks = NULL;
    uint16_t pool_free_blocks;

    STAILQ_FOREACH(pool, &g_msys_pool_list, omp_next) {
        pool_free_blocks = pool->omp_pool->mp_num_free;
        if (pool_free_blocks != 0) {
            pool_with_free_blocks = pool;
            if (dsize <= pool->omp_databuf_len) {
                break;
            }
        }
    }

    return pool_with_free_blocks;
}


struct os_mbuf *
os_msys_get(uint16_t dsize, uint16_t leadingspace)
{
    struct os_mbuf *m;
    struct os_mbuf_pool *pool;

    /* If dsize = 0 that means user has no idea how big block size is needed,
    * therefore lets find for him the biggest one
    */
    if (dsize == 0) {
        pool = os_msys_find_biggest_pool();
    } else {
        pool = os_msys_find_pool(dsize);
    }

    if (!pool) {
        goto err;
    }

    m = os_mbuf_get(pool, leadingspace);
    return (m);
err:
    return (NULL);
}

struct os_mbuf *
os_msys_get_pkthdr(uint16_t dsize, uint16_t user_hdr_len)
{
    uint16_t total_pkthdr_len;
    struct os_mbuf *m;
    struct os_mbuf_pool *pool;

    total_pkthdr_len =  user_hdr_len + sizeof(struct os_mbuf_pkthdr);

    /* If dsize = 0 that means user has no idea how big block size is needed,
     * therefore lets find for him the biggest one
     */
    if (dsize == 0) {
        pool = os_msys_find_biggest_pool();
    } else {
        pool = os_msys_find_pool(dsize + total_pkthdr_len);
    }

    if (!pool) {
        goto err;
    }

    m = os_mbuf_get_pkthdr(pool, user_hdr_len);
    return (m);
err:
    return (NULL);
}

int
os_msys_count(void)
{
    struct os_mbuf_pool *omp;
    int total;

    total = 0;
    STAILQ_FOREACH(omp, &g_msys_pool_list, omp_next) {
        total += omp->omp_pool->mp_num_blocks;
    }

    return total;
}

int
os_msys_num_free(void)
{
    struct os_mbuf_pool *omp;
    int total;

    total = 0;
    STAILQ_FOREACH(omp, &g_msys_pool_list, omp_next) {
        total += omp->omp_pool->mp_num_free;
    }

    return total;
}

#if OS_MSYS_SANITY_ENABLED

/**
 * Retrieves the minimum safe buffer count for an msys pool.  That is, the
 * lowest a pool's buffer count can be without causing the sanity check to
 * fail.
 *
 * @param idx                   The index of the msys pool to query.
 *
 * @return                      The msys pool's minimum safe buffer count.
 */
static int
os_msys_sanity_min_count(int idx)
{
    switch (idx) {
    case 0:
        return MYNEWT_VAL(MSYS_1_SANITY_MIN_COUNT);

    case 1:
        return MYNEWT_VAL(MSYS_2_SANITY_MIN_COUNT);

    default:
        assert(0);
        return 0;
    }
}

static int
os_msys_sanity(struct os_sanity_check *sc, void *arg)
{
    const struct os_mbuf_pool *omp;
    int min_count;
    int idx;

    idx = 0;
    STAILQ_FOREACH(omp, &g_msys_pool_list, omp_next) {
        min_count = os_msys_sanity_min_count(idx);
        if (omp->omp_pool->mp_num_free < min_count) {
            return OS_ENOMEM;
        }

        idx++;
    }

    return 0;
}
#endif

static void
os_msys_init_once(void *data, struct os_mempool *mempool,
                  struct os_mbuf_pool *mbuf_pool,
                  int block_count, int block_size, char *name)
{
    int rc;

    rc = mem_init_mbuf_pool(data, mempool, mbuf_pool, block_count, block_size,
                            name);
    SYSINIT_PANIC_ASSERT(rc == 0);

    rc = os_msys_register(mbuf_pool);
    SYSINIT_PANIC_ASSERT(rc == 0);
}

void
os_msys_init(void)
{
    int rc;

    os_msys_reset();

    (void)os_msys_init_once;
    (void)rc;

#if MYNEWT_VAL(MSYS_1_BLOCK_COUNT) > 0
    os_msys_init_once(os_msys_1_data,
                      &os_msys_1_mempool,
                      &os_msys_1_mbuf_pool,
                      MYNEWT_VAL(MSYS_1_BLOCK_COUNT),
                      SYSINIT_MSYS_1_MEMBLOCK_SIZE,
                      "msys_1");
#endif

#if MYNEWT_VAL(MSYS_2_BLOCK_COUNT) > 0
    os_msys_init_once(os_msys_2_data,
                      &os_msys_2_mempool,
                      &os_msys_2_mbuf_pool,
                      MYNEWT_VAL(MSYS_2_BLOCK_COUNT),
                      SYSINIT_MSYS_2_MEMBLOCK_SIZE,
                      "msys_2");
#endif

#if OS_MSYS_SANITY_ENABLED
    os_msys_sc.sc_func = os_msys_sanity;
    os_msys_sc.sc_checkin_itvl =
        OS_TICKS_PER_SEC * MYNEWT_VAL(MSYS_SANITY_TIMEOUT) / 1000;
    rc = os_sanity_check_register(&os_msys_sc);
    SYSINIT_PANIC_ASSERT(rc == 0);
#endif
}

#if MYNEWT_VAL(SELFTEST)
struct os_mbuf_list *
get_msys_pool_list(void)
{
    return &g_msys_pool_list;
}
#endif
