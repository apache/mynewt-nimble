#!/bin/bash
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

if [ -z ${BSIM_COMPONENTS_PATH+x} ]; then
  echo "This board requires the BabbleSim simulator. Please set" \
        "the environment variable BSIM_COMPONENTS_PATH to point to its components" \
        "folder. More information can be found in" \
        "https://babblesim.github.io/folder_structure_and_env.html"
  exit 1
fi

if [ -z ${BSIM_OUT_PATH+x} ]; then
  echo "This board requires the BabbleSim simulator. Please set" \
        "the environment variable BSIM_OUT_PATH to point to the folder where the" \
        "simulator is compiled to. More information can be found in" \
        "https://babblesim.github.io/folder_structure_and_env.html"
  exit 1
fi

ln -sfn "${BSIM_COMPONENTS_PATH}" ./components

mkdir -p ./src/
cp "${BSIM_OUT_PATH}"/lib/*.32.a ./src/

# XXX: Workaround for bad linking by newt. Sometimes newt will link
# nrf weak functions from nrf_hal_originals.o instead of their BabbleSim
# replacements inside libNRF52_hw_models.32.a. But as long as the other
# weak functions, that do not have their replacements, are not used,
# we can just remove the file from the .a library here.
ar d ./src/libNRF52_hw_models.32.a nrf_hal_originals.o
