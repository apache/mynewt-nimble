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

#ifndef H_LIBSAMPLERATE_CONFIG_
#define H_LIBSAMPLERATE_CONFIG_

#include "syscfg/syscfg.h"

#if MYNEWT_VAL(LIBSAMPLERATE_ENABLE_SINC_BEST_CONVERTER)
#define ENABLE_SINC_BEST_CONVERTER      1
#endif

#if MYNEWT_VAL(LIBSAMPLERATE_ENABLE_SINC_MEDIUM_CONVERTER)
#define ENABLE_SINC_MEDIUM_CONVERTER      1
#endif

#if MYNEWT_VAL(LIBSAMPLERATE_ENABLE_SINC_FAST_CONVERTER)
#define ENABLE_SINC_FAST_CONVERTER      1
#endif

#if MYNEWT_VAL(LIBSAMPLERATE_LIBSAMPLER_NDEBUG)
#define LIBSAMPLER_NDEBUG      1
#endif

#define PACKAGE     "libsamplerate"
#define VERSION     "0.2.2"

#endif /* H_LIBSAMPLERATE_CONFIG_ */
