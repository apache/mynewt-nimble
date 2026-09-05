/*
   wqueue.h
   Worker thread queue based on the Standard C++ library list
   template class.
   ------------------------------------------
   Copyright (c) 2013 Vic Hargrave
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

// https://vichargrave.github.io/articles/2013-01/multithreaded-work-queue-in-cpp
// https://github.com/vichargrave/wqueue/blob/master/wqueue.h


#ifndef __wqueue_h__
#define __wqueue_h__

#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <list>

template <typename T> class wqueue
{
    std::list<T>         m_queue;
    pthread_mutex_t      m_mutex;
    pthread_mutexattr_t  m_mutex_attr;
    pthread_cond_t       m_condv;

public:
    wqueue()
    {
        pthread_condattr_t cond_attr;

        pthread_mutexattr_init(&m_mutex_attr);
        pthread_mutexattr_settype(&m_mutex_attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&m_mutex, &m_mutex_attr);
        pthread_condattr_init(&cond_attr);
        pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC);
        pthread_cond_init(&m_condv, &cond_attr);
        pthread_condattr_destroy(&cond_attr);
    }

    ~wqueue() {
        pthread_mutex_destroy(&m_mutex);
        pthread_cond_destroy(&m_condv);
    }

    void put(T item) {
        pthread_mutex_lock(&m_mutex);
        m_queue.push_back(item);
        pthread_cond_signal(&m_condv);
        pthread_mutex_unlock(&m_mutex);
    }

    T get(uint32_t tmo) {
        struct timespec deadline;
        int rc;

        pthread_mutex_lock(&m_mutex);
        if (tmo && tmo != UINT32_MAX && m_queue.empty()) {
            if (clock_gettime(CLOCK_MONOTONIC, &deadline) != 0) {
                pthread_mutex_unlock(&m_mutex);
                return NULL;
            }

            /* Linux NPL ticks are milliseconds. Keep one absolute deadline
             * so spurious wakeups cannot extend the requested timeout.
             */
            deadline.tv_sec += tmo / 1000;
            deadline.tv_nsec += (tmo % 1000) * 1000000;
            if (deadline.tv_nsec >= 1000000000) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000;
            }
        }

        while (tmo && m_queue.empty()) {
            if (tmo == UINT32_MAX) {
                rc = pthread_cond_wait(&m_condv, &m_mutex);
            } else {
                rc = pthread_cond_timedwait(&m_condv, &m_mutex, &deadline);
            }
            if (rc != 0) {
                break;
            }
        }

        T item = NULL;

        if (m_queue.size() != 0) {
            item = m_queue.front();
            m_queue.pop_front();
        }

        pthread_mutex_unlock(&m_mutex);
        return item;
    }

    void remove(T item) {
        pthread_mutex_lock(&m_mutex);
        m_queue.remove(item);
        pthread_mutex_unlock(&m_mutex);
    }

    int size() {
        pthread_mutex_lock(&m_mutex);
        int size = m_queue.size();
        pthread_mutex_unlock(&m_mutex);
        return size;
    }
};

#endif
