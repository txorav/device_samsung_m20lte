#
# Copyright (C) 2022 The OrangeFox Recovery Project
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

LOCAL_PATH := device/samsung/m20lte

# Fastbootd (disabled for physical eMMC partitions to save ~3.5MB ramdisk)
TW_INCLUDE_FASTBOOTD := false

PRODUCT_PROPERTY_OVERRIDES += \
	of_hide_data_hint=1 \
	ro.orangefox.version=R12.1 \
	ro.product.device=m20lte \
	ro.hardware=exynos7904 \
	ro.boot.hardware=exynos7904 \
	ro.boot.bootdevice=13500000.dwmmc0 \
	ro.bootdevice=13500000.dwmmc0


