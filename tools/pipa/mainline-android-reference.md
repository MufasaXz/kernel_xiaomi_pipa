# Mainline Android references applied to pipa

Reviewed 2026-10-08. These sources supply an Android userspace designed for
mainline drivers. Their docs are bring-up guidance, not evidence that pipa or
every hardware feature on their example devices works.

## Pinned sources

All GitHub repositories below belong to me-cafebabe-aosp-mainline and were
read at lineage-24.0:

| Repository | Revision | Relevant files |
| --- | --- | --- |
| android_device_mainline_common | 03b3b227cc79caa2a2942e30d33fd304d0a3c172 | docs/KERNEL{,_PATCHES}.md, BOOT_AND_PARTITIONS.md, BRINGUP_OVERVIEW.md, SEPOLICY.md; optional/{mesa,minigbm-upstream,drm_hwcomposer} |
| android_device_mainline_qcom-common | bde46ffbd6417872758e03f1ae9e8aa622122ce0 | docs/{GRAPHICS,LEGACY_DOWNSTREAM,QSEECOM,FIRMWARE,REMOTEPROCS,DSP_AND_SENSORS,ADDING_A_SOC,BOOTLOADERS}.md; optional/options.mk |
| android_hardware_mainline_common | 6fa9b36163f20488d120cbaad4ba0ae0857dd7e3 | docs/WIRING_A_HAL.md; interfaces/{audio,sensors,camera}/mainline/README.md |
| android_vendor_mainline | 25ce8806b99bcf3e0e8a9360a383e05bfb080772 | docs/REPOSITORIES.md |
| android_hardware_mainline_qcom | f209c1ac6bee4525857bdd2c7891ec6413ff2902 | libraries/libion_dmaheap, libraries/libsensors_libssc/README.md, hexagonrpc/README.md |
| android_kernel_mainline_configs | dc05b10df0b9efee05dc68f0d8166d4f2713e48f | fragments/common.config, init/use_memfd.rc |
| android_device_xiaomi_mi7150-mainline | 934da7e7d41d09a5234501e0ff4d19ae63faad63 | README.md, davinci_mainline/device.mk |

Repository URLs use https://github.com/me-cafebabe-aosp-mainline/ followed by
the repository name. Resolve /blob/<revision>/<path> for a pinned file.

## What transfers to pipa

| Area | Reference implementation | Pipa requirements / current limits |
| --- | --- | --- |
| Screen/GPU | Mesa freedreno GLES, freedreno/Turnip Vulkan, minigbm MSM, DRM hardware composer | Use the existing MSM DRM/A650/panel drivers. Replace the Qualcomm graphics packages, properties and VINTF fragments together; package CP/GMU and pipa zap firmware. Refresh rate, rotation, touch, suspend/resume and protected content need physical testing. |
| Shared memory | memfd + ashmem ioctl shim; sys.use_memfd init property | Shim integrated into this kernel; opt-in init file added under infx/device/xiaomi/pipa/mainline. Old blobs opening /dev/ashmem directly remain a separate issue. |
| USB | Android configfs state notifications | Ported from Android common-patches with cleanup/error-path fixes; VM exercises dummy-UDC enumeration and disconnect. Pipa PHY, connector role and Android gadget HAL remain untested. |
| Legacy ION | libion_dmaheap and blob import relinking | Reusable userspace adapter, not an ION kernel driver. Secure allocations explicitly fail; system allocations always select system and ignore cached/uncached selection. Does not solve KGSL, composer ioctls or protected display. |
| Audio | Mainline ALSA/UCM HAL + hexagonrpcd | Match pipa four-amplifier topology, ACDB and firmware. A generic sm8250 UCM profile is not proof it matches pipa. |
| Sensors | libssc/QMI/QRTR sensor backend | Pipa DTS enables a separate SLPI; inspect the SDSP service and SLPI firmware rather than blindly using the ADSP sensor services from davinci. |
| Camera | V4L2/libcamera mainline HAL | DTS includes the rear OV13B10/CAMSS path. That does not prove streaming/ISP integration, front camera or Android camera compatibility. CamX blobs cannot simply use V4L2. |
| Security | Downstream qseecomd + hardware-backed HAL option | Docs explicitly require a kernel CONFIG_QSEECOM driver and secure heaps. Upstream QCOM_QSEECOM alone does not provide the blob ioctl ABI. Software KeyMint/Gatekeeper defaults cannot unlock existing hardware-bound data or meet the security requirement. |
| Encryption | Device-specific kernel/userspace setup | Keep existing wrappedkey_v0 fscrypt + dm-default-key. Current experimental SCM/ICE work still requires actual Keymaster/vold and existing-data validation. No formatting or encryption downgrade. |
| Boot | Device-specific bootloader/image layout | Davinci uses U-Boot/GRUB and erase-dtbo instructions. These do not establish pipa's stock bootloader/DTBO handling and must not be copied. |

