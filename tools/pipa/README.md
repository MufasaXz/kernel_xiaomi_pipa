# Pipa Android 17 / Linux 6.18.28 development

**NOT RELEASE READY. No sideload ZIP is published.**

Author: MufasaXz <sudeepyadav7272@gmail.com>
Branch: pipa-6.18.28-android17-v0.1
Version: 6.18.28-pipa-a17-v0.1

## Source and scope

Hardware base: https://github.com/pipa-mainline/linux/tree/pipa/6.18.28
Exact source revision: 3fd483240e935ee7055a93ec5681f3715f003784.
Preserve the existing authorship and licenses of those drivers. This source
snapshot imports that tree; it does not claim that Xiaomi's 4.19 drivers were
ported here from scratch. The import is based on the existing repository's
16 branch to allow publication without transferring unrelated upstream history.

Xiaomi reference: https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/pipa-t-oss
That branch is 4.19.157, not a ready-made 6.x SM8250 Android driver package.
Local Android reference: /home/aosp/infx/device/xiaomi/pipa and
/home/aosp/infx/device/xiaomi/sm8250-common, currently using 4.19.325.

This development branch adds Android core configuration and a reproducible
compile command to the existing pipa mainline hardware port. It is not a
completed downstream Qualcomm vendor ABI port or a certified GKI kernel.

## Build

Run: bash tools/pipa/build.sh

CLANG_BIN selects a directory containing a modern LLVM toolchain; OUT_DIR and
JOBS override output directory and parallelism. The default toolchain is the
existing local Android prebuilt clang-r584948b. Outputs stay in out-pipa;
the working 4.19 Android source and ROM output are not replaced.

The build validates essential builtin configuration and produces Image.gz,
sm8250-xiaomi-pipa.dtb and Image.gz-dtb. Existing 4.19 kernel modules cannot
be loaded into this kernel. Compilation does not prove any hardware works.

## Verified build results

- Linux 6.18.28-pipa-a17-v0.1 builds with the pipa DTB.
- Binder, binderfs, SELinux, seccomp, cgroups, filesystems and USB FunctionFS
  are enabled for Android userspace bring-up.
- UFS, MSM DRM, NT36532 panel, touch, mainline audio, Wi-Fi and Bluetooth
  drivers are builtin. Their Android compatibility remains untested.
- Android dm-default-key and its bio/fscrypt/F2FS hooks are ported. Disposable
  x86 VM tests pass for ext4 and F2FS with software and inline file encryption,
  including byte verification after removing/recreating the mapping. See
  [storage tests](tests/README.md) for provenance, reproduction and limits.
- No physical device boot, display, decryption, charging, suspend, camera,
  accessory or recovery testing has been performed.

## Known blockers for the current Android vendor image

1. Proprietary libgsl.so explicitly opens /dev/kgsl-3d0 and uses KGSL ioctls.
   This kernel implements upstream DRM/MSM, not KGSL. The display allocator
   still calls ion_open(), while this tree provides DMA-BUF heaps, not the
   stock ION ABI. The composer also expects downstream MSM DRM interfaces.
   Screen output from a Linux desktop does not establish Android UI support.
   See [display compatibility](display-compatibility.md) for local evidence.
2. The built Android vendor fstab uses dm-default-key and wrappedkey_v0 for
   /data. The dm-default-key target now works with software keys in a VM.
   An experimental SM8250 SCM/ICE wrapped-key path and legacy fscrypt flag
   compatibility now build, but firmware and existing-data decryption are
   unverified. See [storage compatibility](storage-compatibility.md).
   Existing encrypted data must remain intact; removing encryption or
   formatting data is not an acceptable compatibility fix.
3. libQSEEComAPI.so explicitly expects /dev/qseecom. Upstream QCOM_QSEECOM
   serves a different interface; enabling it does not satisfy those blobs.
4. libadsprpc.so expects /dev/adsprpc-smd[-secure]. Upstream FastRPC, remoteproc,
   firmware paths and audio topology need integration with the Android HALs.
5. CamX camera, video codecs, sensors, charging policy, thermal controls,
   keyboard and pen need vendor compatibility and device testing.
6. The bootloader consumes downstream DTBs/DTBOs. A mainline DTB cannot be
   combined blindly with the existing dtbo partition or vendor_boot DTB.

## Release requirement

The user explicitly requested no partial test release. Do not distribute a
flashable kernel or sideload ZIP from this branch until actual boot and hardware
tests demonstrate reliable behavior on the intended Android 17 ROM. A Linux
compile is not sufficient evidence. Zero hard-brick risk cannot be guaranteed.

The eventual filename should be 6.18.28-pipa.zip (or the final kernel version).
A recovery installer must validate pipa and boot format; preserve the original
boot, vendor_boot and dtbo; install coherent kernel/device-tree inputs; support
rollback; and avoid bootloader, firmware, GPT and userdata writes. Sideload
support itself needs verification in the intended custom recovery.

diagnostic-init.sh is source for a RAM-only diagnostic environment, not a
release or a tested image. It must not be used as evidence of Android boot.
No AnyKernel installer, ZIP or boot image is generated by this build script.
