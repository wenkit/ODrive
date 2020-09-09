#!/bin/bash
set -euo pipefail

# Prerequisites:
# Arch Linux:
#  gcc binutils
#  arm-linux-gnueabihf-gcc arm-linux-gnueabihf-binutils


mkdir -p third_party

function download_deb_pkg() {
    dir="$1"
    url="$2"
    file="$(sed 's|^.*/\([^/]*\)$|\1|' <<< "$url")"

    pushd third_party
    if ! [ -f "${file}" ]; then
        wget "${url}"
    fi
    if ! [ -d "${dir}/usr" ]; then
        ar x "${file}" "data.tar.xz"
        mkdir -p "${dir}"
        tar -xvf "data.tar.xz" -C "${dir}"
    fi
    popd
}

function compile_libusb() {
    arch="$1"
    libusb_version=1.0.23

    pushd third_party
    if ! [ -f "libusb-${libusb_version}.tar.bz2" ]; then
        wget "https://github.com/libusb/libusb/releases/download/v${libusb_version}/libusb-${libusb_version}.tar.bz2"
    fi
    if ! [ -d "libusb-${libusb_version}" ]; then
        tar -xvf "libusb-${libusb_version}.tar.bz2"
    fi

    mkdir -p "libusb-${libusb_version}/build-${arch}"
    pushd "libusb-${libusb_version}/build-${arch}"
    mkdir -p include
    #cp /usr/include/libudev.h include/
    unset LDFLAGS
    ../configure --prefix=/usr/${arch} \
                 --host=${arch} \
                 --disable-shared
    # They broke parallel building in libusb 1.20
    make -j1
    popd
    popd
}

download_deb_pkg libusb-dev-amd64 "http://mirrors.kernel.org/ubuntu/pool/main/libu/libusb-1.0/libusb-1.0-0-dev_1.0.23-2build1_amd64.deb"
download_deb_pkg libusb-amd64 "http://mirrors.kernel.org/ubuntu/pool/main/libu/libusb-1.0/libusb-1.0-0_1.0.23-2build1_amd64.deb"
download_deb_pkg libusb-i386 "http://mirrors.kernel.org/ubuntu/pool/main/libu/libusb-1.0/libusb-1.0-0_1.0.23-2build1_i386.deb"
download_deb_pkg libusb-dev-i386 "http://mirrors.kernel.org/ubuntu/pool/main/libu/libusb-1.0/libusb-1.0-0-dev_1.0.23-2build1_i386.deb"
download_deb_pkg libusb-armhf "http://mirrordirector.raspbian.org/raspbian/pool/main/libu/libusb-1.0/libusb-1.0-0_1.0.23-2_armhf.deb"
download_deb_pkg libusb-dev-armhf "http://mirrordirector.raspbian.org/raspbian/pool/main/libu/libusb-1.0/libusb-1.0-0-dev_1.0.23-2_armhf.deb"
download_deb_pkg libstdc++-linux-armhf "http://mirrors.kernel.org/ubuntu/pool/universe/g/gcc-10-cross/libstdc++-10-dev-armhf-cross_10-20200411-0ubuntu1cross1_all.deb"
#compile_libusb 'x86_64-apple-darwin' # fails with "sys/sysctl.h: No such file or directory"

_architectures=(
    #'arm-linux-gnueabihf'
    #'x86_64-apple-darwin'
    #'x86_64-pc-linux-gnu'
    #'i686-w64-mingw32'
    #'x86_64-w64-mingw32'
    )


FILES=('libfibre.cpp'
	   'platform_support/libusb_transport.cpp'
	   'legacy_protocol.cpp'
	   'legacy_object_client.cpp'
	   'logging.cpp')

arm-linux-gnueabihf-g++ -shared -o libfibre-linux-armhf.so -fPIC -std=c++17 -I./include -DFIBRE_COMPILE -DFIBRE_ENABLE_CLIENT \
        -I./third_party/libusb-dev-armhf/usr/include/libusb-1.0 \
        ./third_party/libusb-armhf/lib/arm-linux-gnueabihf/libusb-1.0.so.0.2.0 \
		"${FILES[@]}" \
        -lpthread \
        -L./third_party/libstdc++-linux-armhf/usr/lib/gcc-cross/arm-linux-gnueabihf/10 \
        -Wl,--unresolved-symbols=ignore-in-shared-libs -static-libstdc++

        
        
        # libudev.so.1.6.13 libpthread-2.28.so librt-2.28.so libc-2.28.so \
        
        # \
        #-static-libgcc -Wl,-Bstatic -lusb-1.0 -Wl,-Bdynamic
		
        #-lusb-1.0

#x86_64-pc-linux-gnu-g++ -shared -o libfibre-linux-amd64.so -fPIC -std=c++17 -I./include -DFIBRE_COMPILE -DFIBRE_ENABLE_CLIENT \
#        -I./third_party/libusb-armhf/usr/include/libusb-1.0 \
#        -L./third_party/libusb-armhf/usr/lib/x86_64-linux-gnu \
#		"${FILES[@]}" \
#		-lusb-1.0

