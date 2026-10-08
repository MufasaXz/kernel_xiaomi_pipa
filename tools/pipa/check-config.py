#!/usr/bin/env python3
"""Fail when first-stage boot or Android core dependencies are missing."""
import pathlib
import sys

config = dict(line.split("=", 1) for line in pathlib.Path(sys.argv[1]).read_text().splitlines()
              if line.startswith("CONFIG_") and "=" in line)
fragment = pathlib.Path(__file__).resolve().parents[2] / "arch/arm64/configs/pipa_android17.config"
requested = dict(line.split("=", 1) for line in fragment.read_text().splitlines()
                 if line.startswith("CONFIG_") and "=" in line)
changed = [key + ": requested " + value + ", got " + config.get(key, "absent")
           for key, value in requested.items() if config.get(key) != value]
if changed:
    sys.exit("Android fragment requests were not satisfied:\n" + "\n".join(changed))
required = """
ARM64_4K_PAGES COMPAT BLK_DEV_INITRD RD_GZIP BOOT_CONFIG
ANDROID_BINDER_IPC ANDROID_BINDERFS SECURITY_SELINUX SECCOMP SECCOMP_FILTER
CGROUPS MEMCG CPUSETS BPF_SYSCALL PSI TMPFS DEVTMPFS
BLK_DEV_DM DM_DEFAULT_KEY DM_VERITY DM_SNAPSHOT EXT4_FS F2FS_FS FS_ENCRYPTION
SCSI_UFS_QCOM PHY_QCOM_QMP_UFS PINCTRL_SM8250 SM_GCC_8250
DRM_MSM DRM_PANEL_NOVATEK_NT36532 TOUCHSCREEN_NT36523_SPI
USB_DWC3 USB_CONFIGFS USB_CONFIGFS_F_FS PSTORE_RAM
""".split()
missing = ["CONFIG_" + key for key in required if config.get("CONFIG_" + key) != "y"]
if missing:
    sys.exit("Required builtin configuration missing: " + ", ".join(missing))
print("Android bring-up configuration checks passed")
print("Vendor ABI still unresolved: KGSL, ION, /dev/qseecom, SM8250 wrappedkey_v0")
