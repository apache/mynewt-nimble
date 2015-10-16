/**
 * Copyright (c) 2015 Stack Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef H_LL_SCAN_
#define H_LL_SCAN_

#include "nimble/ble.h"

/* Maximum amount of scan response data in a scan response */
#define BLE_SCAN_RSP_DATA_MAX_LEN       (31)

/*
 * SCAN_REQ 
 *      -> ScanA    (6 bytes)
 *      -> AdvA     (6 bytes)
 * 
 *  ScanA is the scanners public (TxAdd=0) or random (TxAdd = 1) address
 *  AdvaA is the advertisers public (RxAdd=0) or random (RxAdd=1) address.
 * 
 * Sent by the LL in the Scanning state; received by the LL in the advertising
 * state. The advertising address is the intended recipient of this frame.
 */
#define BLE_SCAN_REQ_LEN                (12)
#define BLE_SCAN_REQ_TXTIME_USECS       (176)

/*
 * SCAN_RSP
 *      -> AdvA         (6 bytes)
 *      -> ScanRspData  (0 - 31 bytes) 
 * 
 *  AdvaA is the advertisers public (TxAdd=0) or random (TxAdd=1) address.
 *  ScanRspData may contain any data from the advertisers host.
 * 
 * Sent by the LL in the advertising state; received by the LL in the
 * scanning state. 
 */
#define BLE_SCAN_RSP_MIN_LEN            (6)
#define BLE_SCAN_RSP_MAX_LEN            (6 + BLE_SCAN_RSP_DATA_MAX_LEN)

#endif /* H_LL_SCAN_ */
