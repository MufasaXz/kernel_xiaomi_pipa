# Disposable storage regression tests

These tests exercise the Android dm-default-key port, not the pipa hardware.
No physical storage or Android userdata is attached to the guest. Each run
creates a fresh temporary 256 MiB virtual disk and disables guest networking.
All keys and file contents are public, deterministic test data.

## Source provenance

The dm-default-key target and bio/fscrypt/F2FS integration follow Android
common android17-6.18 revision
9d29ba85d8a2c505b2d049568c3b1d29c4e0c2fd:
https://android.googlesource.com/kernel/common/+/9d29ba85d8a2c505b2d049568c3b1d29c4e0c2fd/

The target preserves Google's copyright and original author declarations.
The adaptation omits BLK_FEAT_ORDERED_HWQ from its I/O hints because that
feature does not exist in this Linux 6.18.28 base. Core crypto symbol exports
allow the target to be built as a module as well as builtin.

## Run

On an x86_64 Linux host, install QEMU system x86, BusyBox, dmsetup, e2fsprogs,
f2fs-tools, cpio, GCC with static libc, Python 3, and the kernel LLVM build
dependencies. No root access is needed for the test itself.

Run from the source root:

```sh
CLANG_BIN=/path/to/llvm/bin bash tools/pipa/tests/run-storage-vm.sh
```

OUT_DIR, RESULTS_DIR and JOBS override build output, results and parallelism.
The default output is out-storage-vm; use a dedicated output directory because
the runner resets its kernel configuration. It checks every requested config
value after Kconfig resolves dependencies, builds an x86 kernel, and saves the
resolved config, kernel hash and full guest log in the results directory.

To run an already-built test kernel:

```sh
python3 tools/pipa/tests/storage-vm.py --kernel /path/to/bzImage --results /path/to/results
```

## Coverage and observed results

On 2026-10-08, the ARM64 pipa Image.gz/DTB build and separate x86
DM_DEFAULT_KEY=m kernel/module link both passed using Android Clang 22.0.1
(clang-r584948b). VM runtime results:

| Filesystem | File encryption | Result |
| --- | --- | --- |
| ext4 | software fscrypt | PASS |
| ext4 | inlinecrypt, software blk-crypto fallback | PASS |
| F2FS | software fscrypt | PASS |
| F2FS | inlinecrypt, software blk-crypto fallback | PASS |

Each case writes sixteen encrypted 1 MiB files and one plain 1 MiB file,
fsyncs, unmounts, removes and recreates the metadata encryption mapping,
remounts, reinstalls the file key, and checks all file bytes. The underlying
encrypted virtual disk must not mount as a readable filesystem. Invalid
options, invalid sector sizes, missing iv_large_sectors and hardware-wrapped
keys on unsupported storage must be rejected. Success requires guest marker
PIPA_STORAGE_TEST_PASSED and a successful QEMU exit.

This does not validate pipa ICE/TrustZone, existing wrapped keys, Android vold,
power-loss recovery, garbage-collection stress or device boot. The module
configuration was link-tested; runtime cases use the builtin target.
