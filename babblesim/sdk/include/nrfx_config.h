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

#ifndef NRFX_CONFIG_H__
#define NRFX_CONFIG_H__

#undef NRF_DONT_DECLARE_ONLY
#define NRF_DECLARE_ONLY

/* Redefine this to make sure no function is inlined and also no function has
 * weak attribute (e.g. nrf_hal_originals.c)
 */
#undef NRF_STATIC_INLINE
#define NRF_STATIC_INLINE

#include "nrfx_config_bsim.h"

#endif

