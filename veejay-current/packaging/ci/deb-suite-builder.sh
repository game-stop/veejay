#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${VEEJAY_SOURCE_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)}"
ARCH_TARGET="${1:-generic}"
START_PROJECT="${2:-core}"
VERSION="$("${ROOT_DIR}/packaging/ci/release-version.sh" check)"
HOST_DEB_ARCH="$(dpkg --print-architecture)"
RELEASE_TARGET="${RELEASE_TARGET:-${HOST_DEB_ARCH}-${ARCH_TARGET}}"
PACKAGE_VARIANT="${PACKAGE_VARIANT:-standard}"

case "$PACKAGE_VARIANT" in
    standard) SUITE_TARGET="$RELEASE_TARGET" ;;
    nvjpeg) SUITE_TARGET="${RELEASE_TARGET}-nvjpeg" ;;
    *)
        printf "ERROR: unknown package variant '%s'\n" "$PACKAGE_VARIANT" >&2
        exit 2
        ;;
esac

BUILD_ROOT="${DEB_BUILD_ROOT:-${ROOT_DIR}/packaging-build/${SUITE_TARGET}/deb-suite}"
PACKAGE_ROOT="${DEB_PACKAGE_ROOT:-${ROOT_DIR}/release-packages/${SUITE_TARGET}/deb}"

if [[ $EUID -eq 0 ]]; then
    SUDO=()
    BUILD_DEPS_ROOT=()
    BUILD_DEPS_TOOL='apt-get -y --no-install-recommends'
else
    command -v sudo >/dev/null 2>&1 || {
        printf 'ERROR: sudo is required to install Debian build dependencies\n' >&2
        exit 127
    }
    SUDO=(sudo)
    BUILD_DEPS_ROOT=(--root-cmd sudo)
    BUILD_DEPS_TOOL='apt-get -y --no-install-recommends'
fi

for command_name in cc dpkg-buildpackage dpkg-deb mk-build-deps; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf "ERROR: required command '%s' was not found\n" "$command_name" >&2
        exit 127
    }
done

"${ROOT_DIR}/packaging/ci/check-native-target.sh" "$RELEASE_TARGET" "$ARCH_TARGET"
"${ROOT_DIR}/packaging/ci/prepare-source-archives.sh"

if [[ "$START_PROJECT" == core ]]; then
    rm -rf -- "$BUILD_ROOT" "$PACKAGE_ROOT"
fi
mkdir -p "$BUILD_ROOT" "$PACKAGE_ROOT"

build_project() {
    local label="$1"
    local project="$2"
    local dist_name="$3"
    local project_build="${BUILD_ROOT}/${label}"
    local source_dir="${project_build}/${dist_name}-${VERSION}"
    local archive="${ROOT_DIR}/${project}/${dist_name}-${VERSION}.tar.gz"

    rm -rf -- "$project_build"
    mkdir -p "$project_build"
    tar -xzf "$archive" -C "$project_build"

    if [[ "$project" != sendVIMS ]]; then
        {
            printf 'VEEJAY_ARCH_TARGET := %s\n' "$ARCH_TARGET"
            if [[ "$project" == veejay-server ]]; then
                printf 'VEEJAY_NVJPEG_CONFIGURE := --with-nvjpeg=%s\n' \
                    "$([[ "$PACKAGE_VARIANT" == nvjpeg ]] && printf yes || printf no)"
            fi
        } > "${source_dir}/debian/veejay-release.mk"
    fi

    (
        cd "$source_dir"
        mk-build-deps --install --remove "${BUILD_DEPS_ROOT[@]}" \
            --tool "$BUILD_DEPS_TOOL" debian/control
        DEB_BUILD_OPTIONS="nocheck parallel=$(nproc)" \
            dpkg-buildpackage -us -uc -b
    )

    if [[ "$project" == veejay-server ]]; then
        "${ROOT_DIR}/packaging/ci/verify-nvjpeg-config.sh" \
            "$PACKAGE_VARIANT" "$source_dir" | \
            tee "${PACKAGE_ROOT}/NVJPEG-CONFIG-VERIFICATION.txt"
    fi

    while IFS= read -r -d '' package; do
        cp -f -- "$package" "$PACKAGE_ROOT/"
    done < <(find "$project_build" -maxdepth 1 -type f \( -name '*.deb' -o -name '*.ddeb' \) -print0)

    rm -rf -- "$project_build"
}

install_package() {
    local wanted="$1"
    local package
    local -a matches=()

    while IFS= read -r -d '' package; do
        if [[ "$(dpkg-deb -f "$package" Package)" == "$wanted" ]]; then
            matches+=("$package")
        fi
    done < <(find "$PACKAGE_ROOT" -maxdepth 1 -type f -name '*.deb' -print0)

    ((${#matches[@]} > 0)) || {
        printf "ERROR: built Debian package '%s' was not found\n" "$wanted" >&2
        exit 1
    }
    "${SUDO[@]}" apt-get install -y --no-install-recommends "${matches[@]}"
}

case "$START_PROJECT" in
    core)
        build_project core veejay-core veejaycore
        install_package veejay-core
        ;&
    server)
        build_project server veejay-server veejay
        install_package veejay
        ;&
    client)
        build_project client veejay-client reloaded
        ;&
    utils)
        build_project utils veejay-utils veejay-utils
        ;&
    eidolon)
        build_project eidolon veejay-eidolon veejay-eidolon
        ;&
    director)
        build_project director veejay-director reloaded
        ;&
    puredata)
        build_project puredata sendVIMS veejay-puredata
        ;;
    *)
        printf "ERROR: unknown start project '%s'\n" "$START_PROJECT" >&2
        exit 2
        ;;
esac

PACKAGE_BUILD_ENVIRONMENT="${PACKAGE_BUILD_ENVIRONMENT:-ubuntu-24.04}" \
    "${ROOT_DIR}/packaging/ci/verify-packages.sh" \
    deb "$RELEASE_TARGET" "$ARCH_TARGET" "$PACKAGE_VARIANT" "$PACKAGE_ROOT"
