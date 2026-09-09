LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := modhub_aml
LOCAL_SRC_FILES := main.cpp gma_parser.cpp gma_loader.cpp mod/logger.cpp mod/config.cpp
LOCAL_CFLAGS    += -O2 -DNDEBUG -std=c++17
LOCAL_C_INCLUDES += $(LOCAL_PATH)
LOCAL_LDLIBS    += -llog
include $(BUILD_SHARED_LIBRARY)
