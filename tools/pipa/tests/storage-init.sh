#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
export PATH=/bin:/sbin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/mapper /mnt
[ -c /dev/mapper/control ] || mknod /dev/mapper/control c 10 236
exec > /dev/ttyS0 2>&1
fail()
{
    echo "PIPA_STORAGE_TEST_FAILED: $*"
    dmesg | tail -50
    poweroff -f
}
trap 'fail command-failed' EXIT
set -e
/bin/memfd-compat
# Exercise real USB enumeration through the dummy host/device controller.
mkdir -p /sys/kernel/config
mount -t configfs configfs /sys/kernel/config
gadget=/sys/kernel/config/usb_gadget/pipa-test
mkdir "$gadget"
echo 0x1d6b > "$gadget/idVendor"
echo 0x0104 > "$gadget/idProduct"
mkdir "$gadget/functions/acm.usb0" "$gadget/configs/c.1"
ln -s "$gadget/functions/acm.usb0" "$gadget/configs/c.1/acm.usb0"
echo dummy_udc.0 > "$gadget/UDC"
i=0
while [ "$(cat /sys/class/android_usb/android0/state)" != CONFIGURED ] && [ "$i" -lt 20 ]; do
    sleep 1
    i=$((i + 1))
done
[ "$(cat /sys/class/android_usb/android0/state)" = CONFIGURED ]
echo none > "$gadget/UDC"
i=0
while [ "$(cat /sys/class/android_usb/android0/state)" != DISCONNECTED ] && [ "$i" -lt 20 ]; do
    sleep 1
    i=$((i + 1))
done
[ "$(cat /sys/class/android_usb/android0/state)" = DISCONNECTED ]
rm "$gadget/configs/c.1/acm.usb0"
rmdir "$gadget/functions/acm.usb0" "$gadget/configs/c.1" "$gadget"
[ ! -e /sys/class/android_usb/android0 ]
mkdir "$gadget"
[ "$(cat /sys/class/android_usb/android0/state)" = DISCONNECTED ]
rmdir "$gadget"
echo "PASS Android USB configfs enumeration, disconnect and gadget recreation"
i=0
while [ ! -b /dev/vda ] && [ "$i" -lt 20 ]; do
    sleep 1
    i=$((i + 1))
done
[ -b /dev/vda ]
dmsetup version
dmsetup targets
key=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f
sectors=524288
open_mapping()
{
    dmsetup create metadata --table "0 $sectors default-key aes-xts-plain64 $key 0 /dev/vda 0 3 allow_discards sector_size:4096 iv_large_sectors"
    [ -b /dev/mapper/metadata ] || mknod /dev/mapper/metadata b 253 0
}
for fs in ext4 f2fs; do
    for mode in software inlinecrypt; do
        echo "CASE $fs $mode"
        open_mapping
        if [ "$fs" = ext4 ]; then
            mkfs.ext4 -F -O encrypt /dev/mapper/metadata
        else
            mkfs.f2fs -f -O encrypt /dev/mapper/metadata
        fi
        opts=rw
        [ "$mode" = inlinecrypt ] && opts=rw,inlinecrypt
        mount -t "$fs" -o "$opts" /dev/mapper/metadata /mnt
        /bin/fscrypt-roundtrip prepare /mnt
        sync
        umount /mnt
        dmsetup remove metadata
        open_mapping
        mount -t "$fs" -o "$opts" /dev/mapper/metadata /mnt
        /bin/fscrypt-roundtrip verify /mnt
        umount /mnt
        dmsetup remove metadata
        # The raw device must not contain a readable plaintext filesystem.
        if mount -t "$fs" -o ro /dev/vda /mnt; then
            umount /mnt
            fail 'raw filesystem unexpectedly readable'
        fi
        echo "PASS $fs $mode remount and metadata encryption"
    done
done
# Bad parameters must be rejected rather than creating a broken mapping.
for features in '2 sector_size:4096 iv_large_sectors_bad' '1 sector_size:4096' '1 sector_size:513' '1 invalid_option' '1 wrappedkey_v0'; do
    if dmsetup create invalid --table "0 $sectors default-key aes-xts-plain64 $key 0 /dev/vda 0 $features"; then
        dmsetup remove invalid
        fail "accepted unsupported parameters: $features"
    fi
done
echo "PASS invalid parameters and unsupported wrapped keys rejected"
trap - EXIT
echo PIPA_STORAGE_TEST_PASSED
poweroff -f
