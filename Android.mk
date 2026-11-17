#
# Copyright (C) 2020 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)

ifneq ($(filter ef51 ef52 ef50l,$(TARGET_DEVICE)),)
# Widevine uses the Android 10 protobuf ABI under a versioned filename.
include $(CLEAR_VARS)
LOCAL_MODULE := libprotobuf-cpp-lite-v29
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := .so
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true
LOCAL_MULTILIB := 32
LOCAL_MODULE_TARGET_ARCH := arm
# Android 10 (VNDK v29) libprotobuf-cpp-lite for the RIL blobs; AOSP 16 no longer ships prebuilts/vndk/v29
LOCAL_PREBUILT_MODULE_FILE := $(LOCAL_PATH)/prebuilt/vndk-v29/libprotobuf-cpp-lite.so
LOCAL_STRIP_MODULE := false
# Preserve the original VNDK SONAME while installing the versioned filename.
LOCAL_CHECK_ELF_FILES := false
include $(BUILD_PREBUILT)

include $(call all-makefiles-under,$(LOCAL_PATH))

endif
