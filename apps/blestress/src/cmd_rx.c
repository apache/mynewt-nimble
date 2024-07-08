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
#include <inttypes.h>
#include <string.h>
#include "os/mynewt.h"

#include "console/console.h"
#include "shell/shell.h"

#include "cmd.h"

int test_case_num;
struct os_sem rx_stress_test_sem;

static int
start_test_case(int argc, char **argv)
{
    uint8_t test_num;
    uint8_t min = 1;
    uint8_t max = 15;
    uint8_t dflt = 0;
    int rc;

    rc = parse_arg_init(argc - 1, argv +1);
    if (rc != 0) {
        return rc;
    }

    test_num = parse_arg_uint8_bounds_dflt("num", min, max, dflt, &rc);
    if (rc != 0) {
        console_printf("Invalid test number parameter\n");
        return rc;
    }

    test_case_num = test_num;

    os_sem_release(&rx_stress_test_sem);

    return 0;
}

#if MYNEWT_VAL(SHELL_CMD_HELP)
static const struct shell_param stress_test_params[] = {
    {"num", "Test case number, usage: =[1-15], default: 0"},
    {NULL, NULL}
};

static const struct shell_cmd_help test_help = {
    .summary = "\nType start_test num (1-15) as a parameter to "
               "start a specific test case.\nNULL parameter or num=0 will run"
               " all test cases",
    .usage = NULL,
    .params = stress_test_params,
};
#endif

static const struct shell_cmd rx_cmd[] = {
    {
        .sc_cmd = "start_test",
        .sc_cmd_func = start_test_case,
#if MYNEWT_VAL(SHELL_CMD_HELP)
        .help = &test_help,
#endif
    },
};

void
rx_cmd_init(void)
{
    shell_register("rx_cmd", rx_cmd);
}
