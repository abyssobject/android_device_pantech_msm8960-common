/*
 * Copyright (C) 2026 The Android Open Source Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * Vendor-partition libstdc++.so for the legacy msm8960 blobs.
 *
 * Almost every pre-Treble Qualcomm/Pantech blob has DT_NEEDED libstdc++.so.
 * bionic's libstdc++ is a system-only library, so on Android 16 (always
 * Treble linker namespaces) it is invisible to the vendor and sphal
 * namespaces. This library exports exactly the ABI of bionic's libstdc++
 * (bionic/libc/libstdc++.map.txt): global operator new/delete, std::nothrow,
 * the Itanium/ARM-EABI static-init guards and __cxa_pure_virtual, with the
 * same semantics as bionic/libc/bionic/{new,__cxa_guard,__cxa_pure_virtual}.cpp.
 */

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>

#include <android/log.h>

#define EXPORT __attribute__((visibility("default")))

[[noreturn]] static void fatal(const char* msg) {
    __android_log_write(ANDROID_LOG_FATAL, "libstdc++", msg);
    abort();
}

namespace std {
struct nothrow_t {
    explicit nothrow_t() = default;
};
extern EXPORT const nothrow_t nothrow;
const nothrow_t nothrow{};
}  // namespace std

EXPORT void* operator new(size_t size) {
    void* p = malloc(size);
    if (p == nullptr) fatal("new failed to allocate bytes");
    return p;
}

EXPORT void* operator new[](size_t size) {
    void* p = malloc(size);
    if (p == nullptr) fatal("new[] failed to allocate bytes");
    return p;
}

EXPORT void* operator new(size_t size, const std::nothrow_t&) noexcept {
    return malloc(size);
}

EXPORT void* operator new[](size_t size, const std::nothrow_t&) noexcept {
    return malloc(size);
}

EXPORT void operator delete(void* p) noexcept {
    free(p);
}

EXPORT void operator delete[](void* p) noexcept {
    free(p);
}

EXPORT void operator delete(void* p, const std::nothrow_t&) noexcept {
    free(p);
}

EXPORT void operator delete[](void* p, const std::nothrow_t&) noexcept {
    free(p);
}

// ARM EABI guard: a 32-bit word whose least significant bit is set once the
// object is constructed (the compiler tests that bit inline). Bit 8 marks a
// construction in progress by some thread.
static constexpr int kComplete = 0x1;
static constexpr int kPending = 0x100;

static pthread_mutex_t g_guard_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_guard_cond = PTHREAD_COND_INITIALIZER;

extern "C" EXPORT int __cxa_guard_acquire(int* gv) {
    if (__atomic_load_n(gv, __ATOMIC_ACQUIRE) & kComplete) return 0;

    pthread_mutex_lock(&g_guard_mutex);
    for (;;) {
        int v = __atomic_load_n(gv, __ATOMIC_ACQUIRE);
        if (v & kComplete) {
            pthread_mutex_unlock(&g_guard_mutex);
            return 0;
        }
        if (!(v & kPending)) {
            __atomic_store_n(gv, kPending, __ATOMIC_RELAXED);
            pthread_mutex_unlock(&g_guard_mutex);
            return 1;
        }
        pthread_cond_wait(&g_guard_cond, &g_guard_mutex);
    }
}

extern "C" EXPORT void __cxa_guard_release(int* gv) {
    pthread_mutex_lock(&g_guard_mutex);
    __atomic_store_n(gv, kComplete, __ATOMIC_RELEASE);
    pthread_cond_broadcast(&g_guard_cond);
    pthread_mutex_unlock(&g_guard_mutex);
}

extern "C" EXPORT void __cxa_guard_abort(int* gv) {
    pthread_mutex_lock(&g_guard_mutex);
    __atomic_store_n(gv, 0, __ATOMIC_RELAXED);
    pthread_cond_broadcast(&g_guard_cond);
    pthread_mutex_unlock(&g_guard_mutex);
}

extern "C" EXPORT void __cxa_pure_virtual() {
    fatal("Pure virtual function called. Are you calling virtual methods from a destructor?");
}
