#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${VEEJAY_SOURCE_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)}"
ARCH_TARGET="${1:-generic}"
START_PROJECT="${2:-core}"
VERSION="$("${ROOT_DIR}/packaging/ci/release-version.sh" check)"
RELEASE_TARGET="${RELEASE_TARGET:-amd64-${ARCH_TARGET}}"
PACKAGE_VARIANT="${PACKAGE_VARIANT:-standard}"

case "$PACKAGE_VARIANT" in
    standard) SUITE_TARGET="$RELEASE_TARGET" ;;
    nvjpeg) SUITE_TARGET="${RELEASE_TARGET}-nvjpeg" ;;
    *)
        printf "ERROR: unknown package variant '%s'\n" "$PACKAGE_VARIANT" >&2
        exit 2
        ;;
esac

BUILD_ROOT="${ARCH_BUILD_ROOT:-${ROOT_DIR}/packaging-build/${SUITE_TARGET}/arch-suite}"
PACKAGE_ROOT="${ARCH_PACKAGE_ROOT:-${ROOT_DIR}/release-packages/${SUITE_TARGET}/arch}"
INCLUDE_PUREDATA="${ARCH_INCLUDE_PUREDATA:-1}"
MAKEPKG_CONFIG="${MAKEPKG_CONFIG:-/etc/makepkg.conf}"

[[ "$INCLUDE_PUREDATA" == 0 || "$INCLUDE_PUREDATA" == 1 ]] || {
    printf 'ERROR: ARCH_INCLUDE_PUREDATA must be 0 or 1\n' >&2
    exit 2
}

for command_name in bsdtar cc makepkg pacman runuser; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf "ERROR: required command '%s' was not found\n" "$command_name" >&2
        exit 127
    }
done

if ! id builder >/dev/null 2>&1; then
    [[ $EUID -eq 0 ]] || {
        printf "ERROR: build user 'builder' does not exist\n" >&2
        exit 1
    }
    useradd --create-home builder
fi

if [[ $EUID -eq 0 ]]; then
    command -v sudo >/dev/null 2>&1 || {
        printf 'ERROR: sudo is required by makepkg --syncdeps\n' >&2
        exit 127
    }
    mkdir -p /etc/sudoers.d
    printf 'builder ALL=(ALL:ALL) NOPASSWD: /usr/bin/pacman\n' > /etc/sudoers.d/veejay-builder
    chmod 440 /etc/sudoers.d/veejay-builder
fi

"${ROOT_DIR}/packaging/ci/check-native-target.sh" "$RELEASE_TARGET" "$ARCH_TARGET"
"${ROOT_DIR}/packaging/ci/prepare-source-archives.sh"

if [[ "$START_PROJECT" == core ]]; then
    rm -rf -- "$BUILD_ROOT" "$PACKAGE_ROOT"
fi
mkdir -p "$BUILD_ROOT" "$PACKAGE_ROOT"

# Represent the read-only CUDA toolkit mounted by CI without adding the
# temporary dependency provider to the release assets.
install_cuda_provider() {
    if pacman -T cuda >/dev/null 2>&1; then
        return
    fi

    local provider_root="${BUILD_ROOT}/cuda-build-provider"
    local cuda_version="${CUDA_SERIES:-13-3}"
    local provider_package=''

    cuda_version="${cuda_version//-/.}"
    [[ "$cuda_version" =~ ^[0-9]+([.][0-9]+)*$ ]] || {
        printf "ERROR: CUDA_SERIES produced invalid Arch version '%s'\n" "$cuda_version" >&2
        exit 2
    }
    printf 'Installing CI-only Arch CUDA provider (cuda=%s)\n' "$cuda_version"

    rm -rf -- "$provider_root"
    mkdir -p "$provider_root"
    printf '%s\n' \
        'pkgname=veejay-cuda-build-provider' \
        "pkgver=${cuda_version}" \
        'pkgrel=1' \
        'pkgdesc="Temporary provider for the CUDA toolkit mounted by VeeJay CI"' \
        "arch=('any')" \
        "license=('custom')" \
        'provides=("cuda=${pkgver}")' \
        "conflicts=('cuda')" \
        "options=('!strip')" \
        'package() {' \
        '    install -D -m 0644 /dev/null "$pkgdir/usr/share/veejay/cuda-build-provider"' \
        '}' > "${provider_root}/PKGBUILD"
    chown -R builder:builder "$provider_root"

    (
        cd "$provider_root"
        runuser -u builder -- makepkg \
            --config "$MAKEPKG_CONFIG" \
            --noconfirm --cleanbuild
    )

    provider_package="$(find "$provider_root" -maxdepth 1 -type f \
        -name 'veejay-cuda-build-provider-*.pkg.tar.*' ! -name '*.sig' -print -quit)"
    [[ -n "$provider_package" ]] || {
        printf 'ERROR: temporary CUDA provider package was not created\n' >&2
        exit 1
    }
    pacman -U --needed --noconfirm "$provider_package"
    pacman -T cuda >/dev/null || {
        printf 'ERROR: temporary CUDA provider did not satisfy the cuda dependency\n' >&2
        exit 1
    }
}

