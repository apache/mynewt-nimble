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

SECTIONS
{
    .link_tables :
    {
        INCLUDE "link_tables.ld.h"
    }
}
INSERT AFTER .rodata

/* Native simulator tasks and HW events, see native_simulator linker_script.pre.ld */
SECTIONS
{
    nsi_tasks :
    {
        __nsi_tasks_start = .;
        __nsi_PRE_BOOT_1_tasks_start = .;
        KEEP(*(SORT(.nsi_PRE_BOOT_1[0-9]_task)));
        KEEP(*(SORT(.nsi_PRE_BOOT_1[1-9][0-9]_task)));
        KEEP(*(SORT(.nsi_PRE_BOOT_1[1-9][0-9][0-9]_task)));
        __nsi_PRE_BOOT_2_tasks_start = .;
        KEEP(*(SORT(.nsi_PRE_BOOT_2[0-9]_task)));
        KEEP(*(SORT(.nsi_PRE_BOOT_2[1-9][0-9]_task)));
        KEEP(*(SORT(.nsi_PRE_BOOT_2[1-9][0-9][0-9]_task)));
        __nsi_HW_INIT_tasks_start = .;
        KEEP(*(SORT(.nsi_HW_INIT[0-9]_task)));
        KEEP(*(SORT(.nsi_HW_INIT[1-9][0-9]_task)));
        KEEP(*(SORT(.nsi_HW_INIT[1-9][0-9][0-9]_task)));
        __nsi_PRE_BOOT_3_tasks_start = .;
        KEEP(*(SORT(.nsi_PRE_BOOT_3[0-9]_task)));
        KEEP(*(SORT(.nsi_PRE_BOOT_3[1-9][0-9]_task)));
        KEEP(*(SORT(.nsi_PRE_BOOT_3[1-9][0-9][0-9]_task)));
        __nsi_FIRST_SLEEP_tasks_start = .;
        KEEP(*(SORT(.nsi_FIRST_SLEEP[0-9]_task)));
        KEEP(*(SORT(.nsi_FIRST_SLEEP[1-9][0-9]_task)));
        KEEP(*(SORT(.nsi_FIRST_SLEEP[1-9][0-9][0-9]_task)));
        __nsi_ON_EXIT_PRE_tasks_start = .;
        KEEP(*(SORT(.nsi_ON_EXIT_PRE[0-9]_task)));
        KEEP(*(SORT(.nsi_ON_EXIT_PRE[1-9][0-9]_task)));
        KEEP(*(SORT(.nsi_ON_EXIT_PRE[1-9][0-9][0-9]_task)));
        __nsi_ON_EXIT_POST_tasks_start = .;
        KEEP(*(SORT(.nsi_ON_EXIT_POST[0-9]_task)));
        KEEP(*(SORT(.nsi_ON_EXIT_POST[1-9][0-9]_task)));
        KEEP(*(SORT(.nsi_ON_EXIT_POST[1-9][0-9][0-9]_task)));
        __nsi_tasks_end = .;
    }

    nsi_hw_events :
    {
        __nsi_hw_events_start = .;
        KEEP(*(SORT(.nsi_hw_event_[0-9])));
        KEEP(*(SORT(.nsi_hw_event_[1-9][0-9])));
        KEEP(*(SORT(.nsi_hw_event_[1-9][0-9][0-9])));
        __nsi_hw_events_end = .;
    }
}
INSERT AFTER .data
