LOCAL_PATH := $(call my-dir)

#================#

include $(CLEAR_VARS)

# This demo wont build on android
#LOCAL_SRC_FILES := intel_sprite_on.c


LOCAL_CFLAGS += -DHAVE_TERMIOS_H
LOCAL_CFLAGS += -DANDROID -UNDEBUG
LOCAL_CFLAGS += -std=gnu99
# Excessive complaining for established cases. Rely on the Linux version warnings.
LOCAL_CFLAGS += -Wno-sign-compare

LOCAL_C_INCLUDES = $(LOCAL_PATH)/../lib

LOCAL_MODULE := intel_sprite_on

LOCAL_MODULE_TAGS := optional

LOCAL_STATIC_LIBRARIES := libintel_gpu_tools

LOCAL_SHARED_LIBRARIES := libdrm

include $(BUILD_EXECUTABLE)

#================#
