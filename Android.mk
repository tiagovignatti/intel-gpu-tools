LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/intel_reg_write.c         	\
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h         	\
       tools/intel_reg.h               	\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
	lib/intel_reg_map.c 		\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=            	       			\
       $(LOCAL_PATH)/lib				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/ 

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID

LOCAL_MODULE := intel_reg_write
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/intel_reg_read.c          	\
       lib/intel_pci.c			\
       lib/intel_gpu_tools.h         	\
       tools/intel_reg.h               	\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
	lib/intel_reg_map.c 		\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=					\
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_reg_read
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=				\
       tools/intel_disable_clock_gating.c	\
       lib/intel_pci.c 				\
       lib/intel_gpu_tools.h         		\
       tools/intel_reg.h               		\
       lib/intel_batchbuffer.h       		\
       lib/intel_batchbuffer.c       		\
       lib/intel_mmio.c       			\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=            			        \
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_disable_clock_gating
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=			\
       tools/intel_audio_dump.c         \
       lib/intel_pci.c			\
       lib/intel_gpu_tools.h         	\
       tools/intel_reg.h		\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=                    				\
       $(LOCAL_PATH)/lib 					\
       $(TOPDIR)hardware/intel/libdrm/include/drm 		\
       $(TOPDIR)hardware/intel/libdrm/intel 			\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_audio_dump
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/intel_backlight.c          \
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h         	\
       tools/intel_reg.h               	\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=                    			\
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_backlight
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/intel_bios_dumper.c       	\
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h         	\
       tools/intel_reg.h               	\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=                    			\
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_bios_dumper
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/intel_bios_reader.c        \
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h         	\
       tools/intel_reg.h               	\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=            			        \
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_bios_reader
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
# Disabling intel_error_decode tool, since Android still does not have libdrm2.4.30
#================
#include $(CLEAR_VARS)
#
#LOCAL_SRC_FILES :=                     	\
#       tools/intel_error_decode.c      	\
#       lib/intel_pci.c 			\
#       lib/intel_gpu_tools.h         	\
#       tools/intel_reg.h               	\
#       lib/intel_batchbuffer.h       	\
#       lib/intel_batchbuffer.c       	\
#       lib/intel_mmio.c       		\
#       tools/intel_chipset.h 		\
#       lib/instdone.h  			\
#       lib/instdone.c  			\
#       tools/intel_decode.h  		\
#	lib/intel_drm.c
#       
#
#LOCAL_C_INCLUDES +=            			        \
#       $(LOCAL_PATH)/lib 				\
#       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
#       $(TOPDIR)hardware/intel/libdrm/intel 		\
#       $(LOCAL_PATH)/../libpciaccess/include/
#
#LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
#LOCAL_CFLAGS += -DANDROID
#LOCAL_CFLAGS += -std=c99
#
#
#LOCAL_MODULE := intel_error_decode
#LOCAL_MODULE_TAGS := optional
#
#LOCAL_SHARED_LIBRARIES := libpciaccess 	\
#                          libdrm 	\
#                          libdrm_intel
#
#include $(BUILD_EXECUTABLE)
#
#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/intel_gpu_top.c          	\
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h         	\
       tools/intel_reg.h               	\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h 		\
       lib/instdone.h  			\
       lib/instdone.c  			\
	lib/intel_reg_map.c
       

LOCAL_C_INCLUDES +=    			                \
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_gpu_top
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/intel_gpu_time.c          	\
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h         	\
       tools/intel_reg.h               	\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=            			        \
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_gpu_time
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/intel_gtt.c          	\
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h         	\
       tools/intel_reg.h               	\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=            			        \
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1

LOCAL_MODULE := intel_gtt
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/intel_stepping.c          	\
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h         	\
       tools/intel_reg.h               	\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=            			        \
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_stepping
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=			\
       tools/intel_reg_dumper.c         \
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h 	        \
       tools/intel_reg.h       	        \
       lib/intel_batchbuffer.h		\
       lib/intel_batchbuffer.c		\
       lib/intel_mmio.c      		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=                    			\
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_reg_dumper
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/intel_reg_snapshot.c       \
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h 	        \
       tools/intel_reg.h       	        \
       lib/intel_batchbuffer.h		\
       lib/intel_batchbuffer.c		\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h
       

LOCAL_C_INCLUDES +=            			        \
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1

LOCAL_MODULE := intel_reg_snapshot
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       tools/forcewaked.c          	\
       lib/intel_pci.c 			\
       lib/intel_gpu_tools.h		\
       tools/intel_reg.h               	\
       lib/intel_batchbuffer.h       	\
       lib/intel_batchbuffer.c       	\
       lib/intel_mmio.c       		\
       tools/intel_chipset.h 		\
       lib/intel_reg_map.c		\
       lib/intel_drm.c
       

LOCAL_C_INCLUDES +=            			        \
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := forcewaked
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	\
                          libdrm 	\
                          libdrm_intel

include $(BUILD_EXECUTABLE)

#================
include $(CLEAR_VARS)

LOCAL_SRC_FILES :=                     	\
       lib/intel_gpu_tools.h		\
       tools/intel_reg_checker.c	\
	lib/intel_pci.c			\
	lib/intel_mmio.c
       

LOCAL_C_INCLUDES +=            			        \
       $(LOCAL_PATH)/lib 				\
       $(TOPDIR)hardware/intel/libdrm/include/drm 	\
       $(TOPDIR)hardware/intel/libdrm/intel 		\
       $(LOCAL_PATH)/../libpciaccess/include/

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1
LOCAL_CFLAGS += -DANDROID


LOCAL_MODULE := intel_reg_checker
LOCAL_MODULE_TAGS := optional

LOCAL_SHARED_LIBRARIES := libpciaccess 	

include $(BUILD_EXECUTABLE)