build_project() {
    local label="$1"
    local project="$2"
    local archive_name="$3"
    local work_dir="${BUILD_ROOT}/${label}"
    local -a cuda_environment=()

    if [[ "$PACKAGE_VARIANT" == nvjpeg ]]; then
        : "${CUDA_HOME:?CUDA_HOME must identify the mounted CUDA toolkit}"
        [[ -x "${NVCC:-${CUDA_HOME}/bin/nvcc}" ]] || {
            printf "ERROR: nvcc was not found below CUDA_HOME '%s'\n" "$CUDA_HOME" >&2
            exit 1
        }
        cuda_environment=(
            env
            "CUDA_HOME=${CUDA_HOME}"
            "CUDA_PATH=${CUDA_HOME}"
            "NVCC=${NVCC:-${CUDA_HOME}/bin/nvcc}"
            "NVCC_PREPEND_FLAGS=-allow-unsupported-compiler"
            "LD_LIBRARY_PATH=${CUDA_LIBRARY_PATH:-${CUDA_HOME}/lib64}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
        )
        if [[ "$project" == veejay-server ]]; then
            printf 'Arch nvJPEG: enabling nvcc compatibility override for the rolling host compiler\n'
        fi
    fi

    rm -rf -- "$work_dir"
    mkdir -p "$work_dir"
    cp "${ROOT_DIR}/${project}/PKGBUILD" "$work_dir/"
    cp "${ROOT_DIR}/${project}/${archive_name}" "$work_dir/"
    sed -i "s/^pkgver=.*/pkgver=${VERSION}/" "$work_dir/PKGBUILD"
    if grep -q '^_veejay_arch_target=' "$work_dir/PKGBUILD"; then
        sed -i "s/^_veejay_arch_target=.*/_veejay_arch_target=${ARCH_TARGET}/" "$work_dir/PKGBUILD"
    fi
    if grep -q '^_veejay_nvjpeg=' "$work_dir/PKGBUILD"; then
        sed -i "s/^_veejay_nvjpeg=.*/_veejay_nvjpeg=$([[ "$PACKAGE_VARIANT" == nvjpeg ]] && printf yes || printf no)/" \
            "$work_dir/PKGBUILD"
    fi
    chown -R builder:builder "$work_dir"

    (
        cd "$work_dir"
        runuser -u builder -- "${cuda_environment[@]}" makepkg \
            --config "$MAKEPKG_CONFIG" \
            --syncdeps --needed --noconfirm --cleanbuild
    )

    if [[ "$project" == veejay-server ]]; then
        "${ROOT_DIR}/packaging/ci/verify-nvjpeg-config.sh" \
            "$PACKAGE_VARIANT" "${work_dir}/src" | \
            tee "${PACKAGE_ROOT}/NVJPEG-CONFIG-VERIFICATION.txt"
    fi

    while IFS= read -r -d '' package; do
        cp -f -- "$package" "$PACKAGE_ROOT/"
    done < <(find "$work_dir" -maxdepth 1 -type f -name '*.pkg.tar.*' ! -name '*.sig' -print0)

    rm -rf -- "$work_dir"
}

install_package() {
    local prefix="$1"
    local -a matches=()

    mapfile -d '' -t matches < <(
        find "$PACKAGE_ROOT" -maxdepth 1 -type f \
            -name "${prefix}-${VERSION}-*.pkg.tar.*" ! -name '*.sig' -print0
    )
    ((${#matches[@]} > 0)) || {
        printf "ERROR: built Arch package '%s' was not found\n" "$prefix" >&2
        exit 1
    }
    pacman -U --needed --noconfirm "${matches[@]}"
}

if [[ "$PACKAGE_VARIANT" == nvjpeg ]]; then
    install_cuda_provider
fi

case "$START_PROJECT" in
    core)
        build_project core veejay-core "veejaycore-${VERSION}.tar.gz"
        install_package veejay-core
        ;&
    server)
        build_project server veejay-server "veejay-${VERSION}.tar.gz"
        install_package veejay
        ;&
    client)
        build_project client veejay-client "reloaded-${VERSION}.tar.gz"
        ;&
    utils)
        build_project utils veejay-utils "veejay-utils-${VERSION}.tar.gz"
        ;&
    eidolon)
        build_project eidolon veejay-eidolon "veejay-eidolon-${VERSION}.tar.gz"
        ;&
    director)
        build_project director veejay-director "reloaded-${VERSION}.tar.gz"
        ;&
    puredata)
        if [[ "$INCLUDE_PUREDATA" == 1 ]]; then
            build_project puredata sendVIMS "veejay-puredata-${VERSION}.tar.gz"
        fi
        ;;
    *)
        printf "ERROR: unknown start project '%s'\n" "$START_PROJECT" >&2
        exit 2
        ;;
esac

if [[ "$INCLUDE_PUREDATA" == 0 ]]; then
    printf 'ERROR: the standard Arch release profile requires veejay-puredata\n' >&2
    exit 1
fi

PACKAGE_BUILD_ENVIRONMENT="${PACKAGE_BUILD_ENVIRONMENT:-archlinux}" \
    "${ROOT_DIR}/packaging/ci/verify-packages.sh" \
    arch "$RELEASE_TARGET" "$ARCH_TARGET" "$PACKAGE_VARIANT" "$PACKAGE_ROOT"
