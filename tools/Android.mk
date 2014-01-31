LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/Makefile.sources

#================#

define add_tool
    include $(CLEAR_VARS)

    LOCAL_SRC_FILES := $1.c

    LOCAL_CFLAGS += -DHAVE_TERMIOS_H
    LOCAL_CFLAGS += -DHAVE_STRUCT_SYSINFO_TOTALRAM
    LOCAL_CFLAGS += -DANDROID -UNDEBUG
    LOCAL_CFLAGS += -std=c99
    # FIXME: drop once Bionic correctly annotates "noreturn" on pthread_exit
    LOCAL_CFLAGS += -Wno-error=return-type
    # Excessive complaining for established cases. Rely on the Linux version warnings.
    LOCAL_CFLAGS += -Wno-sign-compare

    LOCAL_MODULE := $1
    LOCAL_MODULE_TAGS := optional

    LOCAL_STATIC_LIBRARIES := libintel_gpu_tools

    LOCAL_SHARED_LIBRARIES := libpciaccess  \
                              libdrm        \
                              libdrm_intel

    include $(BUILD_EXECUTABLE)
endef

#================#

skip_tools_list := \
    intel_framebuffer_dump \
    intel_reg_dumper \
    intel_vga_read \
    intel_vga_write

tools_list := $(filter-out $(skip_tools_list),$(bin_PROGRAMS))

$(foreach item,$(tools_list),$(eval $(call add_tool,$(item))))
