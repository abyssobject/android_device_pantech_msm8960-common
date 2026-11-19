#pragma once

#include <aidl/android/hardware/usb/BnUsb.h>
#include <aidl/android/hardware/usb/IUsbCallback.h>

#include <mutex>

namespace aidl {
namespace android {
namespace hardware {
namespace usb {

using ::ndk::ScopedAStatus;

struct Usb : public BnUsb {
    Usb();

    ScopedAStatus enableContaminantPresenceDetection(const std::string& in_portName,
            bool in_enable, int64_t in_transactionId) override;
    ScopedAStatus queryPortStatus(int64_t in_transactionId) override;
    ScopedAStatus setCallback(const std::shared_ptr<IUsbCallback>& in_callback) override;
    ScopedAStatus switchRole(const std::string& in_portName, const PortRole& in_role,
            int64_t in_transactionId) override;
    ScopedAStatus enableUsbData(const std::string& in_portName, bool in_enable,
            int64_t in_transactionId) override;
    ScopedAStatus enableUsbDataWhileDocked(const std::string& in_portName,
            int64_t in_transactionId) override;
    ScopedAStatus limitPowerTransfer(const std::string& in_portName, bool in_limit,
            int64_t in_transactionId) override;
    ScopedAStatus resetUsbPort(const std::string& in_portName,
            int64_t in_transactionId) override;

  private:
    void notifyStatus(Status status);
    static void* ueventThread(void* arg);

    std::mutex mLock;
    std::shared_ptr<IUsbCallback> mCallback;
};

}  // namespace usb
}  // namespace hardware
}  // namespace android
}  // namespace aidl
