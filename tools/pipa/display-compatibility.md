# Android display compatibility on the current pipa ROM

Status: the mainline Android userspace stack is configured; device display remains unverified. No physical display or GPU test has
been performed. This audit uses the actual ROM output and its source tree.

## Confirmed dependencies

| Layer | Current ROM evidence | Linux 6.18 base |
| --- | --- | --- |
| GPU | vendor/build.prop sets ro.hardware.egl=adreno; proprietary libgsl.so contains /dev/kgsl-3d0 and KGSL context, allocation, submission and fence ioctls | upstream DRM/MSM GPU submission ABI |
| Allocator/mapper | libgralloccore.so imports ion_open, ion_close and ion_alloc_fd; Qualcomm allocator and mapper are installed | DMA-BUF heaps; no stock ION userspace ABI |
| Composer | Qualcomm composer service and libdrmutils.so use downstream display interfaces; binary contains DRM_IOCTL_MSM_RMFB2 error path | upstream atomic DRM/KMS with different properties and ioctls |
| Panel/backlight | pipa DT describes NT36532 and KTZ8866; their drivers build | hardware support exists, Android integration unverified |

ROM output inspected: /home/aosp/infx/out/target/product/pipa/vendor.
Source product selection: device/xiaomi/sm8250-common/kona.mk, display section.
Actual display sources are hardware/qcom-caf/sm8250/display, not
hardware/qcom/sm8250/display. Relevant examples:

- gralloc/gr_ion_alloc.cpp calls ion_open and ion_alloc_fd.
- libdrmutils/drm_master.cpp uses DRM_IOCTL_MSM_RMFB2.
- sdm/libs/core/drm/hw_events_drm.cpp registers downstream MSM panel and
  idle/recovery events.
- sde-drm/drm_crtc.cpp consumes downstream capability, output-fence,
  performance, security and ROI properties.

The downstream msm_drm.h defines REGISTER_EVENT=0x41, DEREGISTER_EVENT=0x42,
RMFB2=0x43 and POWER_CTRL=0x44. This base's upstream msm_drm.h does not provide
those commands. Sharing the name MSM does not imply ABI compatibility.
Creating device-node aliases or no-op ioctl handlers would not implement
memory ownership, GPU execution, fence synchronization or display behavior.

## Integration routes

Keeping the current vendor image requires a substantial port of the legacy
KGSL, allocator and downstream display behavior, including their memory,
IOMMU, power-management and synchronization dependencies. Copying the 4.19
drivers into 6.18 is not sufficient. Each supported ioctl and property needs
defined behavior and validation against the existing Android libraries.

The alternative is a separate mainline Android ROM product using a compatible
Mesa Freedreno/Turnip GPU stack, MSM-capable allocator/mapper and upstream
atomic DRM composer. This changes userspace and cannot be delivered as a
kernel-only ZIP for the current ROM. Camera, video and other vendor clients
also need review for Qualcomm buffer-handle and protected-content dependencies.

The workspace contains external/mesa3d, external/minigbm and
external/drm_hwcomposer. Their presence does not establish a working product:
Mesa needs suitable Android driver build integration, allocator/mapper
implementations must agree on handles and metadata, and the local
drm_hwcomposer service lists sdm_backend as a conditional default. Its source
and dependencies are added only when DRMHWC.backend=sdm; the pipa mainline
configuration leaves that selector empty and forces the generic runtime backend. HAL manifests, init services,
SELinux, GPU device permissions and fence handling must match that product.

The current kernel configuration intentionally enables the existing mainline
display drivers. Release readiness still requires actual Android compositor,
GPU, brightness, refresh-rate, suspend/resume, touch and recovery testing on
pipa, together with storage and boot-image compatibility.

## Implemented ROM checkpoint

The opt-in pipa product now selects the 6.18 source, the reference external/mesa
Meson integration, freedreno EGL/Vulkan, minigbm MSM and generic DRM HWC3.
It filters common proprietary graphics service packages and EGL/Vulkan properties,
packages A650 firmware, installs ODM ueventd rules, and adds enforcing SELinux
labels/access for the selected graphics stack. HDR, wide-colour and protected
content capability claims are disabled. Required source revisions are pinned
in device/xiaomi/pipa/mainline/dependencies.xml.

Soong/Make generation passed and the full Infinity Android 17 user build is
compiling. A successful ROM build and real display/GPU validation are still
pending. These changes affect userspace and cannot make a kernel-only ZIP
compatible with the existing proprietary graphics stack.
