include $(LOCAL_PATH)/tools/Makefile.sources
include $(LOCAL_PATH)/lib/Makefile.sources

LIBPCIACCESS_PATH := $(firstword $(wildcard  \
   $(TOP)/external/PRIVATE/libpciaccess      \
   $(TOP)/hardware/intel/libpciaccess        \
   $(TOP)/external/libpciaccess))
ifeq ($(LIBPCIACCESS_PATH),)
   $(error "Unable to find libpciaccess!")
endif

LIBDRM_PATH := $(firstword $(wildcard  \
   $(TOP)/external/PRIVATE/drm         \
   $(TOP)/external/drm))
ifeq ($(LIBDRM_PATH),)
   $(error "Unable to find libdrm!")
endif

skip_lib_list := \
    igt_kms.c \
    igt_kms.h

lib_list := $(filter-out $(skip_lib_list),$(libintel_tools_la_SOURCES))
LIB_SOURCES := $(addprefix lib/,$(lib_list))

#================#

define add_tool
    include $(CLEAR_VARS)

    LOCAL_SRC_FILES :=          \
       tools/$1.c               \
       $(LIB_SOURCES)

    LOCAL_C_INCLUDES +=              \
       $(LOCAL_PATH)/lib             \
       $(LIBDRM_PATH)/include/drm    \
       $(LIBDRM_PATH)/intel          \
       $(LIBPCIACCESS_PATH)/include

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

tools_list := $(filter-out $(skip_tools_list),$(bin_PROGRAMS) $(noinst_PROGRAMS))

$(foreach item,$(tools_list),$(eval $(call add_tool,$(item))))
