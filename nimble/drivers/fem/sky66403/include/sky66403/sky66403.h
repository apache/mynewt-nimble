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


#ifndef _SKY66403_H
#define _SKY66403_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void sky66403_tx_linear_mode(uint8_t enabled);
void sky66403_rx_bypass(uint8_t enabled);
void sky66403_tx_bypass(uint8_t enabled);

#if MYNEWT_VAL_CHOICE(SKY66403_ANTENNA_PORT, runtime)
uint8_t sky66403_default_antenna_get(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _SKY66403_H */
