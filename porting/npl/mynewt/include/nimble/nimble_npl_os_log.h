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

#ifndef _NIMBLE_NPL_OS_LOG_H_
#define _NIMBLE_NPL_OS_LOG_H_

#include <modlog/modlog.h>
#include <log/log.h>

/* Only include the logcfg header if this version of newt can generate it. */
#if MYNEWT_VAL(NEWT_FEATURE_LOGCFG)
#include <logcfg/logcfg.h>
#endif

#endif  /* _NIMBLE_NPL_OS_LOG_H_ */
