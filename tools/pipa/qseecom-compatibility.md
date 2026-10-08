# Pipa legacy QSEECom development transport

This is an experimental source checkpoint, not verified hardware-backed
security. CONFIG_QSEECOM and CONFIG_DMABUF_HEAPS_PIPA_QSEECOM are disabled
in the normal Android fragment. The pipa QSEECom device and its two CMA pools
also have status disabled. Compiling the driver does not enable it.

The imported driver and ABI headers come from
https://github.com/me-cafebabe-aosp-mainline/linux at
adf47b31aa7f23c62e5065297fc1f15ade635b09 (mi89x7/7.1.3). That port targets
an older Qualcomm platform. It is not a tested SM8250 implementation.

## Adaptations in this tree

- Existing mainline SCM transport executes legacy requests through a kernel
  internal bridge, never a userspace arbitrary-SMC interface.
- Internal requests, firmware and scatter tables use coherent qcom_tzmem
  with SHM-bridge registration and DMA ordering barriers. Memory is wiped
  on release; downstream cache instructions on coherent aliases are removed.
- Imported DMA buffers must be physically contiguous, page aligned, below
  4 GiB and have an identity physical DMA mapping. Incompatible buffers fail.
  SHM registrations are shared by DMA-buffer identity and reference counted;
  overlapping regions from independent exporters and reference overflow fail.
- Listener lookup returns NULL for absent entries, including an empty list.
  Deregistration errors retain mappings that secure world may still reference.
  Successful deregistration waits for receiver ioctls without holding the
  listener mutex, avoiding cleanup races/deadlocks.
- Modified-FD imports stay alive across secure execution. Listener imports
  persist until the next request or deregistration; client imports are freed
  on command completion/error under the existing execution mutex.
- DMA-buffer cache operations propagate errors with matching directions.
- Firmware headers and allocation lengths are checked before access and
  alignment. Probe is restricted to xiaomi,pipa.
- The optional CMA exporter allowlists only qseecom and qseecom-ta regions.
  These are CPU-accessible communication buffers, not protected video/display
  heaps. They do not provide protected-content security.
- Unimplemented legacy ICE setup/explicit key eviction returns EOPNOTSUPP;
  it does not pretend the requested operation succeeded.

The DT describes stock pipa sizes: 20 MiB QSEE communication, 16 MiB TA staging,
4 MiB alignment, allocations below 4 GiB. The secure-app address describes an
already reserved region, not newly allocated Linux memory. UEFI owns initial
secure-app loading. The appsbl-qseecom-support flag suppresses a new secure-app
region notification.

## Validation and remaining work

A full ARM64 Image.gz and pipa DTB build passed with QSEECOM and its CMA
exporter enabled. DT nodes remained disabled. No secure monitor, listener,
RPMB, Keymaster, Gatekeeper or existing-userdata test ran on pipa.

The ROM has an opt-in libion DMA-heap implementation derived from the
developer's android_hardware_mainline_qcom revision
f209c1ac6bee4525857bdd2c7891ec6413ff2902. TARGET_PIPA_MAINLINE_QSEECOM selects
it. Secure/content-protection requests fail rather than falling back to normal
memory. Actual heap allocation requires enabled DT pools, driver config,
ueventd permissions and SELinux. These pieces must be brought up together. The adapter also compiles as an
Android AArch64 object against this ROM's Bionic and liblog headers with
-Wall -Wextra -Werror (unused API parameters explicitly exempted).

Remaining concerns include listener lifetime/concurrency review of the
inherited driver, overlapping aliases from independent exporters, internal
low-address allocation reliability, RPMB daemon compatibility, trusted-app
architecture/version checks and secure key eviction. The driver has no
32-bit compatibility ioctl layer. Current pipa security clients are 64-bit;
other legacy clients cannot be assumed compatible.

Do not change wrappedkey_v0 flags or format userdata to bypass a failed
security service. Disposable storage VM results exercise software paths;
they do not prove hardware-bound keys or existing pipa ciphertext unlocks.
