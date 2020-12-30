#!/bin/bash

# colorize and add text parameters
grn=$(tput setaf 2)             # green
yellow=$(tput setaf 3)          # yellow
bldgrn=${txtbld}$(tput setaf 2) # bold green
red=$(tput setaf 1)             # red
txtbld=$(tput bold)             # bold
bldblu=${txtbld}$(tput setaf 4) # bold blue
blu=$(tput setaf 4)             # blue
txtrst=$(tput sgr0)             # reset
blink=$(tput blink)             # blink

export KBUILD_BUILD_USER=Zeetaa
export KBUILD_BUILD_HOST=ZeetaaPrjkt
DATE_START=$(date +"%s")
# Compile plox
function compile() {
    make -j$(nproc) O=out ARCH=arm64 lavender-perf_defconfig cc=" ccache clang-12"
    make -j$(nproc) O=out ARCH=arm64  cc="ccache clang-12" \
                               CROSS_COMPILE=aarch64-linux-gnu- \
                               CROSS_COMPILE_ARM32=arm-linux-gnueabi-
}
compile

echo -e "${bldgrn}"
echo "-------------------"
echo "Build Complet in:"
echo "-------------------"
echo -e "${txtrst}"

DATE_END=$(date +"%s")
DIFF=$(($DATE_END - $DATE_START))
echo "Time: $(($DIFF / 60)) minutes(i) and $(($DIFF % 60)) seconds."
echo
echo " Congrats NotZeetaa Ur Build Finished🔥 "
