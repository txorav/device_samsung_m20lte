#
# Copyright (C) 2022 The OrangeFox Recovery Project
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

FDEVICE="m20lte"

aera_get_target_device() {
local chkdev=$(echo "$BASH_SOURCE" | grep -w $FDEVICE)
   if [ -n "$chkdev" ]; then
      AERA_BUILD_DEVICE="$FDEVICE"
      FOX_BUILD_DEVICE="$FDEVICE"
   else
      chkdev=$(set | grep BASH_ARGV | grep -w $FDEVICE)
      if [ -n "$chkdev" ]; then
         AERA_BUILD_DEVICE="$FDEVICE"
         FOX_BUILD_DEVICE="$FDEVICE"
      fi
   fi
}

fox_get_target_device() {
   aera_get_target_device
}

if [ -z "$1" -a -z "$AERA_BUILD_DEVICE" -a -z "$FOX_BUILD_DEVICE" ]; then
   aera_get_target_device
fi

if [ "$1" = "$FDEVICE" -o "$AERA_BUILD_DEVICE" = "$FDEVICE" -o "$FOX_BUILD_DEVICE" = "$FDEVICE" ]; then

	# Base
	export ALLOW_MISSING_DEPENDENCIES=true
	export LC_ALL="C"
	export AERA_USE_TWRP_RECOVERY_IMAGE_BUILDER="1"
	export FOX_USE_TWRP_RECOVERY_IMAGE_BUILDER="1"

	# OrangeFox & AERA Build Identity
	unset FOX_VERSION
	export FOX_MAINTAINER_PATCH_VERSION="1"
	export OF_MAINTAINER="txorav"
	export FOX_BUILD_TYPE="Unofficial"
	export FOX_BUILD_DEVICE="m20lte"
	export AERA_PRODUCT_PREFIX=AERA
	export AERA_BUILD_STATUS=Unofficial
	export AERA_BUILD_TYPE=Beta
	export AERA_BUILD_DEVICE="m20lte"
	export AERA_MAINTAINER="${AERA_MAINTAINER:-txorav}"
	unset AERA_MAINTAINER_PATCH_VERSION

	# Partitions (m20lte specific)
	export AERA_RECOVERY_INSTALL_PARTITION="/dev/block/platform/13500000.dwmmc0/by-name/RECOVERY"
	export AERA_RECOVERY_SYSTEM_PARTITION="/dev/block/platform/13500000.dwmmc0/by-name/SYSTEM"
	export AERA_RECOVERY_VENDOR_PARTITION="/dev/block/platform/13500000.dwmmc0/by-name/VENDOR"
	export AERA_RECOVERY_BOOT_PARTITION="/dev/block/platform/13500000.dwmmc0/by-name/BOOT"
	export FOX_RECOVERY_INSTALL_PARTITION="/dev/block/platform/13500000.dwmmc0/by-name/RECOVERY"
	export FOX_RECOVERY_SYSTEM_PARTITION="/dev/block/platform/13500000.dwmmc0/by-name/SYSTEM"
	export FOX_RECOVERY_VENDOR_PARTITION="/dev/block/platform/13500000.dwmmc0/by-name/VENDOR"
	export FOX_RECOVERY_BOOT_PARTITION="/dev/block/platform/13500000.dwmmc0/by-name/BOOT"

	# Hardware features
	export AERA_FLASHLIGHT_ENABLE="1"
	export AERA_FL_PATH1="/system/flashlight"
	export OF_FLASHLIGHT_ENABLE="1"
	export OF_FL_PATH1="/system/flashlight"
	export OF_DEVICE_WITHOUT_PERSIST=1
	export AERA_SKIP_FBE_DECRYPTION=1
	export OF_SKIP_FBE_DECRYPTION=1
	export AERA_VANILLA_BUILD="1"
	export FOX_VANILLA_BUILD="1"
	export AERA_USE_LZMA_COMPRESSION="1"
	export OF_USE_LZMA_COMPRESSION="1"

	# Screen Settings (1080 x 2340 FHD+ Infinity-V display)
	export AERA_SCREEN_H=2340
	export AERA_STATUS_H=81
	export AERA_STATUS_INDENT_LEFT=55
	export AERA_STATUS_INDENT_RIGHT=55
	export AERA_CLOCK_POS=1
	export AERA_ALLOW_DISABLE_NAVBAR=0
	export OF_SCREEN_H=2340
	export OF_STATUS_H=81
	export OF_STATUS_INDENT_LEFT=55
	export OF_STATUS_INDENT_RIGHT=55
	export OF_CLOCK_POS=1
	export OF_ALLOW_DISABLE_NAVBAR=0

	# Quick Backup Defaults
	export AERA_QUICK_BACKUP_LIST="/boot;/data;/system_image;/vendor_image;"
	export OF_QUICK_BACKUP_LIST="/boot;/data;/system_image;/vendor_image;"

	# Common Architecture & Settings
	export TARGET_ARCH=arm64
	export TW_DEFAULT_LANGUAGE="en"
	export AERA_KEEP_FORCED_ENCRYPTION="1"
	export OF_KEEP_FORCED_ENCRYPTION="1"
	export AERA_USE_MAGISKBOOT="1"
	export OF_USE_MAGISKBOOT="1"
	export AERA_USE_MAGISKBOOT_FOR_ALL_PATCHES="1"
	export OF_USE_MAGISKBOOT_FOR_ALL_PATCHES="1"
	export AERA_DONT_PATCH_ENCRYPTED_DEVICE="1"
	export OF_DONT_PATCH_ENCRYPTED_DEVICE="1"
	export AERA_NO_TREBLE_COMPATIBILITY_CHECK="1"
	export OF_NO_TREBLE_COMPATIBILITY_CHECK="1"
	export AERA_ADVANCED_SECURITY="1"
	export OF_ADVANCED_SECURITY="1"

	export AERA_USE_BASH_SHELL="1"
	export FOX_USE_BASH_SHELL="1"
	export AERA_ASH_IS_BASH="1"
	export FOX_ASH_IS_BASH="1"
	export AERA_USE_TAR_BINARY="1"
	export FOX_USE_TAR_BINARY="1"
	export AERA_USE_ZIP_BINARY="1"
	export FOX_USE_ZIP_BINARY="1"
	export AERA_REPLACE_BUSYBOX_PS="1"
	export FOX_REPLACE_BUSYBOX_PS="1"
	export AERA_USE_NEW_MAGISKBOOT="1"
	export OF_USE_NEW_MAGISKBOOT="1"
	export AERA_SKIP_MULTIUSER_FOLDERS_BACKUP="1"
	export OF_SKIP_MULTIUSER_FOLDERS_BACKUP="1"

	# Run Post Format Process for MTP
	export AERA_RUN_POST_FORMAT_PROCESS=1
	export OF_RUN_POST_FORMAT_PROCESS=1

	# Samsung Odin settings
	export AERA_SAMSUNG_DEVICE=1
	export AERA_USE_SAMSUNG_SPECIAL=1
	export AERA_NO_SAMSUNG_SPECIAL=0
	export OF_NO_SAMSUNG_SPECIAL=0
	export BOARD_HAS_DOWNLOAD_MODE=true

	# Magisk setup
	function download_magisk(){
		local DEST=$1
		if [ -n "${DEST}" ]; then
			if [ ! -e ${DEST} ]; then
				echo "Downloading the Latest Release of Magisk..."
				local LATEST_MAGISK_URL=$(curl -sL https://api.github.com/repos/topjohnwu/Magisk/releases/latest | grep browser_download_url | grep Magisk- | cut -d : -f 2,3 | tr -d '"')
				mkdir -p $(dirname ${DEST})
				wget -q ${LATEST_MAGISK_URL} -O ${DEST} || wget ${LATEST_MAGISK_URL} -O ${DEST}
				local RCODE=$?
				if [ "$RCODE" = "0" ]; then
					echo "Successfully Downloaded Magisk to ${DEST}!"
				else
					echo "Failed to Download Magisk to ${DEST}!"
				fi
			fi
		fi
	}
	export FOX_USE_SPECIFIC_MAGISK_ZIP=~/Magisk/Magisk.zip
	download_magisk $FOX_USE_SPECIFIC_MAGISK_ZIP

	# Log Build VARs
	if [ -n "$FOX_BUILD_LOG_FILE" -a -f "$FOX_BUILD_LOG_FILE" ]; then
		export | grep "FOX" >> $FOX_BUILD_LOG_FILE
		export | grep "OF_" >> $FOX_BUILD_LOG_FILE
		export | grep "TARGET_" >> $FOX_BUILD_LOG_FILE
		export | grep "TW_" >> $FOX_BUILD_LOG_FILE
	elif [ -n "$AERA_BUILD_LOG_FILE" -a -f "$AERA_BUILD_LOG_FILE" ]; then
		export | grep "AERA" >> $AERA_BUILD_LOG_FILE
		export | grep "FOX" >> $AERA_BUILD_LOG_FILE
		export | grep "OF_" >> $AERA_BUILD_LOG_FILE
		export | grep "TARGET_" >> $AERA_BUILD_LOG_FILE
		export | grep "TW_" >> $AERA_BUILD_LOG_FILE
	fi
fi
