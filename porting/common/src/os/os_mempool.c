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

#include "os/os.h"

#include <string.h>
#include <assert.h>
#include <stdbool.h>

/**
 * @addtogroup OSKernel
 * @{
 *   @defgroup OSMempool Memory Pools
 *   @{
 */

#define OS_MEM_TRUE_BLOCK_SIZE(bsize)   OS_ALIGN(bsize, OS_ALIGNMENT)
#define OS_MEMPOOL_TRUE_BLOCK_SIZE(mp) OS_MEM_TRUE_BLOCK_SIZE(mp->mp_block_size)

STAILQ_HEAD(, os_mempool) g_os_mempool_list =
    STAILQ_HEAD_INITIALIZER(g_os_mempool_list);

#if MYNEWT_VAL(OS_MEMPOOL_POISON)
static uint32_t os_mem_poison = 0xde7ec7ed;

static void
os_mempool_poison(void *start, int sz)
{
    int i;
    char *p = start;

    for (i = sizeof(struct os_memblock); i < sz;
         i = i + sizeof(os_mem_poison)) {
        memcpy(p + i, &os_mem_poison, min(sizeof(os_mem_poison), sz - i));
    }
}

static void
os_mempool_poison_check(void *start, int sz)
{
    int i;
    char *p = start;

    for (i = sizeof(struct os_memblock); i < sz;
         i = i + sizeof(os_mem_poison)) {
        assert(!memcmp(p + i, &os_mem_poison,
               min(sizeof(os_mem_poison), sz - i)));
    }
}
#else
#define os_mempool_poison(start, sz)
#define os_mempool_poison_check(start, sz)
#endif

/**
 * os mempool init
 *
 * Initialize a memory pool.
 *
 * @param mp            Pointer to a pointer to a mempool
 * @param blocks        The number of blocks in the pool
 * @param blocks_size   The size of the block, in bytes.
 * @param membuf        Pointer to memory to contain blocks.
 * @param name          Name of the pool.
 *
 * @return os_error_t
 */
os_error_t
os_mempool_init(struct os_mempool *mp, int blocks, int block_size,
                void *membuf, char *name)
{
    int true_block_size;
    uint8_t *block_addr;
    struct os_memblock *block_ptr;

    /* Check for valid parameters */
    if (!mp || (blocks < 0) || (block_size <= sizeof(struct os_memblock))) {
        return OS_INVALID_PARM;
    }

    if ((!membuf) && (blocks != 0)) {
        return OS_INVALID_PARM;
    }

    if (membuf != NULL) {
        /* Blocks need to be sized properly and memory buffer should be
         * aligned
         */
        if (((uint32_t)membuf & (OS_ALIGNMENT - 1)) != 0) {
            return OS_MEM_NOT_ALIGNED;
        }
    }
    true_block_size = OS_MEM_TRUE_BLOCK_SIZE(block_size);

    /* Initialize the memory pool structure */
    mp->mp_block_size = block_size;
    mp->mp_num_free = blocks;
    mp->mp_min_free = blocks;
    mp->mp_num_blocks = blocks;
    mp->mp_membuf_addr = (uint32_t)membuf;
    mp->name = name;
    os_mempool_poison(membuf, true_block_size);
    SLIST_FIRST(mp) = membuf;

    /* Chain the memory blocks to the free list */
    block_addr = (uint8_t *)membuf;
    block_ptr = (struct os_memblock *)block_addr;
    while (blocks > 1) {
        block_addr += true_block_size;
        os_mempool_poison(block_addr, true_block_size);
        SLIST_NEXT(block_ptr, mb_next) = (struct os_memblock *)block_addr;
        block_ptr = (struct os_memblock *)block_addr;
        --blocks;
    }

    /* Last one in the list should be NULL */
    SLIST_NEXT(block_ptr, mb_next) = NULL;

    STAILQ_INSERT_TAIL(&g_os_mempool_list, mp, mp_list);

    return OS_OK;
}

/**
 * Performs an integrity check of the specified mempool.  This function
 * attempts to detect memory corruption in the specified memory pool.
 *
 * @param mp                    The mempool to check.
 *
 * @return                      true if the memory pool passes the integrity
 *                                  check;
 *                              false if the memory pool is corrupt.
 */
bool
os_mempool_is_sane(const struct os_mempool *mp)
{
    struct os_memblock *block;

    /* Verify that each block in the free list belongs to the mempool. */
    SLIST_FOREACH(block, mp, mb_next) {
        if (!os_memblock_from(mp, block)) {
            return false;
        }
        os_mempool_poison_check(block, OS_MEMPOOL_TRUE_BLOCK_SIZE(mp));
    }

    return true;
}

