#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail
src=$(cd -- "$(dirname -- "$0")/../../.." && pwd)
out=$(realpath -m -- "${OUT_DIR:-$src/out-storage-vm}")
results=$(realpath -m -- "${RESULTS_DIR:-$out/results}")
clang_bin=${CLANG_BIN:-/home/aosp/infx/prebuilts/clang/host/linux-x86/clang-r584948b/bin}
export PATH="$clang_bin:$PATH"
export ARCH=x86_64
export LOCALVERSION=
mkdir -p "$out" "$results"
cd "$src"
make O="$out" LLVM=1 allnoconfig
scripts/kconfig/merge_config.sh -m -Q -O "$out" "$out/.config" tools/pipa/tests/storage-vm.config
make O="$out" LLVM=1 olddefconfig
python3 - "$out/.config" tools/pipa/tests/storage-vm.config <<'PY'
import pathlib
import sys

actual = set(pathlib.Path(sys.argv[1]).read_text().splitlines())
required = [line for line in pathlib.Path(sys.argv[2]).read_text().splitlines()
            if line.startswith("CONFIG_") or line.startswith("# CONFIG_")]
missing = [line for line in required if line not in actual]
if missing:
    sys.exit("Storage VM configuration mismatch: " + ", ".join(missing))
PY
make O="$out" LLVM=1 -j"${JOBS:-$(nproc)}" bzImage
cp "$out/.config" "$results/kernel.config"
sha256sum "$out/arch/x86/boot/bzImage" > "$results/kernel.sha256"
python3 tools/pipa/tests/storage-vm.py --kernel "$out/arch/x86/boot/bzImage" --results "$results"
