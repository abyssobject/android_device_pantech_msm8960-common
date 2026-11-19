/*
 * Minimal AIDL USB HAL for msm8960 (APQ8064, ef52): one micro-USB port,
 * peripheral only. The kernel uses the legacy android_usb gadget (no
 * Type-C class), so the port status is derived from the "usb" power supply:
 * cable present -> device data role / sink power role.
 *
 * Without a USB HAL the framework reports DATA_ROLE_NONE and Settings
 * greys out every "USB preferences" option (file transfer, PTP, tethering).
 */

#define LOG_TAG "android.hardware.usb-service.msm8960"

#include "Usb.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <linux/netlink.h>
#include <poll.h>
#include <pthread.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

namespace aidl {
namespace android {
namespace hardware {
namespace usb {

static constexpr const char* kPortName = "usb0";
static constexpr const char* kUsbOnline = "/sys/class/power_supply/usb/online";

static bool cableConnected() {
    std::string s;
    if (!::android::base::ReadFileToString(kUsbOnline, &s)) return true;
    return ::android::base::Trim(s) == "1";
}

static PortStatus makeStatus() {
    bool connected = cableConnected();
    PortStatus st;
    st.portName = kPortName;
    st.currentDataRole = connected ? PortDataRole::DEVICE : PortDataRole::NONE;
    st.currentPowerRole = connected ? PortPowerRole::SINK : PortPowerRole::NONE;
    st.currentMode = connected ? PortMode::UFP : PortMode::NONE;
    st.canChangeMode = false;
    st.canChangeDataRole = false;
    st.canChangePowerRole = false;
    st.supportedModes = {PortMode::UFP};
    st.supportedContaminantProtectionModes = {ContaminantProtectionMode::NONE};
    st.supportsEnableContaminantPresenceProtection = false;
    st.contaminantProtectionStatus = ContaminantProtectionStatus::NONE;
    st.supportsEnableContaminantPresenceDetection = false;
    st.contaminantDetectionStatus = ContaminantDetectionStatus::NOT_SUPPORTED;
    st.usbDataStatus = {UsbDataStatus::ENABLED};
    st.powerTransferLimited = false;
    st.powerBrickStatus = PowerBrickStatus::NOT_CONNECTED;
    return st;
}

void Usb::notifyStatus(Status status) {
    std::lock_guard<std::mutex> l(mLock);
    if (!mCallback) return;
    std::vector<PortStatus> list = {makeStatus()};
    auto ret = mCallback->notifyPortStatusChange(list, status);
    if (!ret.isOk()) LOG(ERROR) << "notifyPortStatusChange failed: " << ret.getDescription();
}

void* Usb::ueventThread(void* arg) {
    Usb* usb = static_cast<Usb*>(arg);
    struct sockaddr_nl addr = {};
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = 0;
    addr.nl_groups = 0xffffffff;
    int fd = socket(PF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (fd < 0 || bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        PLOG(ERROR) << "uevent socket";
        if (fd >= 0) close(fd);
        return nullptr;
    }
    bool last = cableConnected();
    char buf[2048];
    for (;;) {
        ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) continue;
        buf[n] = '\0';
        bool power = false;
        for (char* p = buf; p < buf + n; p += strlen(p) + 1) {
            if (!strcmp(p, "SUBSYSTEM=power_supply")) power = true;
        }
        if (!power) continue;
        bool now = cableConnected();
        if (now != last) {
            last = now;
            LOG(INFO) << "usb cable " << (now ? "connected" : "disconnected");
            usb->notifyStatus(Status::SUCCESS);
        }
    }
    return nullptr;
}

Usb::Usb() {
    pthread_t t;
    if (pthread_create(&t, nullptr, ueventThread, this) == 0) pthread_detach(t);
}

ScopedAStatus Usb::setCallback(const std::shared_ptr<IUsbCallback>& in_callback) {
    {
        std::lock_guard<std::mutex> l(mLock);
        mCallback = in_callback;
    }
    notifyStatus(Status::SUCCESS);
    return ScopedAStatus::ok();
}

ScopedAStatus Usb::queryPortStatus(int64_t in_transactionId) {
    notifyStatus(Status::SUCCESS);
    std::lock_guard<std::mutex> l(mLock);
    if (mCallback) mCallback->notifyQueryPortStatus(kPortName, Status::SUCCESS, in_transactionId);
    return ScopedAStatus::ok();
}

ScopedAStatus Usb::switchRole(const std::string& in_portName, const PortRole& in_role,
                              int64_t in_transactionId) {
    std::lock_guard<std::mutex> l(mLock);
    if (mCallback) mCallback->notifyRoleSwitchStatus(in_portName, in_role, Status::NOT_SUPPORTED,
                                                     in_transactionId);
    return ScopedAStatus::ok();
}

ScopedAStatus Usb::enableContaminantPresenceDetection(const std::string& in_portName, bool,
                                                      int64_t in_transactionId) {
    std::lock_guard<std::mutex> l(mLock);
    if (mCallback) mCallback->notifyContaminantEnabledStatus(in_portName, false,
                                                             Status::NOT_SUPPORTED, in_transactionId);
    return ScopedAStatus::ok();
}

ScopedAStatus Usb::enableUsbData(const std::string& in_portName, bool in_enable,
                                 int64_t in_transactionId) {
    std::lock_guard<std::mutex> l(mLock);
    if (mCallback) mCallback->notifyEnableUsbDataStatus(in_portName, in_enable,
            in_enable ? Status::SUCCESS : Status::NOT_SUPPORTED, in_transactionId);
    return ScopedAStatus::ok();
}

ScopedAStatus Usb::enableUsbDataWhileDocked(const std::string& in_portName,
                                            int64_t in_transactionId) {
    std::lock_guard<std::mutex> l(mLock);
    if (mCallback) mCallback->notifyEnableUsbDataWhileDockedStatus(in_portName,
            Status::NOT_SUPPORTED, in_transactionId);
    return ScopedAStatus::ok();
}

ScopedAStatus Usb::limitPowerTransfer(const std::string& in_portName, bool,
                                      int64_t in_transactionId) {
    std::lock_guard<std::mutex> l(mLock);
    if (mCallback) mCallback->notifyLimitPowerTransferStatus(in_portName, false,
            Status::NOT_SUPPORTED, in_transactionId);
    return ScopedAStatus::ok();
}

ScopedAStatus Usb::resetUsbPort(const std::string& in_portName, int64_t in_transactionId) {
    std::lock_guard<std::mutex> l(mLock);
    if (mCallback) mCallback->notifyResetUsbPortStatus(in_portName, Status::NOT_SUPPORTED,
                                                       in_transactionId);
    return ScopedAStatus::ok();
}

}  // namespace usb
}  // namespace hardware
}  // namespace android
}  // namespace aidl
