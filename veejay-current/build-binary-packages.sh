#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TARGET="all"
CREATE_SBUILD=0
PACKAGE_ARGS=()

usage() {
    cat <<'USAGE'
Usage: build-binary-packages.sh [--target deb|arch|rpm|all] [--prepare-sbuild] [make-var=value ...]

Build binary packages for the main VeeJay package roots.

Targets:
  deb      Build Debian packages via make deb
  arch     Build Arch packages via make arch
  rpm      Build RPM packages via make rpm
  all      Build deb, arch, and rpm packages in that order (default)

Options:
  --prepare-sbuild  Run make sbuild before make deb for each package.
  -h, --help        Show this help.

Common make variable overrides:
    MAKEFLAGS=-n
    MAKE=gmake
  SBUILD_CHROOT_MODE=unshare
  SBUILD_DIST=unstable
  SBUILD_CHROOT=$HOME/.cache/sbuild/unstable-amd64.tar.zst
  ARCH_CHROOT_DIR=/var/lib/archbuild/multilib-x86_64
  MOCK_CONFIG=epel-9-x86_64

Examples:
    MAKEFLAGS=-n ./build-binary-packages.sh --target deb --prepare-sbuild SBUILD_CHROOT_MODE=unshare SBUILD_DIST=unstable
    ./build-binary-packages.sh --target deb --prepare-sbuild SBUILD_CHROOT_MODE=unshare SBUILD_DIST=unstable
  ./build-binary-packages.sh --target arch ARCH_CHROOT_DIR=/var/lib/archbuild/multilib-x86_64
  ./build-binary-packages.sh --target rpm MOCK_CONFIG=epel-9-x86_64
USAGE
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --target)
            if [[ $# -lt 2 ]]; then
                echo "ERROR: --target requires deb, arch, rpm, or all" >&2
                exit 2
            fi
            TARGET="$2"
            shift 2
            ;;
        --target=*)
            TARGET="${1#--target=}"
            shift
            ;;
        --prepare-sbuild)
            CREATE_SBUILD=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            while [[ $# -gt 0 ]]; do
                PACKAGE_ARGS+=("$1")
                shift
            done
            ;;
        *=*)
            PACKAGE_ARGS+=("$1")
            shift
            ;;
        *)
            echo "ERROR: unknown argument '$1'" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case "${TARGET}" in
    deb|arch|rpm|all) ;;
    *)
        echo "ERROR: unknown target '${TARGET}'" >&2
        usage >&2
        exit 2
        ;;
esac

MAKE_CMD="${MAKE:-make}"

PROJECTS=(
    "veejay-core"
    "veejay-server"
    "veejay-client"
    "veejay-utils"
    "veejay-eidolon"
    "veejay-director"
    "sendVIMS"
)

run_make() {
    local project="$1"
    local make_target="$2"

    echo
    echo "==> ${project}: make ${make_target}"
    "${MAKE_CMD}" -C "${ROOT_DIR}/${project}" "${make_target}" "${PACKAGE_ARGS[@]}"
}

build_deb() {
    local project="$1"

    if [[ "${CREATE_SBUILD}" -eq 1 ]]; then
        run_make "${project}" sbuild
    fi
    run_make "${project}" deb
}

for project in "${PROJECTS[@]}"; do
    case "${TARGET}" in
        deb)
            build_deb "${project}"
            ;;
        arch)
            run_make "${project}" arch
            ;;
        rpm)
            run_make "${project}" rpm
            ;;
        all)
            build_deb "${project}"
            run_make "${project}" arch
            run_make "${project}" rpm
            ;;
    esac
done

echo
echo "All requested package targets completed."
