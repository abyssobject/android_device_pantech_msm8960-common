/*
 * Copyright (C) 2026 The LineageOS Project
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

#define LOG_TAG "PantechCameraWrapper"

#include <cutils/properties.h>
#include <atomic>
#include <cstdint>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <hardware/camera.h>
#include <hardware/gralloc.h>
#include <log/log.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utils/Mutex.h>

#include <map>
#include <string>
#include <vector>

namespace {

// msm ION ABI of the ef52 3.10 kernel (include/uapi/linux/{ion,msm_ion}.h).
struct EfIonFdData { void* handle; int fd; };
struct EfIonHandleData { void* handle; };
struct EfIonCustomData { unsigned int cmd; unsigned long arg; };
struct EfIonFlushData { void* handle; int fd; void* vaddr; unsigned int offset; unsigned int length; };
#define EF_ION_IOC_FREE _IOWR('I', 1, struct EfIonHandleData)
#define EF_ION_IOC_IMPORT _IOWR('I', 5, struct EfIonFdData)
#define EF_ION_IOC_CUSTOM _IOWR('I', 6, struct EfIonCustomData)
#define EF_ION_IOC_CLEAN_INV_CACHES _IOWR('M', 2, struct EfIonFlushData)

// The vendor HAL hands the record frames to the CPU (A16 HAL1 BUFFER_QUEUE
// path: CameraClient copies them into encoder buffers), but it never does
// cache maintenance for a CPU reader: the VFE writes the ION buffer by DMA
// while CPU cache lines of the same memory still hold old data. Track the
// ION fd behind every fd-backed camera_memory_t so the record frame can be
// cleaned+invalidated before it is passed on.
android::Mutex gIonLock;
int gIonDev = -1;
std::map<const camera_memory_t*, int> gMemoryFds;  // dup'ed ION fds

void rememberMemoryFd(const camera_memory_t* mem, int fd) {
    if (mem == nullptr || fd < 0) return;
    int dupFd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (dupFd < 0) return;
    android::Mutex::Autolock lock(gIonLock);
    auto it = gMemoryFds.find(mem);
    if (it != gMemoryFds.end()) close(it->second);
    gMemoryFds[mem] = dupFd;
}

void forgetAllMemoryFds() {
    android::Mutex::Autolock lock(gIonLock);
    for (auto& entry : gMemoryFds) close(entry.second);
    gMemoryFds.clear();
}

void invalidateForCpu(const camera_memory_t* mem, unsigned int index) {
    if (mem == nullptr || mem->data == nullptr || mem->size == 0) return;
    android::Mutex::Autolock lock(gIonLock);
    auto it = gMemoryFds.find(mem);
    if (it == gMemoryFds.end()) return;
    if (gIonDev < 0) {
        gIonDev = open("/dev/ion", O_RDONLY | O_CLOEXEC);
        if (gIonDev < 0) {
            ALOGE("Unable to open /dev/ion for record frame cache maintenance: %d", errno);
            return;
        }
    }
    EfIonFdData fdData = {nullptr, it->second};
    if (ioctl(gIonDev, EF_ION_IOC_IMPORT, &fdData) != 0) {
        ALOGE("ION import of record frame buffer failed: %d", errno);
        return;
    }
    EfIonFlushData flush = {fdData.handle, it->second,
                            static_cast<uint8_t*>(mem->data) + index * mem->size,
                            static_cast<unsigned int>(index * mem->size),
                            static_cast<unsigned int>(mem->size)};
    EfIonCustomData custom = {EF_ION_IOC_CLEAN_INV_CACHES,
                              reinterpret_cast<unsigned long>(&flush)};
    if (ioctl(gIonDev, EF_ION_IOC_CUSTOM, &custom) != 0) {
        ALOGE("ION cache invalidate of record frame %u failed: %d", index, errno);
    }
    EfIonHandleData handleData = {fdData.handle};
    ioctl(gIonDev, EF_ION_IOC_FREE, &handleData);
}

constexpr const char* kVendorCameraHal =
        "/vendor/lib/hw/camera.msm8960.vendor.so";
constexpr const char* kSetRecordingHintValueSymbol =
        "_ZN7android24QCameraHardwareInterface21setRecordingHintValueEi";
constexpr const char* kCreateRecordSymbol =
        "_ZN7android24QCameraHardwareInterface12createRecordEv";
constexpr const char* kInitRecordStreamSymbol =
        "_ZN7android20QCameraStream_record10initStreamEhh";
constexpr size_t kHalObjectOffset = 0x48;
constexpr size_t kRecordStreamOffset = 0x284;
constexpr size_t kRecordingHintOffset = 0x59e;
constexpr size_t kPreviewRestartNeededOffset = 0x5b0;

struct WrappedDevice {
    camera_device_t* device;
    camera_device_ops_t* vendorOps;
    int (*vendorClose)(hw_device_t* device);
    camera_device_ops_t ops;
    std::atomic<int32_t> enabledMessages{0};
    camera_notify_callback notify;
    camera_data_callback data;
    camera_data_timestamp_callback timestamp;
    camera_request_memory memory;
    void* callbackUser;
    size_t callbackSlot;
    preview_stream_ops* window;
    int (*setUsage)(preview_stream_ops*, int);

};

void* gVendorHandle;
camera_module_t* gVendorModule;
using SetRecordingHintValue = int (*)(void*, int);
SetRecordingHintValue gSetRecordingHintValue;
using CreateRecord = int (*)(void*);
CreateRecord gCreateRecord;
using InitRecordStream = int (*)(void*, uint8_t, uint8_t);
InitRecordStream gInitRecordStream;
android::Mutex gLock;
std::vector<WrappedDevice*> gDevices;
WrappedDevice* gMemoryDevices[4] = {};

WrappedDevice* findDeviceLocked(camera_device_t* device) {
    for (WrappedDevice* wrapped : gDevices) {
        if (wrapped->device == device) {
            return wrapped;
        }
    }
    return nullptr;
}

int loadVendorModule() {
    android::Mutex::Autolock lock(gLock);
    if (gVendorModule != nullptr) {
        return 0;
    }

    gVendorHandle = dlopen(kVendorCameraHal, RTLD_NOW | RTLD_LOCAL);
    if (gVendorHandle == nullptr) {
        ALOGE("Unable to load %s: %s", kVendorCameraHal, dlerror());
        return -ENODEV;
    }

    gVendorModule = reinterpret_cast<camera_module_t*>(
            dlsym(gVendorHandle, HAL_MODULE_INFO_SYM_AS_STR));
    if (gVendorModule == nullptr) {
        ALOGE("Unable to find %s in vendor camera HAL: %s",
              HAL_MODULE_INFO_SYM_AS_STR, dlerror());
        dlclose(gVendorHandle);
        gVendorHandle = nullptr;
        return -EINVAL;
    }

    gSetRecordingHintValue = reinterpret_cast<SetRecordingHintValue>(
            dlsym(gVendorHandle, kSetRecordingHintValueSymbol));
    if (gSetRecordingHintValue == nullptr) {
        ALOGE("Unable to find vendor recording hint setter: %s", dlerror());
        return -EINVAL;
    }
    gCreateRecord = reinterpret_cast<CreateRecord>(
            dlsym(gVendorHandle, kCreateRecordSymbol));
    if (gCreateRecord == nullptr) {
        ALOGE("Unable to find vendor record stream creator: %s", dlerror());
        return -EINVAL;
    }
    gInitRecordStream = reinterpret_cast<InitRecordStream>(
            dlsym(gVendorHandle, kInitRecordStreamSymbol));
    if (gInitRecordStream == nullptr) {
        ALOGE("Unable to find vendor record stream initializer: %s", dlerror());
        return -EINVAL;
    }

    return 0;
}

int ensureRecordStream(camera_device_t* device) {
    if (device->priv == nullptr) {
        return -ENODEV;
    }
    auto privateData = static_cast<uint8_t*>(device->priv);
    auto hal = *reinterpret_cast<uint8_t**>(privateData + kHalObjectOffset);
    if (hal == nullptr) {
        return -ENODEV;
    }
    auto recordStream = *reinterpret_cast<void**>(hal + kRecordStreamOffset);
    if (recordStream == nullptr) {
        int rc = gCreateRecord(hal);
        if (rc < 0) {
            return rc;
        }
        recordStream = *reinterpret_cast<void**>(hal + kRecordStreamOffset);
    }
    if (recordStream == nullptr) {
        return -ENODEV;
    }
    return gInitRecordStream(recordStream, 0, 1);
}

// Pie installs the HAL callbacks during open(), before CameraClient has
// installed its own callbacks. The legacy blob can send unsolicited events
// in that interval; only forward notifications requested by enable_msg_type.
void notifyCallback(int32_t type, int32_t ext1, int32_t ext2, void* user) {
    auto wrapped = static_cast<WrappedDevice*>(user);
    if ((wrapped->enabledMessages.load() & type) != 0 && wrapped->notify) {
        wrapped->notify(type, ext1, ext2, wrapped->callbackUser);
    }
}

void dataCallback(int32_t type, const camera_memory_t* data, unsigned int index,
                  camera_frame_metadata_t* metadata, void* user) {
    auto wrapped = static_cast<WrappedDevice*>(user);
    if ((wrapped->enabledMessages.load() & type) != 0 && wrapped->data) {
        wrapped->data(type, data, index, metadata, wrapped->callbackUser);
    }
}

void timestampCallback(int64_t timestamp, int32_t type,
                       const camera_memory_t* data, unsigned int index, void* user) {
    auto wrapped = static_cast<WrappedDevice*>(user);
    // The record frame is read by the CPU next (CameraClient copies it into
    // the encoder's buffer): drop stale cache lines over the DMA-written data.
    invalidateForCpu(data, index);
    if ((wrapped->enabledMessages.load() & type) != 0 && wrapped->timestamp) {
        wrapped->timestamp(timestamp, type, data, index, wrapped->callbackUser);
    }
}

template <size_t Slot>
camera_memory_t* memoryCallback(int fd, size_t size, unsigned int count, void*) {
    camera_request_memory memory = nullptr;
    void* callbackUser = nullptr;
    {
        android::Mutex::Autolock lock(gLock);
        // The blob passes either its HAL or a stream object as the cookie.
        // A per-device trampoline restores the actual set_callbacks cookie
        // without depending on the layout of those proprietary objects.
        WrappedDevice* wrapped = gMemoryDevices[Slot];
        if (wrapped != nullptr) {
            memory = wrapped->memory;
            callbackUser = wrapped->callbackUser;
        }
    }
    camera_memory_t* mem = memory ? memory(fd, size, count, callbackUser) : nullptr;
    rememberMemoryFd(mem, fd);
    return mem;
}

constexpr camera_request_memory kMemoryCallbacks[] = {
    memoryCallback<0>, memoryCallback<1>, memoryCallback<2>, memoryCallback<3>,
};

void wrappedSetCallbacks(camera_device_t* device, camera_notify_callback notify,
                         camera_data_callback data,
                         camera_data_timestamp_callback timestamp,
                         camera_request_memory memory, void* user) {
    WrappedDevice* wrapped;
    {
        android::Mutex::Autolock lock(gLock);
        wrapped = findDeviceLocked(device);
    }
    if (wrapped == nullptr) return;
    wrapped->notify = notify;
    wrapped->data = data;
    wrapped->timestamp = timestamp;
    wrapped->memory = memory;
    wrapped->callbackUser = user;
    wrapped->vendorOps->set_callbacks(device, notifyCallback, dataCallback,
                                     timestampCallback, kMemoryCallbacks[wrapped->callbackSlot], wrapped);
}

void wrappedEnableMsgType(camera_device_t* device, int32_t types) {
    WrappedDevice* wrapped;
    {
        android::Mutex::Autolock lock(gLock);
        wrapped = findDeviceLocked(device);
    }
    if (wrapped == nullptr) return;
    wrapped->enabledMessages.fetch_or(types);
    wrapped->vendorOps->enable_msg_type(device, types);
}

void wrappedDisableMsgType(camera_device_t* device, int32_t types) {
    WrappedDevice* wrapped;
    {
        android::Mutex::Autolock lock(gLock);
        wrapped = findDeviceLocked(device);
    }
    if (wrapped == nullptr) return;
    wrapped->enabledMessages.fetch_and(~types);
    wrapped->vendorOps->disable_msg_type(device, types);
}

int wrappedSetUsage(preview_stream_ops* window, int usage) {
    int (*setUsage)(preview_stream_ops*, int) = nullptr;
    {
        android::Mutex::Autolock lock(gLock);
        for (WrappedDevice* wrapped : gDevices) {
            if (wrapped->window == window) {
                setUsage = wrapped->setUsage;
                break;
            }
        }
    }
    // Use the MSM8960 IOMMU heap for ordinary preview buffers. The legacy
    // MM heap flag occupies bit 31, which the HAL1 HIDL adapter sign-extends
    // into invalid 64-bit usage bits on Pie. Protected buffers keep their
    // original heap requirements.
    if (!(usage & GRALLOC_USAGE_PROTECTED) &&
            (static_cast<uint32_t>(usage) & GRALLOC_USAGE_PRIVATE_3)) {
        usage = (static_cast<uint32_t>(usage) & ~GRALLOC_USAGE_PRIVATE_3) |
                GRALLOC_USAGE_PRIVATE_2;
    }
    return setUsage ? setUsage(window, usage) : -ENODEV;
}

int wrappedSetPreviewWindow(camera_device_t* device, preview_stream_ops* window) {
    camera_device_ops_t* vendorOps;
    {
        android::Mutex::Autolock lock(gLock);
        WrappedDevice* wrapped = findDeviceLocked(device);
        if (wrapped == nullptr) return -ENODEV;
        if (wrapped->window != nullptr) {
            wrapped->window->set_usage = wrapped->setUsage;
        }
        wrapped->window = window;
        wrapped->setUsage = window ? window->set_usage : nullptr;
        if (window != nullptr) window->set_usage = wrappedSetUsage;
        vendorOps = wrapped->vendorOps;
    }
    return vendorOps->set_preview_window(device, window);
}

int wrappedStartPreview(camera_device_t* device) {
    camera_device_ops_t* vendorOps;
    {
        android::Mutex::Autolock lock(gLock);
        WrappedDevice* wrapped = findDeviceLocked(device);
        if (wrapped == nullptr) {
            return -ENODEV;
        }
        vendorOps = wrapped->vendorOps;
    }

    // An already running preview (including recording) needs no new streams.
    // Let the vendor HAL handle repeated start_preview calls itself.
    if (vendorOps->preview_enabled(device)) {
        return vendorOps->start_preview(device);
    }

    // After a non-ZSL snapshot, the vendor HAL keeps its capture channel
    // active until cancelPictureInternal() runs from startPreview(). Adding
    // our record stream before that cleanup fails with -EINVAL. Use the HAL1
    // entry point to finish the snapshot first; it is a no-op when stopped.
    // Do this outside gLock because cleanup can wait for camera callbacks.
    int rc = vendorOps->cancel_picture(device);
    if (rc != 0) {
        ALOGE("Unable to finish vendor snapshot before preview: %d", rc);
        return rc;
    }

    rc = ensureRecordStream(device);
    if (rc != 0) {
        ALOGE("Unable to prepare vendor record stream before preview: %d", rc);
        return rc;
    }
    ALOGI("Prepared vendor record stream before preview");
    return vendorOps->start_preview(device);
}

int wrappedStartRecording(camera_device_t* device) {
    camera_device_ops_t* vendorOps;
    {
        android::Mutex::Autolock lock(gLock);
        WrappedDevice* wrapped = findDeviceLocked(device);
        if (wrapped == nullptr) {
            ALOGE("start_recording called for an unknown camera device");
            return -ENODEV;
        }
        vendorOps = wrapped->vendorOps;
    }

    if (device->priv != nullptr) {
        auto privateData = static_cast<uint8_t*>(device->priv);
        auto hal = *reinterpret_cast<uint8_t**>(privateData + kHalObjectOffset);
        if (hal != nullptr && hal[kPreviewRestartNeededOffset] != 0 &&
                vendorOps->preview_enabled(device)) {
            // The record stream was initialized in wrappedStartPreview() with the
            // video size of that moment (the HAL default is 1920x1088). When the
            // app changes video-size afterwards, the HAL asks for a preview
            // restart. Without it the record stream keeps delivering 1920x1088
            // NV12 frames while the encoder is configured for the new size, and
            // it reads luma as chroma (all-green video). Restart through our
            // start_preview path so the record stream is re-initialized.
            ALOGI("Video size changed during preview: restarting preview before recording");
            vendorOps->stop_preview(device);
            int rc = wrappedStartPreview(device);
            if (rc != 0) {
                ALOGE("Unable to restart preview for the new video size: %d", rc);
                return rc;
            }
        }
        if (hal != nullptr) {
            int rc = gSetRecordingHintValue(hal, 1);
            if (rc != 0) {
                ALOGE("Unable to enable vendor recording mode: %d", rc);
                return rc;
            }
            hal[kRecordingHintOffset] = 1;
            hal[kPreviewRestartNeededOffset] = 0;
            auto recordStream =
                    *reinterpret_cast<void**>(hal + kRecordStreamOffset);
            if (recordStream == nullptr) {
                return -ENODEV;
            }
            ALOGI("Starting pre-initialized vendor record stream");
        }
    }

    return vendorOps->start_recording != nullptr
            ? vendorOps->start_recording(device) : -ENOSYS;
}

int wrappedSetParameters(camera_device_t* device, const char* parameters) {
    camera_device_ops_t* vendorOps;
    {
        android::Mutex::Autolock lock(gLock);
        WrappedDevice* wrapped = findDeviceLocked(device);
        if (wrapped == nullptr) {
            return -ENODEV;
        }
        vendorOps = wrapped->vendorOps;
    }

    if (vendorOps->set_parameters == nullptr) {
        return -ENOSYS;
    }
    if (parameters == nullptr) {
        return vendorOps->set_parameters(device, parameters);
    }

    std::string adjusted(parameters);
    constexpr const char* enabled = "recording-hint=true";
    size_t pos = adjusted.find(enabled);
    // A16: pass the real recording hint (VFE video mode) by default; HAL1 video
    // frames only flow in video mode. persist.camera.ef52.rec_hint=0 restores the old rewrite.
    if (pos != std::string::npos &&
            property_get_bool("persist.camera.ef52.rec_hint", true)) {
        ALOGI("Passing recording-hint=true to the vendor HAL");
        pos = std::string::npos;
    }
    if (pos != std::string::npos) {
        adjusted.replace(pos, std::char_traits<char>::length(enabled),
                         "recording-hint=false");
        ALOGI("Keeping video preview in non-recording mode");
    }
    return vendorOps->set_parameters(device, adjusted.c_str());
}

int wrappedStoreMetaDataInBuffers(camera_device_t* device, int enable) {
    camera_device_ops_t* vendorOps;
    {
        android::Mutex::Autolock lock(gLock);
        WrappedDevice* wrapped = findDeviceLocked(device);
        if (wrapped == nullptr) {
            return -ENODEV;
        }
        vendorOps = wrapped->vendorOps;
    }

    // This blob reports metadata-buffer support but its timestamp callback
    // contains raw YUV camera_memory_t buffers. Make CameraSource select its
    // YUV callback path instead of interpreting image bytes as native_handle_t.
    if (enable != 0) {
        ALOGI("Rejecting unsupported video metadata-buffer mode");
        return -ENOSYS;
    }
    return vendorOps->store_meta_data_in_buffers != nullptr
            ? vendorOps->store_meta_data_in_buffers(device, 0) : 0;
}

int wrappedClose(hw_device_t* hwDevice) {
    camera_device_t* device = reinterpret_cast<camera_device_t*>(hwDevice);
    WrappedDevice* wrapped = nullptr;
    {
        android::Mutex::Autolock lock(gLock);
        for (auto it = gDevices.begin(); it != gDevices.end(); ++it) {
            if ((*it)->device == device) {
                wrapped = *it;
                if (wrapped->window != nullptr) {
                    wrapped->window->set_usage = wrapped->setUsage;
                }
                gDevices.erase(it);
                break;
            }
        }
    }

    if (wrapped == nullptr) {
        return -ENODEV;
    }

    device->ops = wrapped->vendorOps;
    device->common.close = wrapped->vendorClose;
    int rc = wrapped->vendorClose != nullptr
            ? wrapped->vendorClose(&device->common) : 0;
    {
        android::Mutex::Autolock lock(gLock);
        gMemoryDevices[wrapped->callbackSlot] = nullptr;
    }
    // The vendor HAL has released its memory; drop our ION fd references.
    forgetAllMemoryFds();
    delete wrapped;
    return rc;
}

int wrappedOpen(const hw_module_t*, const char* id, hw_device_t** hwDevice) {
    int rc = loadVendorModule();
    if (rc != 0) {
        return rc;
    }

    hw_device_t* vendorHwDevice = nullptr;
    rc = gVendorModule->common.methods->open(
            &gVendorModule->common, id, &vendorHwDevice);
    if (rc != 0) {
        return rc;
    }

    camera_device_t* device = reinterpret_cast<camera_device_t*>(vendorHwDevice);
    WrappedDevice* wrapped = new WrappedDevice{};
    wrapped->device = device;
    wrapped->vendorOps = device->ops;
    wrapped->vendorClose = device->common.close;
    wrapped->ops = *device->ops;
    wrapped->ops.set_preview_window = wrappedSetPreviewWindow;
    wrapped->ops.set_callbacks = wrappedSetCallbacks;
    wrapped->ops.enable_msg_type = wrappedEnableMsgType;
    wrapped->ops.disable_msg_type = wrappedDisableMsgType;
    wrapped->ops.set_parameters = wrappedSetParameters;
    wrapped->ops.store_meta_data_in_buffers = wrappedStoreMetaDataInBuffers;
    wrapped->ops.start_preview = wrappedStartPreview;
    wrapped->ops.start_recording = wrappedStartRecording;

    device->ops = &wrapped->ops;
    device->common.close = wrappedClose;
    {
        android::Mutex::Autolock lock(gLock);
        size_t slot = 0;
        while (slot < 4 && gMemoryDevices[slot] != nullptr) ++slot;
        if (slot == 4) {
            device->ops = wrapped->vendorOps;
            device->common.close = wrapped->vendorClose;
            if (wrapped->vendorClose) wrapped->vendorClose(vendorHwDevice);
            delete wrapped;
            return -EMFILE;
        }
        wrapped->callbackSlot = slot;
        gMemoryDevices[slot] = wrapped;
        gDevices.push_back(wrapped);
    }

    *hwDevice = vendorHwDevice;
    return 0;
}

int getNumberOfCameras() {
    return loadVendorModule() == 0 &&
            gVendorModule->get_number_of_cameras != nullptr
            ? gVendorModule->get_number_of_cameras() : 0;
}

int getCameraInfo(int cameraId, camera_info* info) {
    int rc = loadVendorModule();
    return rc == 0 && gVendorModule->get_camera_info != nullptr
            ? gVendorModule->get_camera_info(cameraId, info) : rc;
}

int setCallbacks(const camera_module_callbacks_t* callbacks) {
    int rc = loadVendorModule();
    return rc == 0 && gVendorModule->set_callbacks != nullptr
            ? gVendorModule->set_callbacks(callbacks) : rc;
}

void getVendorTagOps(vendor_tag_ops_t* ops) {
    if (loadVendorModule() == 0 && gVendorModule->get_vendor_tag_ops != nullptr) {
        gVendorModule->get_vendor_tag_ops(ops);
    }
}

int openLegacy(const hw_module_t*, const char* id, uint32_t halVersion,
               hw_device_t** device) {
    int rc = loadVendorModule();
    return rc == 0 && gVendorModule->open_legacy != nullptr
            ? gVendorModule->open_legacy(&gVendorModule->common, id,
                                         halVersion, device) : -ENOSYS;
}

int setTorchMode(const char* cameraId, bool enabled) {
    int rc = loadVendorModule();
    return rc == 0 && gVendorModule->set_torch_mode != nullptr
            ? gVendorModule->set_torch_mode(cameraId, enabled) : -ENOSYS;
}

int initModule() {
    int rc = loadVendorModule();
    return rc == 0 && gVendorModule->init != nullptr
            ? gVendorModule->init() : rc;
}

hw_module_methods_t gModuleMethods = {
    .open = wrappedOpen,
};

}  // namespace

extern "C" camera_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        // The proprietary module predates camera module API 2.0 and leaves
        // camera_info::device_version at zero. Advertising 1.0 makes the
        // framework correctly treat each device as HAL1.
        .module_api_version = CAMERA_MODULE_API_VERSION_1_0,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = CAMERA_HARDWARE_MODULE_ID,
        .name = "Pantech MSM8960 Camera Wrapper",
        .author = "The LineageOS Project",
        .methods = &gModuleMethods,
        .dso = nullptr,
        .reserved = {0},
    },
    .get_number_of_cameras = getNumberOfCameras,
    .get_camera_info = getCameraInfo,
    .set_callbacks = setCallbacks,
    .get_vendor_tag_ops = getVendorTagOps,
    .open_legacy = openLegacy,
    .set_torch_mode = setTorchMode,
    .init = initModule,
    .reserved = {},
};
