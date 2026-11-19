/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * set_sched_policy() for the legacy GPS blobs.
 *
 * libloc_eng.so (and libloc_core.so) import set_sched_policy without having
 * libprocessgroup in DT_NEEDED; on Android 14 the symbol came from the
 * system libcutils dependency tree, which the Android 16 vendor libcutils no
 * longer has. LineageOS supplied it through an LD shim, which AOSP bionic
 * does not support.
 *
 * This library is linked with -z global (DF_1_GLOBAL) and is a DT_NEEDED of
 * the GNSS HAL service executable, so the linker puts it in the global group
 * of the process and gps.msm8960.so -> libloc_eng.so, dlopen()ed later,
 * resolves set_sched_policy here. The call is forwarded to the real
 * implementation in libprocessgroup.
 */

#include <dlfcn.h>

#define LOG_TAG "sched_policy_shim"
#include <log/log.h>

namespace {

using SetSchedPolicyFn = int (*)(int, int);

SetSchedPolicyFn realSetSchedPolicy() {
    static SetSchedPolicyFn fn = [] {
        // dlsym() on a handle searches that library's own dependency tree, so
        // this finds the libprocessgroup definition, not this one.
        void* handle = dlopen("libprocessgroup.so", RTLD_NOW);
        SetSchedPolicyFn f =
                handle ? reinterpret_cast<SetSchedPolicyFn>(dlsym(handle, "set_sched_policy"))
                       : nullptr;
        if (f == nullptr) {
            ALOGW("set_sched_policy not found in libprocessgroup: %s", dlerror());
        }
        return f;
    }();
    return fn;
}

}  // namespace

extern "C" int set_sched_policy(int tid, int policy) {
    SetSchedPolicyFn fn = realSetSchedPolicy();
    // Scheduling policy is only a hint for the GPS worker threads.
    return fn ? fn(tid, policy) : 0;
}

extern "C" void msm8960_sched_policy_shim_init() {
    // Referenced by the GNSS service so that the DT_NEEDED entry is kept.
}
