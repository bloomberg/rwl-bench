#include <algorithm>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <pthread.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <semaphore.h>

         // ===========================
         // Anti-Optimization Functions
         // ===========================

inline
void escape(void* p)
{
    asm volatile("" : : "g"(p) : "memory");
}

inline
void clobber()
{
    asm volatile("" : : : "memory");
}

         // ================
         // Helper Functions
         // ================

int64_t nanoseconds(timespec t, timespec e)
{
    return static_cast<int64_t>(t.tv_sec - e.tv_sec) * 1000000000LL
        + (t.tv_nsec - e.tv_nsec);
}

#ifndef __APPLE__

         // ================
         // PthreadSemaphore
         // ================

class PthreadSemaphore {
    sem_t d_semaphore;

  public:
    PthreadSemaphore(int count = 0) {  ::sem_init(&d_semaphore, 0, count);  }
    ~PthreadSemaphore() {  ::sem_destroy(&d_semaphore);  }

    void post() {  ::sem_post(&d_semaphore);  }
    void post(int count) {  for (int i = 0; i < count; ++i) post();  }
    void wait() {  ::sem_wait(&d_semaphore);  }
};

#else

         // ================
         // PthreadSemaphore
         // ================

class PthreadSemaphore {
    sem_t *d_semaphore;

  public:
    PthreadSemaphore(int count = 0) {
        d_semaphore = ::sem_open("rwl-bench",
                                 O_CREAT | O_EXCL,
                                 S_IRUSR | S_IWUSR,
                                 count);
    }
    ~PthreadSemaphore() {  ::sem_close(d_semaphore);  }

    void post() {  ::sem_post(d_semaphore);  }
    void post(int count) {  for (int i = 0; i < count; ++i) post();  }
    void wait() {  ::sem_wait(d_semaphore);  }
    int trywait() {  return ::sem_trywait(d_semaphore);  }
};

         // ========
         // MacMutex
         // ========

class MacMutex {
    PthreadSemaphore d_semaphore;

  public:
    MacMutex() {  d_semaphore.post();  }
    ~MacMutex() {}

    void lock() {  d_semaphore.wait();  }
    int try_lock() {  return d_semaphore.trywait();  }
    void unlock() {  d_semaphore.post();  }
};

#endif
         // ====
         // Null
         // ====

class Null {
  public:
    Null() {}
    ~Null() {}

    void lock() {}
    void unlock() {}

    void lock_shared() {}
    void unlock_shared() {}
};

         // ======
         // Atomic
         // ======

class Atomic {
    std::atomic<int> d_lock;

  public:
    Atomic() {}
    ~Atomic() {}

    void lock() {  d_lock.fetch_add(1, std::memory_order_acq_rel);  }
    void unlock() {  d_lock.fetch_add(1, std::memory_order_acq_rel);  }

    void lock_shared() {  d_lock.load(std::memory_order_acquire);  }
    void unlock_shared() {  d_lock.load(std::memory_order_acquire);  }
};

         // ============
         // WrappedMutex
         // ============

class WrappedMutex {
    std::mutex d_mutex;

  public:
    WrappedMutex() : d_mutex() {}
    ~WrappedMutex() {}

    void lock() {  d_mutex.lock();  }
    void unlock() {  d_mutex.unlock();  }

    void lock_shared() {  d_mutex.lock();  }
    void unlock_shared() {  d_mutex.unlock();  }
};

         // ================
         // WrappedSemaphore
         // ================

class WrappedSemaphore {
    PthreadSemaphore d_semaphore;

  public:
    WrappedSemaphore() {  d_semaphore.post();  }
    ~WrappedSemaphore() {}

    void lock() {  d_semaphore.wait();  }
    void unlock() {  d_semaphore.post();  }

    void lock_shared() {  d_semaphore.wait();  }
    void unlock_shared() {  d_semaphore.post();  }
};

         // =======
         // Pthread
         // =======

class Pthread {
    pthread_rwlock_t d_lock;

