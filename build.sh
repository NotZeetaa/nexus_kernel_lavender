#!/usr/bin/env bash
DEFCONFIG=lavender-perf_defconfig
IMAGE=$(pwd)/out/arch/arm64/boot/Image.gz-dtb
echo "Cloning dependencies"
git clone --depth=1 --single-branch https://github.com/kdrag0n/proton-clang clang
git clone --depth=1 --single-branch https://github.com/Prashant-1695/Flashable_Zip Anykernel
echo "Done"
Camera=OldCam
TANGGAL=$(date +"%F-%S")
START=$(date +"%s")
KERNEL_DIR=$(pwd)
PATH="${PWD}/clang/bin:$PATH"
export KBUILD_COMPILER_STRING=$(${KERNEL_DIR}/clang/bin/clang --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g' -e 's/[[:space:]]*$//')
export ARCH=arm64
export SUBARCH=arm64
export KBUILD_BUILD_HOST=droneci
export KBUILD_BUILD_USER="prashant"
############
function sticker() {
    curl -s -X POST "https://api.telegram.org/bot$token/sendSticker" \
        -d sticker="CAACAgUAAx0CVxrmOQABAuqhYHYLgi-2cn9jpggMD8VYBIEzQWgAAsQBAALU8vhUZa6bA1OeOtoeBA" \
        -d chat_id=$chat_id
}
############
function sendinfo() {
    curl -s -X POST "https://api.telegram.org/bot$token/sendMessage" \
        -d chat_id="$chat_id" \
        -d "disable_web_page_preview=true" \
        -d "parse_mode=html" \
        -d text="<b>• Mercenary Kernel •</b>%0ABuild started on <code>Drone CI</code>%0AFor device <b>Xiaomi Redmi Note 7</b> (lavender)%0Abranch <code>$(git rev-parse --abbrev-ref HEAD)</code>(master)%0AUnder commit <code>$(git log --pretty=format:'"%h : %s"' -1)</code>%0AUsing compiler: <code>${KBUILD_COMPILER_STRING}</code>%0AStarted on <code>$(date)</code>%0A<b>Build Status:</b>#BETA"
}
############
function push() {
    cd anykernel
    ZIP=$(echo *.zip)
    curl -F document=@$ZIP "https://api.telegram.org/bot$token/sendDocument" \
        -F chat_id="$chat_id" \
        -F "disable_web_page_preview=true" \
        -F "parse_mode=html" \
        -F caption="Build took $(($DIFF / 60)) minute(s) and $(($DIFF % 60)) second(s). | For <b>Xiaomi Redmi Note 7 (lavender)</b> | <b>$(${GCC}gcc --version | head -n 1 | perl -pe 's/\(http.*?\)//gs' | sed -e 's/  */ /g')</b>"
}
############
function error() {
    curl -s -X POST "https://api.telegram.org/bot$token/sendMessage" \
        -d chat_id="$chat_id" \
        -d "disable_web_page_preview=true" \
        -d "parse_mode=markdown" \
        -d text="Build throw an error(s)"
    exit 1
}
############
function compile() {
    make O=out ARCH=arm64 $DEFCONFIG
    make -j$(nproc --all) O=out \
        ARCH=arm64 \
        CC=clang \
        LD=ld.lld \
        AR=llvm-ar \
        NM=llvm-nm \
        OBJCOPY=llvm-objcopy \
        OBJDUMP=llvm-objdump \
        STRIP=llvm-strip \
        CROSS_COMPILE=aarch64-linux-gnu- \
        CROSS_COMPILE_ARM32=arm-linux-gnueabi- Image.gz-dtb
}
############
function zipping() {
    if [ -f "$IMAGE" ]; then
        cp $IMAGE anykernel
        cd anykernel || exit 1
        zip -r9 neXus-EAS-v7.1-lavender-${Camera}-${TANGGAL}.zip.zip *
        cd ..
    else
        error && exit 1
    fi
}
sticker
sendinfo
compile
zipping
END=$(date +"%s")
DIFF=$(($END - $START))
push
