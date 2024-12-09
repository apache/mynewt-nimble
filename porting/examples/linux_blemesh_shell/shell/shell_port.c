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
#include <stdio.h>
#include <string.h>
#include "nimble/nimble_npl.h"
#include "shell/shell.h"
#include "console/console.h"
#include "console/console_port.h"

static const struct shell_cmd *def_commands;

int
shell_register(const char *module_name, const struct shell_cmd *commands)
{
    def_commands = commands;
    return 0;
}

void
shell_register_app_cmd_handler(shell_cmd_func_t handler)
{
}

void
shell_register_prompt_handler(shell_prompt_function_t handler)
{
}

void
shell_register_default_module(const char *name)
{
}

void
shell_evq_set(struct os_eventq *evq)
{
    console_set_queues(evq, NULL);
    console_set_event_cb(handle_shell_evt);
}

void
print_prompt(struct streamer *streamer)
{
    console_printf(">mesh\n");
}

static size_t
line2argv(char *str, char *argv[], size_t size, struct streamer *streamer)
{
    size_t argc = 0;

    if (!strlen(str)) {
        return 0;
    }

    while (*str && *str == ' ') {
        str++;
    }

    if (!*str) {
        return 0;
    }

    argv[argc++] = str;

    while ((str = strchr(str, ' '))) {
        *str++ = '\0';

        while (*str && *str == ' ') {
            str++;
        }

        if (!*str) {
            break;
        }

        argv[argc++] = str;

        if (argc == size) {
            fprintf(stderr, "Too many parameters (max %zu)\n", size - 1);
            return 0;
        }
    }

    /* keep it POSIX style where argv[argc] is required to be NULL */
    argv[argc] = NULL;

    return argc;
}

int
show_cmd_help(char *cmd, struct streamer *streamer)
{
    const struct shell_cmd *command;

    for (int i = 0; def_commands[i].sc_cmd; i++) {
        if (cmd) {
            command = &def_commands[i];
        } else if (!strcmp(cmd, def_commands[i].sc_cmd)) {
            command = &def_commands[i];
        }

        if (command->help) {
            console_printf("%s: %s\n", command->sc_cmd, command->help->usage ?: "");
        } else {
            console_printf("%s\n", command->sc_cmd);
        }
    }

    return 0;
}

int
shell_exec(int argc, char **argv, struct streamer *streamer)
{
    const struct shell_cmd *cmd;
    const char *command = argv[0];
    size_t argc_offset = 0;
    int rc;

    if (!strcmp(command, "help")) {
        return show_cmd_help(NULL, NULL);
    }

    for (int i = 0; def_commands[i].sc_cmd; i++) {
        if (!strcmp(command, def_commands[i].sc_cmd)) {
            cmd = &def_commands[i];
        }
    }

    if (!cmd) {
        console_printf("Unrecognized command: %s\n", argv[0]);
        console_printf("Type 'help' for list of available commands\n");
        print_prompt(streamer);
        return BLE_NPL_ENOENT;
    }

    /* Execute callback with arguments */
    if (!cmd->sc_ext) {
        rc = cmd->sc_cmd_func(argc - argc_offset, &argv[argc_offset]);
    } else {
        rc = cmd->sc_cmd_ext_func(cmd, argc - argc_offset, &argv[argc_offset], streamer);
    }
    if (rc < 0) {
        show_cmd_help(argv[0], streamer);
    }

    print_prompt(streamer);

    return rc;
}

static void
shell_process_command(char *line, struct streamer *streamer)
{
    char *argv[MYNEWT_VAL(SHELL_CMD_ARGC_MAX) + 1];
    size_t argc;

    argc = line2argv(line, argv, MYNEWT_VAL(SHELL_CMD_ARGC_MAX) + 1, streamer);
    if (!argc) {
        print_prompt(streamer);
        return;
    }

    shell_exec(argc, argv, streamer);
}

void
handle_shell_evt(struct os_event *ev)
{
    char *line;

    if (!ev) {
        print_prompt(NULL);
        return;
    }

    line = ev->ev_arg;
    if (!line) {
        print_prompt(NULL);
        return;
    }

    shell_process_command(line, NULL);

    ev->ev_queued = 0;
}

#if MYNEWT_VAL(SHELL_MGMT)
int
shell_nlip_input_register(shell_nlip_input_func_t nf, void *arg)
{
    return 0;
}
int
shell_nlip_output(struct os_mbuf *m)
{
    return 0;
}
#endif

#if MYNEWT_VAL(SHELL_COMPAT)
int
shell_cmd_register(const struct shell_cmd *sc)
{
    return 0;
}
#endif
