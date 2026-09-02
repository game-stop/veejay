#!/usr/bin/env bash
set -euo pipefail

if [[ "${ARCH_SUITE_SNAPSHOT:-}" != "$0" ]]; then
    snapshot="$(mktemp /tmp/arch-suite-builder.XXXXXX)"
    cp "$0" "$snapshot"
    chmod 755 "$snapshot"
    exec env ARCH_SUITE_SNAPSHOT="$snapshot" "$snapshot" "$@"
fi
trap 'rm -f -- "$ARCH_SUITE_SNAPSHOT"' EXIT

arch_target="${1:-generic}"
start_package="${2:-core}"
build_root="${ARCH_BUILD_ROOT:-/work}"
package_root="${ARCH_PACKAGE_ROOT:-/packages}"
include_puredata="${ARCH_INCLUDE_PUREDATA:-1}"
if [[ "$include_puredata" != 0 && "$include_puredata" != 1 ]]; then
    echo "ARCH_INCLUDE_PUREDATA must be 0 or 1" >&2
    exit 2
fi
suite_lock="${ARCH_SUITE_LOCK:-${package_root}.lock}"
command -v flock >/dev/null 2>&1 || {
    echo "flock is required to protect the shared Arch build root" >&2
    exit 127
}
mkdir -p "$(dirname -- "$suite_lock")"
exec 9>"$suite_lock"
if ! flock -n 9; then
    echo "Another Arch package suite is using $package_root" >&2
    exit 75
fi
if [[ "$start_package" == "core" ]]; then
    rm -rf "$build_root" "$package_root"
fi
mkdir -p "$build_root" "$package_root"

makepkg_config=/etc/makepkg.conf
makepkg_arch="$(sed -n -E 's/^(export[[:space:]]+)?CARCH="([^"]*)"/\2/p' "$makepkg_config")"

powerpc_cpu=""
powerpc_vector=""
case "${makepkg_arch}:${arch_target}" in
    powerpc:generic)
        powerpc_cpu=powerpc
        powerpc_vector=0
        ;;
    powerpc:7400)
        powerpc_cpu=7400
        powerpc_vector=1
        export QEMU_CPU=7400
        ;;
    powerpc64:generic)
        powerpc_cpu=powerpc64
        powerpc_vector=0
        ;;
    powerpc64:power7)
        powerpc_cpu=power7
        powerpc_vector=1
        export QEMU_CPU=power7
        ;;
esac

if [[ -n "$powerpc_cpu" ]]; then
    makepkg_config="$build_root/makepkg-${arch_target}.conf"
    cp /etc/makepkg.conf "$makepkg_config"
    if grep -q -E -- '-m(arch|cpu)=[^[:space:]\"]+' "$makepkg_config"; then
        sed -i -E '/^[[:space:]]*#/! s/-m(arch|cpu)=[^[:space:]\"]+/-mcpu='"$powerpc_cpu"'/g' \
            "$makepkg_config"
    else
        printf '\nCFLAGS+=" -mcpu=%s"\nCXXFLAGS+=" -mcpu=%s"\n' \
            "$powerpc_cpu" "$powerpc_cpu" >> "$makepkg_config"
    fi
    sed -i -E \
        -e '/^[[:space:]]*#/! s/-m(no-)?altivec//g' \
        -e '/^[[:space:]]*#/! s/-mabi=altivec//g' \
        "$makepkg_config"
    if [[ "$powerpc_vector" == 1 ]]; then
        printf '\nCFLAGS+=" -maltivec -mabi=altivec"\nCXXFLAGS+=" -maltivec -mabi=altivec"\n' \
            >> "$makepkg_config"
    else
        printf '\nCFLAGS+=" -mno-altivec"\nCXXFLAGS+=" -mno-altivec"\n' \
            >> "$makepkg_config"
    fi
    if ! bash -c '
        source "$1"
        [[ " $CFLAGS " == *" -mcpu=$2 "* &&
           " $CXXFLAGS " == *" -mcpu=$2 "* ]]
        macros="$(printf "" | $CC $CFLAGS -dM -E -)"
        if [[ "$3" == 1 ]]; then
            [[ " $CFLAGS " == *" -maltivec "* &&
               "$macros" == *"__ALTIVEC__"* ]]
        else
            [[ " $CFLAGS " == *" -mno-altivec "* &&
               "$macros" != *"__ALTIVEC__"* ]]
        fi
    ' _ "$makepkg_config" "$powerpc_cpu" "$powerpc_vector"; then
        echo "Unable to establish the ${arch_target} ${makepkg_arch} CPU/AltiVec requirement" >&2
        exit 1
    fi
fi

