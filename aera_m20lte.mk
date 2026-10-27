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

# AERA product settings for Samsung Galaxy M20 (m20lte)
AERA_MAINTAINER := txorav
OF_MAINTAINER := txorav

# Display properties (1080 x 2340 FHD+ Infinity-V display)
AERA_SCREEN_H := 2340
AERA_STATUS_H := 81
AERA_STATUS_INDENT_LEFT := 55
AERA_STATUS_INDENT_RIGHT := 55
AERA_CLOCK_POS := 1
AERA_ALLOW_DISABLE_NAVBAR := 0
TARGET_SCREEN_WIDTH := 1080
TARGET_SCREEN_HEIGHT := 2340

# Compatibility mirror
OF_SCREEN_H := 2340
OF_STATUS_H := 81
OF_STATUS_INDENT_LEFT := 55
OF_STATUS_INDENT_RIGHT := 55
OF_CLOCK_POS := 1
OF_ALLOW_DISABLE_NAVBAR := 0

# Samsung specific flags
AERA_SAMSUNG_DEVICE := 1
AERA_USE_SAMSUNG_SPECIAL := 1
AERA_NO_SAMSUNG_SPECIAL := 0
OF_NO_SAMSUNG_SPECIAL := 0
BOARD_HAS_DOWNLOAD_MODE := true
AERA_HAS_DOWNLOAD_MODE := 1
TW_HAS_DOWNLOAD_MODE := true

# Compression and packaging
AERA_VANILLA_BUILD := 1
FOX_VANILLA_BUILD := 1
AERA_USE_LZMA_COMPRESSION := 1
OF_USE_LZMA_COMPRESSION := 1

# Backup defaults
AERA_QUICK_BACKUP_LIST := /boot;/data;/system_image;/vendor_image;
OF_QUICK_BACKUP_LIST := /boot;/data;/system_image;/vendor_image;

# Patching & Magisk
AERA_USE_MAGISKBOOT := 1
OF_USE_MAGISKBOOT := 1
AERA_USE_MAGISKBOOT_FOR_ALL_PATCHES := 1
OF_USE_MAGISKBOOT_FOR_ALL_PATCHES := 1
AERA_DONT_PATCH_ENCRYPTED_DEVICE := 1
OF_DONT_PATCH_ENCRYPTED_DEVICE := 1
AERA_NO_TREBLE_COMPATIBILITY_CHECK := 1
OF_NO_TREBLE_COMPATIBILITY_CHECK := 1
AERA_USE_BASH_SHELL := 1
FOX_USE_BASH_SHELL := 1
AERA_ASH_IS_BASH := 1
FOX_ASH_IS_BASH := 1
AERA_USE_TAR_BINARY := 1
FOX_USE_TAR_BINARY := 1
AERA_USE_ZIP_BINARY := 1
FOX_USE_ZIP_BINARY := 1
AERA_REPLACE_BUSYBOX_PS := 1
FOX_REPLACE_BUSYBOX_PS := 1
AERA_USE_NEW_MAGISKBOOT := 1
OF_USE_NEW_MAGISKBOOT := 1
AERA_SKIP_MULTIUSER_FOLDERS_BACKUP := 1
OF_SKIP_MULTIUSER_FOLDERS_BACKUP := 1
AERA_RUN_POST_FORMAT_PROCESS := 1
OF_RUN_POST_FORMAT_PROCESS := 1
