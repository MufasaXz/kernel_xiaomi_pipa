#!/bin/sh
# RAM-only diagnostics: never mount, format, or write any internal partition.
export PATH=/bin:/sbin:/system/bin
/bin/busybox --install -s /bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /dev/socket /dev/usb-ffs/adb /sys/kernel/config /sys/fs/pstore /tmp
chmod 0755 /dev /dev/usb-ffs /dev/usb-ffs/adb
mount -t devpts devpts /dev/pts
mount -t configfs configfs /sys/kernel/config
mount -t pstore pstore /sys/fs/pstore
echo 8 > /proc/sys/kernel/printk
echo "pipa 6.18.28 Android bring-up v0.1: RAM-only diagnostics" > /dev/kmsg
echo "Internal storage is not mounted." > /dev/kmsg
mkdir -p /tmp/logs
dmesg > /tmp/logs/early-dmesg.txt
cat /proc/cmdline > /tmp/logs/cmdline.txt
cat /proc/version > /tmp/logs/version.txt
cat /proc/partitions > /tmp/logs/partitions.txt

g=/sys/kernel/config/usb_gadget/pipa
mkdir -p "$g"
echo 0x18d1 > "$g/idVendor"
echo 0x4ee2 > "$g/idProduct"
echo 0x0200 > "$g/bcdUSB"
mkdir -p "$g/strings/0x409"
echo pipa-bringup > "$g/strings/0x409/serialnumber"
echo MufasaXz > "$g/strings/0x409/manufacturer"
echo 'Pipa 6.18 RAM diagnostics' > "$g/strings/0x409/product"
mkdir -p "$g/configs/c.1/strings/0x409"
echo 'ADB and serial diagnostics' > "$g/configs/c.1/strings/0x409/configuration"
echo 250 > "$g/configs/c.1/MaxPower"
mkdir -p "$g/functions/acm.usb0"
ln -s "$g/functions/acm.usb0" "$g/configs/c.1/acm.usb0"
mkdir -p "$g/functions/ffs.adb"
mount -t functionfs adb /dev/usb-ffs/adb
chmod 0777 /dev/usb-ffs/adb
chmod 0666 /dev/usb-ffs/adb/ep0
export ADB_EXTERNAL_STORAGE=/tmp
export LD_LIBRARY_PATH=/system/lib64
export ADB_TRACE=usb,auth,transport
/system/bin/adbd > /tmp/logs/adbd.txt 2>&1 &
# FunctionFS endpoints appear only after adbd writes its descriptors.
i=0
while [ "$i" -lt 10 ] && [ ! -e /dev/usb-ffs/adb/ep1 ]; do
    sleep 1
    i=$((i + 1))
done
if [ -e /dev/usb-ffs/adb/ep1 ]; then
    chmod 0666 /dev/usb-ffs/adb/ep*
    ln -s "$g/functions/ffs.adb" "$g/configs/c.1/ffs.adb"
fi
# Explicitly request peripheral role where the Type-C driver exports a switch.
for role in /sys/class/usb_role/*/role; do
    [ -e "$role" ] && echo device > "$role"
done
i=0
while [ "$i" -lt 30 ]; do
    for udc in /sys/class/udc/*; do
        [ -e "$udc" ] || continue
        echo "${udc##*/}" > "$g/UDC" && break
    done
    [ -s "$g/UDC" ] && break
    sleep 1
    i=$((i + 1))
done
dmesg > /tmp/logs/dmesg.txt
# USB ACM is a fallback if the Android recovery adbd cannot start standalone.
(
    while :; do
        if [ -c /dev/ttyGS0 ]; then
            /bin/setsid /bin/cttyhack /bin/sh -i < /dev/ttyGS0 > /dev/ttyGS0 2>&1
        fi
        sleep 1
    done
) &
echo "Diagnostic init reached; use USB ADB or serial console." > /dev/kmsg
while :; do
    sleep 60
done
