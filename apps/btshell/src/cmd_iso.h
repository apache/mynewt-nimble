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

#ifndef H_CMD_ISO_
#define H_CMD_ISO_

#include "cmd.h"

extern const struct shell_cmd_help cmd_iso_big_create_help;
extern const struct shell_cmd_help cmd_iso_big_terminate_help;
extern const struct shell_cmd_help cmd_iso_big_sync_create_help;
extern const struct shell_cmd_help cmd_iso_big_sync_terminate_help;
extern const struct shell_cmd_help cmd_iso_data_path_setup_help;
extern const struct shell_cmd_help cmd_iso_data_path_remove_help;

int cmd_iso_big_create(int argc, char **argv);
int cmd_iso_big_terminate(int argc, char **argv);
int cmd_iso_big_sync_create(int argc, char **argv);
int cmd_iso_big_sync_terminate(int argc, char **argv);
int cmd_iso_data_path_setup(int argc, char **argv);
int cmd_iso_data_path_remove(int argc, char **argv);

#endif /* H_CMD_ISO_ */