  public:
    Pthread() {  pthread_rwlock_init(&d_lock, NULL);  }
    ~Pthread() {  pthread_rwlock_destroy(&d_lock);  }

    void lock() {  pthread_rwlock_wrlock(&d_lock);  }
    void unlock() {  pthread_rwlock_unlock(&d_lock);  }

    void lock_shared() {  pthread_rwlock_rdlock(&d_lock);  }
    void unlock_shared() {  pthread_rwlock_unlock(&d_lock);  }
};

         // ======
         // Losing
         // ======

class Losing {
    static const long long s_READER_MASK         = 0x00000000000FFFFFLL;
    static const long long s_READER_INC          = 0x0000000000000001LL;
    static const long long s_PENDING_READER_MASK = 0x000000FFFFF00000LL;
    static const long long s_PENDING_READER_INC  = 0x0000000000100000LL;
    static const long long s_WRITER_MASK         = 0x0FFFFF0000000000LL;
    static const long long s_WRITER_INC          = 0x0000010000000000LL;

    // INSTANCE DATA
    std::atomic<int64_t> d_rwCount;
    PthreadSemaphore     d_wsema;
    PthreadSemaphore     d_rsema;

  public:
    Losing() : d_rwCount(0) {}
    ~Losing() {}

    void lock()
    {
        if (d_rwCount.fetch_add(s_WRITER_INC, std::memory_order_acq_rel)) {
            d_wsema.wait();
        }
    }

    void unlock()
    {
        int64_t count = d_rwCount.load(std::memory_order_acquire);
        int64_t post;

        do {
            post = (count & s_PENDING_READER_MASK) >> 20;
        } while (false == d_rwCount.compare_exchange_strong(
                                 count,
                                 (count & s_WRITER_MASK) + post - s_WRITER_INC,
                                 std::memory_order_acq_rel));

        if (post) {
            d_rsema.post(static_cast<int>(post));
        }
        else if (s_WRITER_INC != count) {
            d_wsema.post();
        }
    }

    void lock_shared()
    {
        int64_t count = d_rwCount.load(std::memory_order_acquire);
        int64_t writer;

        do {
            writer = count & s_WRITER_MASK;
        } while (false == d_rwCount.compare_exchange_strong(
                                              count,
                                              count + (  0 == writer
                                                       ? s_READER_INC
                                                       : s_PENDING_READER_INC),
                                              std::memory_order_acq_rel));

        if (writer) {
            d_rsema.wait();
        }
    }

    void unlock_shared()
    {
        int64_t count = d_rwCount.fetch_add(-s_READER_INC,
                                            std::memory_order_acq_rel)
                      - s_READER_INC;

        if (count && 0 == (count & s_READER_MASK)) {
            d_wsema.post();
        }
    }
};

         // ================
         // ReaderWriterLock
         // ================


class ReaderWriterLock {
    // This class provides a multi-reader/single-writer lock mechanism.

    // CLASS DATA
    static const std::int64_t k_READER_MASK         = 0x00000000ffffffffLL;
    static const std::int64_t k_READER_INC          = 0x0000000000000001LL;
    static const std::int64_t k_PENDING_WRITER_MASK = 0x0fffffff00000000LL;
    static const std::int64_t k_PENDING_WRITER_INC  = 0x0000000100000000LL;
    static const std::int64_t k_WRITER              = 0x1000000000000000LL;

    // DATA
    std::atomic<std::int64_t> d_state;      // atomic value
                                            // used to track
                                            // the state of
                                            // this mutex
#ifndef __APPLE__
    std::mutex                d_mutex;      // primary access
                                            // control
#else
    MacMutex                  d_mutex;      // primary access
                                            // control
#endif

    PthreadSemaphore          d_semaphore;  // used to capture
                                            // writers
                                            // released from
                                            // 'd_mutex' but
                                            // must wait for
                                            // readers to
                                            // finish

    // NOT IMPLEMENTED
    ReaderWriterLock(const ReaderWriterLock&);
    ReaderWriterLock& operator=(const ReaderWriterLock&);

