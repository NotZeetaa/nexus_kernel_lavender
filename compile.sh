#!/bin/bash
export KBUILD_BUILD_USER=Zeetaa
export KBUILD_BUILD_HOST=ZeetaaPrjkt
# Compile plox
function compile() {
    make -j$(nproc) O=out ARCH=arm64 lavender-perf_defconfig cc=" ccache clang-12"
    make -j$(nproc) O=out ARCH=arm64  cc="ccache clang-12" \
                               CROSS_COMPILE=aarch64-linux-gnu- \
                               CROSS_COMPILE_ARM32=arm-linux-gnueabi-
}
compile
