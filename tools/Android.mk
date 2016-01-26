LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/Makefile.sources

LOCAL_TOOLS_DIR := intel/validation/core/igt/tools

#================#

define add_tool
    include $(CLEAR_VARS)

    ifeq ($($(1)_SOURCES),)
        LOCAL_SRC_FILES := $1.c
    else
        LOCAL_SRC_FILES := $($(1)_SOURCES)
    endif

    LOCAL_CFLAGS += -DHAVE_TERMIOS_H
    LOCAL_CFLAGS += -DHAVE_STRUCT_SYSINFO_TOTALRAM
    LOCAL_CFLAGS += -DANDROID -UNDEBUG
    LOCAL_CFLAGS += -std=gnu99
    # FIXME: drop once Bionic correctly annotates "noreturn" on pthread_exit
    LOCAL_CFLAGS += -Wno-error=return-type
    # Excessive complaining for established cases. Rely on the Linux version warnings.
    LOCAL_CFLAGS += -Wno-sign-compare
    ifeq ($($(1)_LDFLAGS),)
    else
        LOCAL_LDFLAGS += $($(1)_LDFLAGS)
    endif

    LOCAL_C_INCLUDES = $(LOCAL_PATH)/../lib
    LOCAL_C_INCLUDES += ${ANDROID_BUILD_TOP}/external/PRIVATE/drm/include/drm
    LOCAL_C_INCLUDES += ${ANDROID_BUILD_TOP}/external/zlib

    LOCAL_MODULE := $1_tool
    LOCAL_MODULE_TAGS := optional

    LOCAL_STATIC_LIBRARIES := libintel_gpu_tools

    LOCAL_SHARED_LIBRARIES := libpciaccess  \
                              libdrm        \
                              libdrm_intel

    # Tools dir on host
    LOCAL_MODULE_PATH := $(TARGET_OUT_VENDOR)/$(LOCAL_TOOLS_DIR)
    # Tools dir on target.
    LOCAL_CFLAGS += -DPKGDATADIR=\"/system/vendor/$(LOCAL_TOOLS_DIR)\"

    include $(BUILD_EXECUTABLE)
endef

#================#

# Copy the register files
$(shell mkdir -p $(TARGET_OUT_VENDOR)/$(LOCAL_TOOLS_DIR)/registers)
$(shell cp $(LOCAL_PATH)/registers/* $(TARGET_OUT_VENDOR)/$(LOCAL_TOOLS_DIR)/registers)


skip_tools_list := \
    intel_framebuffer_dump \
    intel_reg_dumper \
    intel_vga_read \
    intel_vga_write

ifneq ("${ANDROID_HAS_CAIRO}", "1")
    skip_tools_list += intel_display_crc
    skip_tools_list += intel_residency
endif

tools_list := $(filter-out $(skip_tools_list),$(bin_PROGRAMS))

$(foreach item,$(tools_list),$(eval $(call add_tool,$(item))))
