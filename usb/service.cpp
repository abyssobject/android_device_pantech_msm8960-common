#define LOG_TAG "android.hardware.usb-service.msm8960"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "Usb.h"

using ::aidl::android::hardware::usb::Usb;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<Usb> usb = ndk::SharedRefBase::make<Usb>();
    const std::string instance = std::string() + Usb::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(usb->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);
    ABinderProcess_joinThreadPool();
    return -1;
}
