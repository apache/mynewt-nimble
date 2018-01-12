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

#include <stdint.h>
#include <pthread.h>

pthread_mutex_t s_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
pthread_cond_t s_cond = PTHREAD_COND_INITIALIZER;

void os_atomic_begin()
{
    pthread_mutex_lock(&s_mutex);
}

void os_atomic_end()
{
    pthread_mutex_unlock(&s_mutex);
    pthread_cond_signal(&s_cond);
}
