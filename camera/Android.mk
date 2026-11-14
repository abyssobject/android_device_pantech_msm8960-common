LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := camera.msm8960
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := CameraWrapper.cpp

LOCAL_C_INCLUDES := \
    system/media/camera/include

LOCAL_SHARED_LIBRARIES := \
    libdl \
    liblog \
    libutils

LOCAL_CFLAGS := -Wall -Werror

# Android 14: libhardware/libcutils headers are no longer implicit
LOCAL_HEADER_LIBRARIES += libhardware_headers libcutils_headers
include $(BUILD_SHARED_LIBRARY)