if [[ "$makepkg_arch" == "armv7h" &&
      ("$arch_target" == "generic" || "$arch_target" == "cortex-a7") ]]; then
    if [[ "$arch_target" == "generic" ]]; then
        arm_fpu=vfpv3-d16
    else
        arm_fpu=neon
    fi
    makepkg_config="$build_root/makepkg-${arch_target}.conf"
    cp /etc/makepkg.conf "$makepkg_config"
    if grep -q -E -- '-mfpu=[^[:space:]\"]+' "$makepkg_config"; then
        sed -i -E "s/-mfpu=[^[:space:]\"]+/-mfpu=${arm_fpu}/g" "$makepkg_config"
    else
        printf '\nCFLAGS+=" -mfpu=%s"\nCXXFLAGS+=" -mfpu=%s"\n' \
            "$arm_fpu" "$arm_fpu" >> "$makepkg_config"
    fi
    if [[ "$arch_target" == "cortex-a7" ]]; then
        if grep -q -E -- '-m(arch|cpu)=[^[:space:]\"]+' "$makepkg_config"; then
            sed -i -E '/^[[:space:]]*#/! s/-m(arch|cpu)=[^[:space:]\"]+/-mcpu=cortex-a7/g' \
                "$makepkg_config"
        else
            printf '\nCFLAGS+=" -mcpu=cortex-a7"\nCXXFLAGS+=" -mcpu=cortex-a7"\n' \
                >> "$makepkg_config"
        fi
    fi
    if ! bash -c '
        source "$1"
        [[ " $CFLAGS " == *" -mfpu=$2 "* &&
           " $CXXFLAGS " == *" -mfpu=$2 "* ]]
        if [[ "$3" == cortex-a7 ]]; then
            [[ " $CFLAGS " == *" -mcpu=cortex-a7 "* &&
               " $CXXFLAGS " == *" -mcpu=cortex-a7 "* &&
               " $CFLAGS " != *" -march="* &&
               " $CXXFLAGS " != *" -march="* ]]
        fi
    ' _ "$makepkg_config" "$arm_fpu" "$arch_target"; then
        echo "Unable to establish the ${arch_target} ARMv7 ${arm_fpu} FPU requirement" >&2
        exit 1
    fi
fi

if [[ "$makepkg_arch" == "powerpc64le" && "$arch_target" == "generic" ]]; then
    makepkg_config="$build_root/makepkg-generic.conf"
    cp /etc/makepkg.conf "$makepkg_config"
    if grep -q -E -- '-mcpu=[^[:space:]\"]+' "$makepkg_config"; then
        sed -i -E '/^[[:space:]]*#/! s/-mcpu=[^[:space:]\"]+/-mcpu=powerpc64/g' \
            "$makepkg_config"
    else
        printf '\nCFLAGS+=" -mcpu=powerpc64"\nCXXFLAGS+=" -mcpu=powerpc64"\n' \
            >> "$makepkg_config"
    fi
    if ! bash -c '
        source "$1"
        [[ " $CFLAGS " == *" -mcpu=powerpc64 "* &&
           " $CXXFLAGS " == *" -mcpu=powerpc64 "* &&
           " $CFLAGS " != *" -mcpu=power8 "* &&
           " $CXXFLAGS " != *" -mcpu=power8 "* ]]
    ' _ "$makepkg_config"; then
        echo "Unable to establish the generic scalar PPC64LE compiler target" >&2
        exit 1
    fi
fi

build_package() {
    local label="$1"
    local recipe="$2"
    local archive="$3"
    local work_dir="${build_root}/${label}"

    mkdir -p "$work_dir"
    cp "$recipe" "$archive" "$work_dir/"
    if grep -q '^_veejay_arch_target=' "$work_dir/PKGBUILD"; then
        sed -i "s/^_veejay_arch_target=.*/_veejay_arch_target='${arch_target}'/" "$work_dir/PKGBUILD"
    fi
    chown -R builder:builder "$work_dir"
    cd "$work_dir"
    runuser -u builder -- makepkg --config "$makepkg_config" --noconfirm --cleanbuild --clean
    find "$work_dir" -maxdepth 1 -type f -name '*.pkg.tar.*' ! -name '*.sig' -exec cp -f {} "$package_root/" \;
}

install_packages() {
    pacman -U --noconfirm "$@"
}

case "$start_package" in
    core)
        build_package core /src/veejay-core/PKGBUILD /src/veejay-core/veejaycore-1.6.0.tar.gz
        install_packages "$package_root"/veejay-core-*.pkg.tar.*
        ;&
    server)
        build_package server /src/veejay-server/PKGBUILD /src/veejay-server/veejay-1.6.0.tar.gz
        install_packages "$package_root"/veejay-1.6.0-*.pkg.tar.*
        ;&
    client)
        build_package client /src/veejay-client/PKGBUILD /src/veejay-client/reloaded-1.6.0.tar.gz
        ;&
    utils)
        build_package utils /src/veejay-utils/PKGBUILD /src/veejay-utils/veejay-utils-1.6.0.tar.gz
        ;&
    eidolon)
        build_package eidolon /src/veejay-eidolon/PKGBUILD /src/veejay-eidolon/veejay-eidolon-1.6.0.tar.gz
        ;&
    director)
        build_package director /src/veejay-director/PKGBUILD /src/veejay-director/reloaded-1.6.0.tar.gz
        ;&
    puredata)
        if [[ "$include_puredata" == 1 ]]; then
            build_package puredata /src/sendVIMS/PKGBUILD /src/veejay-puredata-1.6.0.tar.gz
        fi
        ;;
    *)
        echo "Unknown start package: $start_package" >&2
        exit 2
        ;;
esac
