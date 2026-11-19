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

#define LOG_TAG "vibrator.msm8960"

#include "Vibrator.h"

#include <android-base/file.h>
#include <android-base/logging.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

namespace aidl::android::hardware::vibrator {

namespace {

constexpr char kEnablePath[] = "/sys/class/timed_output/vibrator/enable";

// drivers/misc/pm8xxx-vibrator.c (FEATURE_LEVEL_CONTROL): bits 31..16 are the
// drive level, bits 15..0 the timeout in ms. Level 20 gives 3.1 V, as the
// Android 4 vibrator HAL (device/pantech/ef52/vibrator) used.
constexpr int32_t kLevel = 20;
// A timeout above the board's max_timeout_ms (15000) means "forever" to the
// driver, so never send more.
constexpr int32_t kMaxTimeoutMs = 15000;

ndk::ScopedAStatus unsupported() {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace

ndk::ScopedAStatus Vibrator::write(int32_t value) {
    if (!::android::base::WriteStringToFile(std::to_string(value) + "\n", kEnablePath)) {
        PLOG(ERROR) << "Failed to write " << value << " to " << kEnablePath;
        return ndk::ScopedAStatus::fromExceptionCode(EX_SERVICE_SPECIFIC);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getCapabilities(int32_t* _aidl_return) {
    *_aidl_return = IVibrator::CAP_ON_CALLBACK;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::off() {
    return write(0);
}

ndk::ScopedAStatus Vibrator::on(int32_t timeoutMs,
                                const std::shared_ptr<IVibratorCallback>& callback) {
    if (timeoutMs <= 0) {
        return off();
    }
    const int32_t ms = std::min(timeoutMs, kMaxTimeoutMs);
    ndk::ScopedAStatus status = write((kLevel << 16) | ms);
    if (!status.isOk()) {
        return status;
    }
    if (callback != nullptr) {
        // The kernel timer stops the motor; report completion after it.
        std::thread([callback, timeoutMs] {
            std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
            callback->onComplete();
        }).detach();
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::perform(Effect, EffectStrength,
                                     const std::shared_ptr<IVibratorCallback>&, int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getSupportedEffects(std::vector<Effect>* _aidl_return) {
    _aidl_return->clear();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::setAmplitude(float) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::setExternalControl(bool) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getCompositionDelayMax(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getCompositionSizeMax(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getSupportedPrimitives(std::vector<CompositePrimitive>* supported) {
    supported->clear();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getPrimitiveDuration(CompositePrimitive, int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::compose(const std::vector<CompositeEffect>&,
                                     const std::shared_ptr<IVibratorCallback>&) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getSupportedAlwaysOnEffects(std::vector<Effect>* _aidl_return) {
    _aidl_return->clear();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::alwaysOnEnable(int32_t, Effect, EffectStrength) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::alwaysOnDisable(int32_t) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getResonantFrequency(float*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getQFactor(float*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getFrequencyResolution(float*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getFrequencyMinimum(float*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getBandwidthAmplitudeMap(std::vector<float>*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPwlePrimitiveDurationMax(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPwleCompositionSizeMax(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getSupportedBraking(std::vector<Braking>* supported) {
    supported->clear();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::composePwle(const std::vector<PrimitivePwle>&,
                                         const std::shared_ptr<IVibratorCallback>&) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::performVendorEffect(const VendorEffect&,
                                                 const std::shared_ptr<IVibratorCallback>&) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getFrequencyToOutputAccelerationMap(
        std::vector<FrequencyAccelerationMapEntry>*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPwleV2PrimitiveDurationMaxMillis(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPwleV2CompositionSizeMax(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::getPwleV2PrimitiveDurationMinMillis(int32_t*) {
    return unsupported();
}

ndk::ScopedAStatus Vibrator::composePwleV2(const CompositePwleV2&,
                                           const std::shared_ptr<IVibratorCallback>&) {
    return unsupported();
}

}  // namespace aidl::android::hardware::vibrator
