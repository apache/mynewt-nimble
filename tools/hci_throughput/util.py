#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#  http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

import logging
import shutil
import time
import os
from pathlib import Path


def create_test_directory():
    test_dir_name = "tests/" + time.strftime("%Y_%m_%d_%H_%M_%S")
    path = os.path.join(os.getcwd(), test_dir_name)
    os.mkdir(path, mode=0o777)
    print("Test directory: ", path)
    return path


def configure_logging(log_filename, clear_log_file=True):
    format_template = ("%(asctime)s %(threadName)s %(name)s %(levelname)s "
                       "%(filename)-25s %(lineno)-5s "
                       "%(funcName)-25s : %(message)s")
    logging.basicConfig(format=format_template,
                        filename=log_filename,
                        filemode='a',
                        level=logging.DEBUG)
    if clear_log_file:
        with open(log_filename, "w") as f:
            f.write("asctime\t\t\t\t\tthreadName name levelname filename\
            \tlineno\tfuncName\t\t\t\tmessage\n")

    logging.getLogger("asyncio").setLevel(logging.WARNING)
    logging.getLogger("matplotlib").setLevel(logging.WARNING)


def copy_config_files_to_test_directory(files: list, test_directory: str):
    for file in files:
        shutil.copy(file, test_directory + "/" + Path(file).name)


def copy_log_files_to_test_directory(dir: str):
    log_files = ["log/log_rx.log", "log/log_tx.log", "log/check_addr.log"]
    for file in log_files:
        shutil.copy(file, dir + "/" + time.strftime("%Y_%m_%d_%H_%M_%S_") +
                                file.replace("log/", ""))


# Running tests as sudo implies root permissions on created directories/files.
# This function sets the default permission mode to dirs/files in given path
# recursively.
def set_default_chmod_recurs(path):
    for root, dirs, files in os.walk(path):
        for d in dirs:
            os.chmod(os.path.join(root, d), 0o0777)
        for f in files:
            os.chmod(os.path.join(root, f), 0o0777)