  public:
    ReaderWriterLock();

    void lock();
    int  try_lock();
    void unlock();

    void lock_shared();
    int  try_lock_shared();
    void unlock_shared();
};

ReaderWriterLock::ReaderWriterLock()
: d_state(0)
{
}

void ReaderWriterLock::lock()
{
    // The presence of a pending writer must be noted before attempting the
    // 'mutex.lock' in case this thread blocks on the mutex lock operation.

    d_state.fetch_add(k_PENDING_WRITER_INC, std::memory_order_acq_rel);
    d_mutex.lock();
    if (d_state.fetch_add(k_WRITER - k_PENDING_WRITER_INC,
                          std::memory_order_acq_rel) & k_READER_MASK) {
        // There must be no readers present to obtain the write lock.  By
        // obtaining the mutex, there can be no new readers obtaining a read
        // lock (ensuring this lock is not reader biased).  If there are
        // currently readers present, the last reader to release its read lock
        // will 'post' to 'd_semaphore'.  Note that, since the locking
        // primitive is a semaphore, the timing of the 'wait' and the 'post' is
        // irrelevant.

        d_semaphore.wait();
    }
}

int ReaderWriterLock::try_lock()
{
    // To obtain a write lock, 'd_mutex' must be obtained *and* there must be
    // no readers.

    if (0 == d_mutex.try_lock()) {
        std::int64_t state = d_state.load(std::memory_order_acquire);

        if (0 == (state & k_READER_MASK)) {
            // Since the mutex is obtained and there are no readers (and none
            // can enter while the mutex is held), the lock has been obtained.

            d_state.fetch_add(k_WRITER, std::memory_order_acq_rel);

            return 0;
        }
        d_mutex.unlock();
    }
    return 1;
}

void ReaderWriterLock::unlock()
{
    d_state.fetch_add(-k_WRITER, std::memory_order_acq_rel);
    d_mutex.unlock();
}

void ReaderWriterLock::lock_shared()
{
    std::int64_t state = d_state.load(std::memory_order_acquire);

    do {
        // If there are no actual or pending writers, the lock can be obtained
        // by simply incrementing the reader count.  This results, typically,
        // in a substantial performance benefit when there are very few writers
        // in the system and no noticible degredation in other scenarios.

        if (state & (k_WRITER | k_PENDING_WRITER_MASK)) {
            d_mutex.lock();
            d_state.fetch_add(k_READER_INC, std::memory_order_acq_rel);
            d_mutex.unlock();
            return;
        }
    } while (false == d_state.compare_exchange_strong(
                                                   state,
                                                   state + k_READER_INC,
                                                   std::memory_order_acq_rel));
}

int ReaderWriterLock::try_lock_shared()
{
    std::int64_t state = d_state.load(std::memory_order_acquire);

    // If there are no actual or pending writers, the lock can be obtained by
    // simply incrementing the reader count.  Since this method must return
    // "immediately" if the lock is not obtained, only one attempt will be
    // performed.

    if (0 == (state & (k_WRITER | k_PENDING_WRITER_MASK))) {
        if (d_state.compare_exchange_strong(state,
                                            state + k_READER_INC,
                                            std::memory_order_acq_rel)) {
            return 0;
        }
    }

    // To accomodate the possibility of mutex re-acquisition being important
    // for the performance characteristics of this lock, the mutex acquisition
    // must be attempted.

    if (0 == d_mutex.try_lock()) {
        d_state.fetch_add(k_READER_INC, std::memory_order_acq_rel);
        d_mutex.unlock();
        return 0;
    }
    return 1;
}

void ReaderWriterLock::unlock_shared()
{
    std::int64_t state = d_state.fetch_add(-k_READER_INC);

    // If this is the last reader and there is a pending writer who obtained
    // 'd_mutex' (and hence will be calling 'wait' on 'd_semaphore'), 'post' to
    // 'd_semaphore' to allow the pending writer to complete obtaining the
    // write lock.

    if (k_READER_INC == (state & k_READER_MASK) && (state & k_WRITER)) {
        d_semaphore.post();
    }
}








