#
# Copyright (C) 2024-2026 The AERA Recovery Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# AERA Maintainer
AERA_MAINTAINER := txorav

# Display & UI Settings (Galaxy M20 resolution: 1080x2340)
AERA_SCREEN_H := 2340
AERA_SCREEN_W := 1080
AERA_STATUS_H := 100
AERA_HIDE_NOTCH := 1
AERA_CLOCK_POS := 1
AERA_STATUS_INDENT_LEFT := 48
AERA_STATUS_INDENT_RIGHT := 48
AERA_OPTIONS_LIST_NUM := 6
AERA_USE_GREEN_LED := 0
AERA_USE_LOCKSCREEN_BUTTON := 1

# Partition, Storage & Tooling (Galaxy M20 has physical non-dynamic partitions)
AERA_FORCE_DATA_FORMAT_F2FS := 1
AERA_UNBIND_SDCARD_F2FS := 1
AERA_WIPE_METADATA_AFTER_DATAFORMAT := 1
AERA_DISPLAY_FORMAT_FILESYSTEMS_DEBUG_INFO := 1
AERA_QUICK_BACKUP_LIST := /boot;/data;

# Kernel & Decryption Handling
AERA_FORCE_PREBUILT_KERNEL := 1
AERA_NO_RELOAD_AFTER_DECRYPTION := 1
AERA_NO_TREBLE_COMPATIBILITY_CHECK := 1
AERA_ENABLE_FRP_WIPE := 1
AERA_ENABLE_ROOT_MANAGER := 1

# Network & PC Connection over WLAN (disabled to prevent partition overflow on 38MB recovery partition)
AERA_ENABLE_WLAN := 0
