#
# Build helpers the open-source Qualcomm msm8960 HALs (hardware/qcom-caf/...) expect.
# LineageOS provides them in vendor/lineage; plain AOSP does not, so the device tree does.
# Included from BoardConfigCommon.mk.
#

# Source locations of the Qualcomm HAL families used by this platform
QCOM_HAL_PATH_qcom-display   := hardware/qcom-caf/msm8960/display
QCOM_HAL_PATH_qcom-media     := hardware/qcom-caf/msm8960/media
QCOM_HAL_PATH_qcom-audio     := hardware/qcom-caf/msm8960/audio
QCOM_HAL_PATH_qcom-wlan      := hardware/qcom-caf/wlan
QCOM_HAL_PATH_qcom-bt-vendor := hardware/qcom-caf/bt
QCOM_HAL_PATH_qcom-camera    := hardware/qcom/camera
QCOM_HAL_PATH_qcom-gps       := hardware/qcom/gps
QCOM_HAL_PATH_ril            := hardware/ril

# $(call project-path-for,<family>)
define project-path-for
$(QCOM_HAL_PATH_$(1))
endef

# $(call is-board-platform,<name>) / $(call is-board-platform-in-list,<names>)
define is-board-platform
$(if $(filter $(1),$(TARGET_BOARD_PLATFORM)),true)
endef
define is-board-platform-in-list
$(if $(filter $(1),$(TARGET_BOARD_PLATFORM)),true)
endef

# $(call is-vendor-board-platform,QCOM)
define is-vendor-board-platform
$(if $(and $(filter QCOM,$(1)),$(filter msm8960,$(TARGET_BOARD_PLATFORM))),true)
endef

# $(call is-platform-sdk-version-at-least,<n>)
define is-platform-sdk-version-at-least
$(if $(filter $(1),$(shell seq 1 $(PLATFORM_SDK_VERSION))),true)
endef
