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

#include "testutil/testutil.h"

#include "host/ble_hs.h"
#include "audio/ble_audio.h"

static struct ble_audio_event_listener event_listener;

static int
event_handler(struct ble_audio_event *event, void *arg)
{
    return 0;
}

TEST_CASE_SELF(ble_audio_listener_register_test)
{
    int rc;

    rc = ble_audio_event_listener_register(&event_listener, event_handler,
                                           NULL);
    TEST_ASSERT(rc == 0);

    rc = ble_audio_event_listener_register(&event_listener, event_handler,
    NULL);
    TEST_ASSERT(rc != 0);

    rc = ble_audio_event_listener_unregister(&event_listener);
    TEST_ASSERT(rc == 0);

    rc = ble_audio_event_listener_register(NULL, event_handler, NULL);
    TEST_ASSERT(rc != 0);

    rc = ble_audio_event_listener_register(&event_listener, NULL, NULL);
    TEST_ASSERT(rc != 0);

    rc = ble_audio_event_listener_unregister(NULL);
    TEST_ASSERT(rc != 0);

    rc = ble_audio_event_listener_unregister(&event_listener);
    TEST_ASSERT(rc != 0);
}
