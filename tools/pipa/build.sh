#!/usr/bin/env bash
set -euo pipefail
src=$(cd -- "$(dirname -- "$0")/../.." && pwd)
out=$(realpath -m -- "${OUT_DIR:-$src/out-pipa}")
clang_bin=${CLANG_BIN:-/home/aosp/infx/prebuilts/clang/host/linux-x86/clang-r584948b/bin}
export PATH="$clang_bin:$PATH"
export ARCH=arm64
export KBUILD_BUILD_USER=MufasaXz
export KBUILD_BUILD_HOST=pipa-android17
export LOCALVERSION=
mkdir -p "$out"
cd "$src"
make O="$out" LLVM=1 defconfig
scripts/kconfig/merge_config.sh -m -Q -O "$out" "$out/.config" arch/arm64/configs/sm8250.config arch/arm64/configs/pipa_android17.config
make O="$out" LLVM=1 olddefconfig
python3 tools/pipa/check-config.py "$out/.config"
make O="$out" LLVM=1 -j"${JOBS:-$(nproc)}" Image.gz qcom/sm8250-xiaomi-pipa.dtb
cat "$out/arch/arm64/boot/Image.gz" "$out/arch/arm64/boot/dts/qcom/sm8250-xiaomi-pipa.dtb" > "$out/Image.gz-dtb"
make -s O="$out" LLVM=1 kernelrelease
