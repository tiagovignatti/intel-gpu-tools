LOCAL_PATH := $(call my-dir)

GPU_TOOLS_PATH := $(LOCAL_PATH)/..

.PHONY: version.h.tmp

$(GPU_TOOLS_PATH)/version.h.tmp:
	@touch $@
	@if test -d ../.git; then \
		if which git > /dev/null; then git log -n 1 --oneline | \
		        sed 's/^\([^ ]*\) .*/#define IGT_GIT_SHA1 "g\1"/' \
		        >> $@ ; \
		fi \
	else \
		echo '#define IGT_GIT_SHA1 "NOT-GIT"' >> $@ ; \
	fi

$(GPU_TOOLS_PATH)/version.h: $(GPU_TOOLS_PATH)/version.h.tmp
	@echo "updating version.h"
	@if ! cmp -s $(GPU_TOOLS_PATH)/version.h.tmp $(GPU_TOOLS_PATH)/version.h; then \
		mv $(GPU_TOOLS_PATH)/version.h.tmp $(GPU_TOOLS_PATH)/version.h ; \
	else \
		rm $(GPU_TOOLS_PATH)/version.h.tmp ; \
	fi

# FIXME: autogenerate this info #
$(GPU_TOOLS_PATH)/config.h:
	@echo "updating config.h"
	@echo '#define PACKAGE_VERSION "1.5"' >> $@ ; \
	echo '#define TARGET_CPU_PLATFORM "android-ia"' >> $@ ;

include $(LOCAL_PATH)/Makefile.sources

skip_lib_list := \
    igt_kms.c \
    igt_kms.h

lib_list := $(filter-out $(skip_lib_list),$(libintel_tools_la_SOURCES))

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(lib_list)

LOCAL_GENERATED_SOURCES :=       \
	$(GPU_TOOLS_PATH)/version.h  \
	$(GPU_TOOLS_PATH)/config.h

LOCAL_C_INCLUDES +=              \
	$(LOCAL_PATH)/..

LOCAL_EXPORT_C_INCLUDE_DIRS += $(LOCAL_PATH)

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES
LOCAL_CFLAGS += -DHAVE_STRUCT_SYSINFO_TOTALRAM
LOCAL_CFLAGS += -DANDROID
LOCAL_CFLAGS += -std=c99
LOCAL_MODULE:= libintel_gpu_tools

LOCAL_SHARED_LIBRARIES := libpciaccess  \
			  libdrm        \
			  libdrm_intel

include $(BUILD_STATIC_LIBRARY)

