#!/bin/bash

echo "███████╗███████╗███████╗████████╗ █████╗  █████╗ "
echo "╚══███╔╝██╔════╝██╔════╝╚══██╔══╝██╔══██╗██╔══██╗"
echo "  ███╔╝ █████╗  █████╗     ██║   ███████║███████║"
echo " ███╔╝  ██╔══╝  ██╔══╝     ██║   ██╔══██║██╔══██║"
echo "███████╗███████╗███████╗   ██║   ██║  ██║██║  ██║"
echo "╚══════╝╚══════╝╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝"

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

rm -f /data/data/com.termux/files/home/ubuntu-in-termux/ubuntu-fs/root/nexus/out/arch/arm64/boot/Image.gz-dtb*

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


   cd /data/data/com.termux/files/home/ubuntu-in-termux/ubuntu-fs/root/flzip/
   rm -f /data/data/com.termux/files/home/ubuntu-in-termux/ubuntu-fs/root/flzip/*.zip;
   rm -f /data/data/com.termux/files/home/ubuntu-in-termux/ubuntu-fs/root/flzip/Image.gz-dtb*
   cp -af /data/data/com.termux/files/home/ubuntu-in-termux/ubuntu-fs/root/nexus/out/arch/arm64/boot/Image.gz-dtb /data/data/com.termux/files/home/ubuntu-in-termux/ubuntu-fs/root/flzip/
   
   zip -r9 Nexus-Eas-lavender-old-V1.zip * -x .git README.md *placeholder

   echo "-------------------"
   echo "Build Complet in:"
   echo "-------------------"
   echo -e "${txtrst}"


   DATE_END=$(date +"%s")
   DIFF=$(($DATE_END - $DATE_START))
   echo "Time: $(($DIFF / 60)) minutes(i) and $(($DIFF % 60)) seconds."
   cd /data/data/com.termux/files/home/ubuntu-in-termux/ubuntu-fs/root/nexus/




echo "███████╗███████╗███████╗████████╗ █████╗  █████╗ "
echo "╚══███╔╝██╔════╝██╔════╝╚══██╔══╝██╔══██╗██╔══██╗"
echo "  ███╔╝ █████╗  █████╗     ██║   ███████║███████║"
echo " ███╔╝  ██╔══╝  ██╔══╝     ██║   ██╔══██║██╔══██║"
echo "███████╗███████╗███████╗   ██║   ██║  ██║██║  ██║"
echo "╚══════╝╚══════╝╚══════╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝"
cd ..
cd ..
cd ..
cd ..
cd ..
cd ..
cd ..
cd ..
cd ..