/**
 * Checks if a memory block was allocated from the specified mempool.
 *
 * @param mp                    The mempool to check as parent.
 * @param block_addr            The memory block to check as child.
 *
 * @return                      0 if the block does not belong to the mempool;
 *                              1 if the block does belong to the mempool.
 */
int
os_memblock_from(const struct os_mempool *mp, const void *block_addr)
{
    uint32_t true_block_size;
    uint32_t baddr32;
    uint32_t end;

    _Static_assert(sizeof block_addr == sizeof baddr32,
                   "Pointer to void must be 32-bits.");

    baddr32 = (uint32_t)block_addr;
    true_block_size = OS_MEMPOOL_TRUE_BLOCK_SIZE(mp);
    end = mp->mp_membuf_addr + (mp->mp_num_blocks * true_block_size);

    /* Check that the block is in the memory buffer range. */
    if ((baddr32 < mp->mp_membuf_addr) || (baddr32 >= end)) {
        return 0;
    }

    /* All freed blocks should be on true block size boundaries! */
    if (((baddr32 - mp->mp_membuf_addr) % true_block_size) != 0) {
        return 0;
    }

    return 1;
}

/**
 * os memblock get
 *
 * Get a memory block from a memory pool
 *
 * @param mp Pointer to the memory pool
 *
 * @return void* Pointer to block if available; NULL otherwise
 */
void *
os_memblock_get(struct os_mempool *mp)
{
    os_sr_t sr;
    struct os_memblock *block;

    /* Check to make sure they passed in a memory pool (or something) */
    block = NULL;
    if (mp) {
        OS_ENTER_CRITICAL(sr);
        /* Check for any free */
        if (mp->mp_num_free) {
            /* Get a free block */
            block = SLIST_FIRST(mp);

            os_mempool_poison_check(block, OS_MEMPOOL_TRUE_BLOCK_SIZE(mp));

            /* Set new free list head */
            SLIST_FIRST(mp) = SLIST_NEXT(block, mb_next);

            /* Decrement number free by 1 */
            mp->mp_num_free--;
            if (mp->mp_min_free > mp->mp_num_free) {
                mp->mp_min_free = mp->mp_num_free;
            }
        }
        OS_EXIT_CRITICAL(sr);
    }

    return (void *)block;
}

/**
 * os memblock put
 *
 * Puts the memory block back into the pool
 *
 * @param mp Pointer to memory pool
 * @param block_addr Pointer to memory block
 *
 * @return os_error_t
 */
os_error_t
os_memblock_put(struct os_mempool *mp, void *block_addr)
{
    os_sr_t sr;
    struct os_memblock *block;

    /* Make sure parameters are valid */
    if ((mp == NULL) || (block_addr == NULL)) {
        return OS_INVALID_PARM;
    }

#if MYNEWT_VAL(OS_MEMPOOL_CHECK)
    /* Check that the block we are freeing is a valid block! */
    assert(os_memblock_from(mp, block_addr));

    /*
     * Check for duplicate free.
     */
    SLIST_FOREACH(block, mp, mb_next) {
        assert(block != (struct os_memblock *)block_addr);
    }
#endif
    os_mempool_poison(block_addr, OS_MEMPOOL_TRUE_BLOCK_SIZE(mp));
    block = (struct os_memblock *)block_addr;
    OS_ENTER_CRITICAL(sr);

    /* Chain current free list pointer to this block; make this block head */
    SLIST_NEXT(block, mb_next) = SLIST_FIRST(mp);
    SLIST_FIRST(mp) = block;

    /* XXX: Should we check that the number free <= number blocks? */
    /* Increment number free */
    mp->mp_num_free++;

    OS_EXIT_CRITICAL(sr);

    return OS_OK;
}


struct os_mempool *
os_mempool_info_get_next(struct os_mempool *mp, struct os_mempool_info *omi)
{
    struct os_mempool *cur;

    if (mp == NULL) {
        cur = STAILQ_FIRST(&g_os_mempool_list);
    } else {
        cur = STAILQ_NEXT(mp, mp_list);
    }

    if (cur == NULL) {
        return (NULL);
    }

    omi->omi_block_size = cur->mp_block_size;
    omi->omi_num_blocks = cur->mp_num_blocks;
    omi->omi_num_free = cur->mp_num_free;
    omi->omi_min_free = cur->mp_min_free;
    strncpy(omi->omi_name, cur->name, sizeof(omi->omi_name));

    return (cur);
}


/**
 *   @} OSMempool
 * @} OSKernel
 */