std::atomic<int> g_state;

int sleepMicroseconds = 20000;
int iterations        = 100;

unsigned int percentRead;

template <typename T>
class Work {
    T            *d_lock;
    int           d_load;
    unsigned int  d_offset;
    unsigned int  d_workDone;

  public:
    Work(T *lock, int load, unsigned int offset)
    : d_lock(lock)
    , d_load(load)
    , d_offset(offset)
    , d_workDone(0)
    {
    }
    
    void doit() {
        while (0 == g_state.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        while (1 == g_state.load(std::memory_order_acquire)) {
            bool doRead = ((d_workDone + d_offset) * 3037 % 100) < percentRead;

            if (doRead) d_lock->lock_shared();
            else d_lock->lock();

            int j;
            escape(&j);
            j = 1;
            for (int i = 0; i < d_load; ++i) {
                j = j * 3 % 7;
                clobber();
            }

            if (doRead) d_lock->unlock_shared();
            else d_lock->unlock();

            ++d_workDone;
        }
    }

    unsigned int workDone() {  return d_workDone;  }
};

typedef Atomic              L0;
typedef Null                L1;
typedef WrappedMutex        L2;
typedef WrappedSemaphore    L3;
typedef Pthread             L4;
typedef Losing              L5;
typedef ReaderWriterLock    L6;

template <class T>
int testOnce(const int numThread, const int load)
{
    g_state.store(0, std::memory_order_release);

    std::vector<std::thread> thread;
    std::vector<Work<T> >    todo;

    T lock;

    for (int i = 0; i < numThread; ++i) {
        todo.push_back(Work<T>(&lock, load, i));
    }
    for (int i = 0; i < numThread; ++i) {
        thread.push_back(std::thread(&Work<T>::doit, &todo[i]));
    }

    timespec start;
    timespec stop;
    
    clock_gettime(CLOCK_MONOTONIC, &start);

    g_state.store(1, std::memory_order_release);

    std::this_thread::sleep_for(std::chrono::microseconds(sleepMicroseconds));

    g_state.store(2, std::memory_order_release);

    clock_gettime(CLOCK_MONOTONIC, &stop);

    int64_t wd = 0;
    for (int i = 0; i < numThread; ++i) {
        thread[i].join();
        wd += todo[i].workDone();
    }

    int64_t rv = nanoseconds(stop, start);

    return (static_cast<int>(wd * 1000000 / rv));
}

template <class T>
int test(const int numThread, const int load)
{
    std::vector<int> result(iterations);

    for (int i = 0; i < iterations; ++i) {
        result[i] = testOnce<T>(numThread, load);
    }

    std::sort(result.begin(), result.end());

    return result[80 * iterations / 100];
}

int main(int argc, char *argv[])
{
    if (argc < 2) return 0;

    percentRead = atoi(argv[1]);

    if (argc > 2) {
        sleepMicroseconds = atoi(argv[2]) * 1000;
    }

    if (argc > 3) {
        iterations = atoi(argv[3]);
    }
    
    int numThreadList[] = { 1, 2, 4, 8, 12, 16, 32 };

    double v;

    printf("load,threads,atomic,null,mutex,semaphore,pthread,losing,rwl\n");
    for (int load = 1; load <= 10000; load *= 10) {
        for (int nti = 0; nti < 7; ++nti) {
            int numThread = numThreadList[nti];

            printf("%i,%i,%i,%i,%i,%i,%i,%i,%i\n",
                   load,
                   numThread,
                   test<L0>(numThread, load),
                   test<L1>(numThread, load),
                   test<L2>(numThread, load),
                   test<L3>(numThread, load),
                   test<L4>(numThread, load),
                   test<L5>(numThread, load),
                   test<L6>(numThread, load));
        }
    }
}

// ----------------------------------------------------------------------------
// Copyright 2017 Bloomberg Finance L.P.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ----------------------------- END-OF-FILE ----------------------------------