The checked qcom-common tree recognizes sm82* as pre-GKI but has neither an
SM8250 SoC directory nor automatic SM8250 family mapping. Add a real family
definition or an extension when integrating; selecting sm7150 is incorrect.

## Local Android source findings

The current ROM is Infinity Android 17 with a downstream 4.19 vendor stack.
external/minigbm and external/drm_hwcomposer already define MSM allocation and
an AIDL composer. The current external/mesa3d generated Soong integration does
not expose freedreno EGL/GLES or Turnip modules. The reference uses a different
external/mesa Meson integration; importing its switches without dependencies
would fail to build. Manifest/dependency integration is still required.

device/xiaomi/sm8250-common/kona.mk explicitly adds Qualcomm allocator, mapper
and composer packages. The common manifest includes vendor Keymaster,
Gatekeeper, audio, sensors and CamX camera providers. Its existing SELinux
configuration also ignores neverallows; a secure release requires auditing
policy and passing enforcement/neverallow checks, not copying permissive
bring-up defaults.

The pipa and sm8250-common repositories contained local Infinity changes
before this checkpoint. They are preserved and excluded from the new commit.
The new opt-in ROM checkpoint also selects Mesa/minigbm/generic DRM composer,
firmware, DRM permissions and enforcing graphics SELinux. Dependency revisions
are pinned in the pipa mainline directory; full ROM compilation is in progress.

## Kernel patch provenance and adaptations

Android kernel/common-patches revision:
95c6537fa2f92733e8b9fe23ab4ace2446070f41, android-mainline/:

- ANDROID-mm-memfd-ashmem-shim-Introduce-shim-layer.patch
- ANDROID-mm-shmem-Use-memfd-ashmem-shim-ioctl-handler.patch
- ANDROID-usb-gadget-configfs-Add-Uevent-to-notify-userspace.patch

Preserve the upstream author/copyright notices. The memfd port removes the
absent ASHMEM_C dependency and ASHMEM_RUST fallback, retaining explicit opt-in.
USB adaptation fixes a duplicate kfree in gadget creation, propagates creation
errors, avoids device teardown while holding a spinlock, guards scheduling for
inactive gadgets, fixes the disabled helper signature and only reports a
configured state after a successful nonzero configuration. New VM cases test
shared-memory write sealing and configfs gadget create/enumerate/drop/recreate.
They do not validate pipa USB or Android userspace behaviour on the tablet.

## Integration order

1. Resolve mainline userspace dependencies and SM8250 family support in an
   isolated Android product; keep current ROM products available.
2. Integrate Mesa/minigbm/DRM composer with pipa firmware and coherent boot
   inputs. Verify boot logs, first-stage mounts and actual display/GPU.
3. Complete QSEECom/secure heaps and test existing encrypted-data access using
   real hardware-backed credentials. VM software-key tests are insufficient.
4. Integrate DSP/audio/sensors, camera/codecs, Wi-Fi/Bluetooth, battery/thermal,
   suspend, pen and keyboard; verify SELinux enforcing and rollback.
5. Package a sideload installer only after device evidence meets the user's
   release requirement. No flashable artifact is part of this checkpoint.
