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

#include "test_util.h"
#include "os/os.h"

#define    TEST_MEMPOOL_BLOCKS       4
#define    TEST_MEMPOOL_BLOCK_SIZE   128

static struct os_mempool s_mempool;

static os_membuf_t s_mempool_mem[OS_MEMPOOL_SIZE(TEST_MEMPOOL_BLOCKS,
						 TEST_MEMPOOL_BLOCK_SIZE)];

static void *s_memblock[TEST_MEMPOOL_BLOCKS];

/**
 * Unit test for initializing a mempool.
 *
 * os_error_t os_mempool_init(struct os_mempool *mp, int blocks,
 *                            int block_size, void *membuf, char *name);
 *
 */
int test_init()
{
    int err;
    err = os_mempool_init(NULL,
			  TEST_MEMPOOL_BLOCKS,
			  TEST_MEMPOOL_BLOCK_SIZE,
			  NULL,
			  "Null mempool");
    VerifyOrQuit(err, "os_mempool_init accepted NULL parameters.");

    err = os_mempool_init(&s_mempool,
			  TEST_MEMPOOL_BLOCKS,
			  TEST_MEMPOOL_BLOCK_SIZE,
			  s_mempool_mem,
			  "s_mempool");
    return err;
}

/**
 * Test integrity check of a mempool.
 *
 * bool os_mempool_is_sane(const struct os_mempool *mp);
 */
int test_is_sane()
{
    return (os_mempool_is_sane(&s_mempool)) ? PASS : FAIL;
}

/**
 * Test getting a memory block from the pool, putting it back,
 * and checking if it is still valid.
 *
 * void *os_memblock_get(struct os_mempool *mp);
 *
 * os_error_t os_memblock_put(struct os_mempool *mp, void *block_addr);
 *
 * int os_memblock_from(const struct os_mempool *mp, const void *block_addr);
 */
int test_stress()
{
    int loops = 3;
    while(loops--)
    {
        for (int i = 0; i < 4; i++)
	{
	    s_memblock[i] = os_memblock_get(&s_mempool);
	    VerifyOrQuit(os_memblock_from(&s_mempool, s_memblock[i]),
			 "os_memblock_get return invalid block.");
	}


        for (int i = 0; i < 4; i++)
	{
 	    SuccessOrQuit(os_memblock_put(&s_mempool, s_memblock[i]),
			"os_memblock_put refused to take valid block.");
	    //VerifyOrQuit(!os_memblock_from(&s_mempool, s_memblock[i]),
	    //		 "Block still valid after os_memblock_put.");
	}

    }
    return PASS;
}

int main(void)
{
    SuccessOrQuit(test_init(),    "Failed: os_mempool_init");
    SuccessOrQuit(test_is_sane(), "Failed: os_mempool_is_sane");
    SuccessOrQuit(test_stress(),  "Failed: os_mempool stree test");
    SuccessOrQuit(test_is_sane(), "Failed: os_mempool_is_sane");
    printf("All tests passed\n");
    return PASS;
}
