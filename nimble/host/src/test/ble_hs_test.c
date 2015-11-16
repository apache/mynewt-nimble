/**
 * Copyright (c) 2015 Runtime Inc.
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

#include "host/ble_hs_test.h"
#include "testutil/testutil.h"

#ifdef PKG_TEST

int
main(void)
{
    tu_config.tc_print_results = 1;
    tu_init();

    l2cap_test_all();
    ble_hs_att_test_all();
    ble_host_hci_test_all();
    ble_hs_conn_test_all();
    ble_gap_test_all();
    ble_hs_uuid_test_all();

    return tu_any_failed;
}

#endif
