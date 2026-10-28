LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := foxd
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/system/bin

LOCAL_SRC_FILES := \
    src/main.cpp \
    src/jsonrpc.cpp \
    src/wifi_engine.cpp \
    src/scrcpy_server.cpp \
    src/container_engine.cpp \
    src/root_suite.cpp \
    src/app_manager.cpp \
    src/m20lte_hal.cpp

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include

LOCAL_CFLAGS := -Wall -Wextra -Wno-unused-parameter
LOCAL_CPPFLAGS := -std=c++17 -Wall -Wextra -Wno-unused-parameter
LOCAL_SHARED_LIBRARIES := libcutils liblog

include $(BUILD_EXECUTABLE)
