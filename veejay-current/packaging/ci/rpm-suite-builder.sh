#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${VEEJAY_SOURCE_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)}"
ARCH_TARGET="${1:-generic}"
START_PROJECT="${2:-core}"
VERSION="$("${ROOT_DIR}/packaging/ci/release-version.sh" check)"

case "$(uname -m)" in
    x86_64) default_release_target="amd64-${ARCH_TARGET}" ;;
    aarch64) default_release_target="arm64-${ARCH_TARGET}" ;;
    *) default_release_target="$(uname -m)-${ARCH_TARGET}" ;;
esac

RELEASE_TARGET="${RELEASE_TARGET:-$default_release_target}"
PACKAGE_VARIANT="${PACKAGE_VARIANT:-standard}"

case "$PACKAGE_VARIANT" in
    standard) SUITE_TARGET="$RELEASE_TARGET" ;;
    nvjpeg) SUITE_TARGET="${RELEASE_TARGET}-nvjpeg" ;;
    *)
        printf "ERROR: unknown package variant '%s'\n" "$PACKAGE_VARIANT" >&2
        exit 2
        ;;
esac

BUILD_ROOT="${RPM_BUILD_ROOT:-${ROOT_DIR}/packaging-build/${SUITE_TARGET}/rpm-suite}"
PACKAGE_ROOT="${RPM_PACKAGE_ROOT:-${ROOT_DIR}/release-packages/${SUITE_TARGET}/rpm}"
BUILD_JOBS="${RPM_BUILD_NCPUS:-$(nproc)}"

if [[ $EUID -eq 0 ]]; then
    SUDO=()
else
    command -v sudo >/dev/null 2>&1 || {
        printf 'ERROR: root privileges are required to install locally built RPMs\n' >&2
        exit 127
    }
    SUDO=(sudo)
fi

for command_name in cc dnf rpm rpmbuild runuser; do
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

"${ROOT_DIR}/packaging/ci/check-native-target.sh" "$RELEASE_TARGET" "$ARCH_TARGET"
"${ROOT_DIR}/packaging/ci/prepare-source-archives.sh"

if [[ "$START_PROJECT" == core ]]; then
    rm -rf -- "$BUILD_ROOT" "$PACKAGE_ROOT"
fi
mkdir -p "$BUILD_ROOT" "$PACKAGE_ROOT"

build_project() {
    local label="$1"
    local project="$2"
    local spec_name="$3"
    local archive_name="$4"
    local source_name="$5"
    local topdir="${BUILD_ROOT}/${label}/rpmbuild"
    local nvjpeg_setting=no
    local -a cuda_environment=()

    if [[ "$PACKAGE_VARIANT" == nvjpeg ]]; then
        nvjpeg_setting=yes
        : "${CUDA_HOME:?CUDA_HOME must identify the mounted CUDA toolkit}"
        [[ -x "${NVCC:-${CUDA_HOME}/bin/nvcc}" ]] || {
            printf "ERROR: nvcc was not found below CUDA_HOME '%s'\n" "$CUDA_HOME" >&2
            exit 1
        }
        cuda_environment=(
            env
            "CUDA_HOME=${CUDA_HOME}"
            "CUDA_PATH=${CUDA_HOME}"
            "LD_LIBRARY_PATH=${CUDA_LIBRARY_PATH:-${CUDA_HOME}/lib64}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
            "NVCC=${NVCC:-${CUDA_HOME}/bin/nvcc}"
        )
    fi

    rm -rf -- "${BUILD_ROOT:?}/${label}"
    mkdir -p "$topdir"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
    cp "${ROOT_DIR}/${project}/${spec_name}" "$topdir/SPECS/"
    cp "${ROOT_DIR}/${project}/${archive_name}" "$topdir/SOURCES/${source_name}"
    chown -R builder:builder "${BUILD_ROOT}/${label}"

    runuser -u builder -- "${cuda_environment[@]}" rpmbuild -ba \
        --define "_topdir ${topdir}" \
        --define "_smp_build_ncpus ${BUILD_JOBS}" \
        --define "veejay_arch_target ${ARCH_TARGET}" \
        --define "veejay_nvjpeg ${nvjpeg_setting}" \
        "${topdir}/SPECS/${spec_name}"

    if [[ "$project" == veejay-server ]]; then
        "${ROOT_DIR}/packaging/ci/verify-nvjpeg-config.sh" \
            "$PACKAGE_VARIANT" "${topdir}/BUILD" | \
            tee "${PACKAGE_ROOT}/NVJPEG-CONFIG-VERIFICATION.txt"
    fi

    while IFS= read -r -d '' package; do
        cp -f -- "$package" "$PACKAGE_ROOT/"
    done < <(find "$topdir/RPMS" -type f -name '*.rpm' -print0)

    rm -rf -- "${BUILD_ROOT:?}/${label}"
}

install_packages() {
    local wanted
    local package
    local -a matches=()

    for wanted in "$@"; do
        while IFS= read -r -d '' package; do
            if [[ "$(rpm -qp --queryformat '%{NAME}\n' "$package")" == "$wanted" ]]; then
                matches+=("$package")
            fi
        done < <(find "$PACKAGE_ROOT" -maxdepth 1 -type f -name '*.rpm' ! -name '*.src.rpm' -print0)
    done

    ((${#matches[@]} == $#)) || {
        printf 'ERROR: one or more locally built RPM dependencies are missing\n' >&2
        exit 1
    }
    "${SUDO[@]}" dnf install -y --nogpgcheck "${matches[@]}"
}

case "$START_PROJECT" in
    core)
        build_project core veejay-core veejay-core.spec \
            "veejaycore-${VERSION}.tar.gz" "veejay-core-${VERSION}.tar.gz"
        install_packages veejay-core veejay-core-devel
        ;&
    server)
        build_project server veejay-server veejay.spec \
            "veejay-${VERSION}.tar.gz" "veejay-${VERSION}.tar.gz"
        install_packages veejay veejay-devel
        ;&
    client)
        build_project client veejay-client veejay-client.spec \
            "reloaded-${VERSION}.tar.gz" "veejay-client-${VERSION}.tar.gz"
        ;&
    utils)
        build_project utils veejay-utils veejay-utils.spec \
            "veejay-utils-${VERSION}.tar.gz" "veejay-utils-${VERSION}.tar.gz"
        ;&
    eidolon)
        build_project eidolon veejay-eidolon veejay-eidolon.spec \
            "veejay-eidolon-${VERSION}.tar.gz" "veejay-eidolon-${VERSION}.tar.gz"
        ;&
    director)
        build_project director veejay-director veejay-director.spec \
            "reloaded-${VERSION}.tar.gz" "veejay-director-${VERSION}.tar.gz"
        ;;
    *)
        printf "ERROR: unknown start project '%s'\n" "$START_PROJECT" >&2
        exit 2
        ;;
esac

PACKAGE_BUILD_ENVIRONMENT="${PACKAGE_BUILD_ENVIRONMENT:-fedora-43}" \
    "${ROOT_DIR}/packaging/ci/verify-packages.sh" \
    rpm "$RELEASE_TARGET" "$ARCH_TARGET" "$PACKAGE_VARIANT" "$PACKAGE_ROOT"
