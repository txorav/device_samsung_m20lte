#!/bin/bash
set -e

OUT_DIR="$1"
LABEL="${2:-Recovery}"
ZIP_NAME="${3:-Recovery-m20lte-AnyKernel3.zip}"

if [ -z "$OUT_DIR" ] || [ ! -f "$OUT_DIR/recovery.img" ]; then
    echo "Usage: $0 <out_dir> [label] [zip_name]"
    echo "Error: recovery.img not found in target dir: $OUT_DIR"
    exit 1
fi

AK3_DIR="/tmp/anykernel3"
rm -rf "$AK3_DIR"
echo "Cloning AnyKernel3 template..."
git clone --depth=1 https://github.com/osm0sis/AnyKernel3 "$AK3_DIR"
cp -f "$OUT_DIR/recovery.img" "$AK3_DIR/recovery.img"

cat << EOF > "$AK3_DIR/anykernel.sh"
### AnyKernel3 Recovery Installer Script
## Samsung Galaxy M20 (m20lte)
properties() { '
kernel.string=$LABEL for Samsung Galaxy M20
do.devicecheck=1
do.modules=0
do.systemless=1
do.cleanup=1
do.cleanuponabort=0
device.name1=m20lte
device.name2=m20
device.name3=SM-M205F
device.name4=SM-M205FN
device.name5=SM-M205G
supported.versions=
'; }

BLOCK=/dev/block/platform/13500000.dwmmc0/by-name/RECOVERY;
IS_SLOT_DEVICE=0;

. tools/ak3-core.sh;

ui_print " ";
ui_print "- Flashing $LABEL to physical partition...";
if [ -f recovery.img ]; then
  cat recovery.img > \$BLOCK;
  ui_print "- Recovery flashed successfully to \$BLOCK!";
else
  ui_print "! Error: recovery.img not found!";
  exit 1;
fi;
EOF

cd "$AK3_DIR"
zip -r9 -q "$OUT_DIR/$ZIP_NAME" * -x .git .github README.md
cd "$OUT_DIR"
md5sum "$ZIP_NAME" > "$ZIP_NAME.md5"
sha256sum "$ZIP_NAME" > "$ZIP_NAME.sha256"
echo "Successfully built AnyKernel3 package: $OUT_DIR/$ZIP_NAME"
