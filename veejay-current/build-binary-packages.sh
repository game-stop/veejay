#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${VEEJAY_SOURCE_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)}"
if [[ "${PACKAGE_SUITE_SNAPSHOT:-}" != "$0" ]]; then
    snapshot="$(mktemp /tmp/build-binary-packages.XXXXXX)"
    cp "$0" "$snapshot"
    chmod 755 "$snapshot"
    exec env PACKAGE_SUITE_SNAPSHOT="$snapshot" VEEJAY_SOURCE_ROOT="$ROOT_DIR" \
        "$snapshot" "$@"
fi
trap 'rm -f -- "$PACKAGE_SUITE_SNAPSHOT"' EXIT

TARGET="all"
ARCH_TARGET="generic"
ARCH_TARGET_SET=0
RELEASE_TARGET=""
RELEASE_ROOT="${RELEASE_ROOT:-${ROOT_DIR}/release-packages/$(date +%F)}"
START_PROJECT=""
BOOTSTRAP=1
CREATE_SBUILD=0
PACKAGE_ARGS=()
TARGET_ARGS=()

usage() {
    cat <<'USAGE'
Usage: build-binary-packages.sh [options] [make-var=value ...]

Build binary packages for the main VeeJay package roots.

Targets:
  deb      Build Debian packages via make deb
  arch     Build Arch packages via make arch
  rpm      Build RPM packages via make rpm
  all      Build deb, arch, and rpm packages in that order (default)

Options:
    --release-target NAME
                                        Select a named architecture/profile from --list-targets.
    --arch-target VALUE
                                        Configure portable (generic), build-host (native), or
                                        compiler-validated CPU-specific binaries. Default: generic.
    --release-dir DIR Store profile-labelled artifacts below DIR.
    --from PROJECT   Resume at a package root from the package suite list.
    --no-bootstrap   Do not refresh stale Autotools-generated Makefiles.
    --list-targets   Print the release CPU target matrix and exit.
  --prepare-sbuild  Run make sbuild before make deb for each package.
  -h, --help        Show this help.

Common make variable overrides:
    MAKEFLAGS=-n
    MAKE=gmake
  SBUILD_CHROOT_MODE=unshare
  SBUILD_DIST=unstable
  SBUILD_CHROOT=$HOME/.cache/sbuild/unstable-amd64.tar.zst
    SBUILD_CREATE_ARGS=--keyring=/path/to/debian-ports-archive-keyring.gpg
  ARCH_CHROOT_DIR=/var/lib/archbuild/multilib-x86_64
  MOCK_CONFIG=epel-9-x86_64

Examples:
    MAKEFLAGS=-n ./build-binary-packages.sh --target deb --release-target amd64-generic --no-bootstrap
    ./build-binary-packages.sh --target deb --release-target amd64-x86-64-v3 --prepare-sbuild SBUILD_CHROOT_MODE=unshare
  ./build-binary-packages.sh --target arch ARCH_CHROOT_DIR=/var/lib/archbuild/multilib-x86_64
  ./build-binary-packages.sh --target rpm MOCK_CONFIG=epel-9-x86_64
USAGE
}

list_targets() {
    cat <<'TARGETS'
Release target                 Package ecosystems            --arch-target
amd64-generic                  Debian, RPM, Arch              generic
amd64-x86-64-v3               Debian, RPM, Arch              x86-64-v3
armhf-generic                  Debian, Arch; community RPM    generic
armhf-cortex-a7-neon           Debian, Arch; community RPM    cortex-a7
arm64-generic                  Debian, RPM, Arch Linux ARM    generic
arm64-cortex-a72               Debian, RPM, Arch Linux ARM    cortex-a72
powerpc-generic                Debian Ports, community RPM/Arch generic
powerpc-7400-altivec           Debian Ports, community RPM/Arch 7400
ppc64-generic                  Debian Ports, community RPM/Arch generic
ppc64-power7-altivec           Debian Ports, community RPM/Arch power7
ppc64le-generic                Debian, RPM, ArchPOWER         generic
ppc64le-power8-altivec         Debian, RPM, ArchPOWER         power8

native is a local-machine build profile, not a redistributable release target.
Generic AArch64 already includes baseline ASIMD. Specialized ARM and PowerPC
packages are compile-time ISA targets and must be kept in compatibility-labelled
repositories or artifact directories.
TARGETS
}

set_release_target() {
    local expected_arch_target=""
    local deb_arch=""
    local arch_chroot_arch=""

    case "${RELEASE_TARGET}" in
        amd64-generic)
            expected_arch_target="generic"; deb_arch="amd64"; arch_chroot_arch="x86_64"
            TARGET_ARGS+=("MOCK_CONFIG=fedora-43-x86_64")
            ;;
        amd64-x86-64-v3)
            expected_arch_target="x86-64-v3"; deb_arch="amd64"; arch_chroot_arch="x86_64"
            TARGET_ARGS+=("MOCK_CONFIG=fedora-43-x86_64")
            ;;
        armhf-generic)
            expected_arch_target="generic"; deb_arch="armhf"; arch_chroot_arch="armv7h"
            ;;
        armhf-cortex-a7-neon)
            expected_arch_target="cortex-a7"; deb_arch="armhf"; arch_chroot_arch="armv7h"
            ;;
        arm64-generic)
            expected_arch_target="generic"; deb_arch="arm64"; arch_chroot_arch="aarch64"
            TARGET_ARGS+=("MOCK_CONFIG=fedora-43-aarch64")
            ;;
        arm64-cortex-a72)
            expected_arch_target="cortex-a72"; deb_arch="arm64"; arch_chroot_arch="aarch64"
            TARGET_ARGS+=("MOCK_CONFIG=fedora-43-aarch64")
            ;;
        powerpc-generic)
            expected_arch_target="generic"; deb_arch="powerpc"; arch_chroot_arch="powerpc"
            TARGET_ARGS+=("SBUILD_DIST=sid" "SBUILD_MIRROR=http://ftp.ports.debian.org/debian-ports")
            ;;
        powerpc-7400-altivec)
            expected_arch_target="7400"; deb_arch="powerpc"; arch_chroot_arch="powerpc"
            TARGET_ARGS+=("SBUILD_DIST=sid" "SBUILD_MIRROR=http://ftp.ports.debian.org/debian-ports")
            export SBUILD_QEMU_CPU=7400
            ;;
        ppc64-generic)
            expected_arch_target="generic"; deb_arch="ppc64"; arch_chroot_arch="powerpc64"
            TARGET_ARGS+=("SBUILD_DIST=sid" "SBUILD_MIRROR=http://ftp.ports.debian.org/debian-ports")
            ;;
        ppc64-power7-altivec)
            expected_arch_target="power7"; deb_arch="ppc64"; arch_chroot_arch="powerpc64"
            TARGET_ARGS+=("SBUILD_DIST=sid" "SBUILD_MIRROR=http://ftp.ports.debian.org/debian-ports")
            export SBUILD_QEMU_CPU=power7
            ;;
        ppc64le-generic)
            expected_arch_target="generic"; deb_arch="ppc64el"; arch_chroot_arch="powerpc64le"
            TARGET_ARGS+=("MOCK_CONFIG=fedora-43-ppc64le")
            ;;
        ppc64le-power8-altivec)
            expected_arch_target="power8"; deb_arch="ppc64el"; arch_chroot_arch="powerpc64le"
            TARGET_ARGS+=("MOCK_CONFIG=fedora-43-ppc64le")
            ;;
        *)
            echo "ERROR: unknown release target '${RELEASE_TARGET}'" >&2
            list_targets >&2
            exit 2
            ;;
    esac

    if [[ "${ARCH_TARGET_SET}" -eq 1 && "${ARCH_TARGET}" != "${expected_arch_target}" ]]; then
        echo "ERROR: release target '${RELEASE_TARGET}' requires --arch-target=${expected_arch_target}" >&2
        exit 2
    fi

    ARCH_TARGET="${expected_arch_target}"
    TARGET_ARGS+=("SBUILD_ARCH=${deb_arch}")
    TARGET_ARGS+=("ARCH_CHROOT_DIR=/var/lib/archbuild/extra-${arch_chroot_arch}")
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
        --arch-target)
            if [[ $# -lt 2 ]]; then
                echo "ERROR: --arch-target requires generic, native, or a CPU name" >&2
                exit 2
            fi
            ARCH_TARGET="$2"
            ARCH_TARGET_SET=1
            shift 2
            ;;
        --arch-target=*)
            ARCH_TARGET="${1#--arch-target=}"
            ARCH_TARGET_SET=1
            shift
            ;;
        --release-target)
            if [[ $# -lt 2 ]]; then
                echo "ERROR: --release-target requires a name from --list-targets" >&2
                exit 2
            fi
            RELEASE_TARGET="$2"
            shift 2
            ;;
        --release-target=*)
            RELEASE_TARGET="${1#--release-target=}"
            shift
            ;;
        --release-dir)
            if [[ $# -lt 2 ]]; then
                echo "ERROR: --release-dir requires a directory" >&2
                exit 2
            fi
            RELEASE_ROOT="$2"
            shift 2
            ;;
        --release-dir=*)
            RELEASE_ROOT="${1#--release-dir=}"
            shift
            ;;
        --from)
            if [[ $# -lt 2 ]]; then
                echo "ERROR: --from requires a package root" >&2
                exit 2
            fi
            START_PROJECT="$2"
            shift 2
            ;;
        --from=*)
            START_PROJECT="${1#--from=}"
            shift
            ;;
        --no-bootstrap)
            BOOTSTRAP=0
            shift
            ;;
        --list-targets)
            list_targets
            exit 0
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

if [[ -n "${RELEASE_TARGET}" ]]; then
    set_release_target
else
    host_arch="$(dpkg --print-architecture 2>/dev/null || uname -m)"
    RELEASE_TARGET="${host_arch}-${ARCH_TARGET}"
fi

if [[ ! "${ARCH_TARGET}" =~ ^[A-Za-z0-9][A-Za-z0-9._+-]*$ ]]; then
    echo "ERROR: --arch-target must be a compiler CPU name containing only letters, digits, '.', '_', '+', or '-'" >&2
    exit 2
fi

PACKAGE_ARGS=("VEEJAY_ARCH_TARGET=${ARCH_TARGET}" "${TARGET_ARGS[@]}" "${PACKAGE_ARGS[@]}")

MAKE_CMD="${MAKE:-make}"

if [[ "${RELEASE_ROOT}" != /* ]]; then
    RELEASE_ROOT="${ROOT_DIR}/${RELEASE_ROOT}"
fi
ARTIFACT_ROOT="${RELEASE_ROOT}/${RELEASE_TARGET}"
PACKAGE_BUILD_DIR="packaging-build/${RELEASE_TARGET}"
command -v flock >/dev/null 2>&1 || {
    echo "ERROR: flock is required to protect release artifacts" >&2
    exit 127
}
mkdir -p "${RELEASE_ROOT}/.locks"
exec 8>"${RELEASE_ROOT}/.locks/${RELEASE_TARGET}.lock"
if ! flock -n 8; then
    echo "ERROR: another package suite is writing release target '${RELEASE_TARGET}'" >&2
    exit 75
fi

PROJECTS=(
    "veejay-core"
    "veejay-server"
    "veejay-client"
    "veejay-utils"
    "veejay-eidolon"
    "veejay-director"
    "sendVIMS"
)

if [[ -n "${START_PROJECT}" ]]; then
    RESUMED_PROJECTS=()
    found_start=0
    for project in "${PROJECTS[@]}"; do
        if [[ "${project}" == "${START_PROJECT}" ]]; then
            found_start=1
        fi
        if [[ "${found_start}" -eq 1 ]]; then
            RESUMED_PROJECTS+=("${project}")
        fi
    done
    if [[ "${found_start}" -eq 0 ]]; then
        echo "ERROR: --from project '${START_PROJECT}' is not in the package suite" >&2
        exit 2
    fi
    PROJECTS=("${RESUMED_PROJECTS[@]}")
fi

declare -A PREPARED_PROJECTS=()

prepare_project() {
    local project="$1"
    local project_dir="${ROOT_DIR}/${project}"

    if [[ "${BOOTSTRAP}" -eq 0 || "${project}" == "sendVIMS" || -n "${PREPARED_PROJECTS[${project}]:-}" ]]; then
        return
    fi

    if [[ ! -f "${project_dir}/Makefile" ||
          "${project_dir}/packaging.am" -nt "${project_dir}/Makefile" ||
          "${project_dir}/Makefile.am" -nt "${project_dir}/Makefile" ||
          "${project_dir}/configure.ac" -nt "${project_dir}/configure" ]]; then
        echo
        echo "==> ${project}: refreshing Autotools package rules"
        pushd "${project_dir}" >/dev/null
        ./autogen.sh
        ./configure --with-arch-target=generic
        popd >/dev/null
    fi

    PREPARED_PROJECTS["${project}"]=1
}

write_manifest() {
    mkdir -p "${ARTIFACT_ROOT}"
    pushd "${ARTIFACT_ROOT}" >/dev/null
    find . -type f ! -name SHA256SUMS ! -name '*.log' -print0 | sort -z | xargs -0 -r sha256sum > SHA256SUMS
    popd >/dev/null
}

write_profile() {
    local commit
    local dirty="no"

    commit="$(git -C "${ROOT_DIR}" rev-parse HEAD 2>/dev/null || echo unknown)"
    if [[ -n "$(git -C "${ROOT_DIR}" status --short --untracked-files=no 2>/dev/null)" ]]; then
        dirty="yes"
    fi

    mkdir -p "${ARTIFACT_ROOT}"
    {
        printf 'release_target=%s\n' "${RELEASE_TARGET}"
        printf 'arch_target=%s\n' "${ARCH_TARGET}"
        printf 'source_commit=%s\n' "${commit}"
        printf 'tracked_worktree_dirty=%s\n' "${dirty}"
        printf 'built_at_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > "${ARTIFACT_ROOT}/BUILD-PROFILE.txt"
    write_manifest
}

collect_artifacts() {
    local project="$1"
    local format="$2"
    local source_dir="${ROOT_DIR}/${project}/${PACKAGE_BUILD_DIR}/${format}"
    local destination="${ARTIFACT_ROOT}/${format}"
    local artifact

    [[ -d "${source_dir}" ]] || return 0
    mkdir -p "${destination}"

    case "${format}" in
        deb)
            while IFS= read -r -d '' artifact; do
                cp -f -- "${artifact}" "${destination}/"
            done < <(find "${source_dir}" -maxdepth 1 -type f \
                \( -name '*.deb' -o -name '*.ddeb' -o -name '*.changes' -o -name '*.buildinfo' \) -print0)
            ;;
        arch)
            while IFS= read -r -d '' artifact; do
                cp -f -- "${artifact}" "${destination}/"
            done < <(find "${source_dir}" -maxdepth 1 -type f -name '*.pkg.tar.*' -print0)
            ;;
        rpm)
            while IFS= read -r -d '' artifact; do
                cp -f -- "${artifact}" "${destination}/"
            done < <(find "${source_dir}" -type f -name '*.rpm' ! -name '*.src.rpm' -print0)
            ;;
    esac

    write_manifest
}

run_make() {
    local project="$1"
    local make_target="$2"
    local format="${3:-}"
    local -a local_args=("${PACKAGE_ARGS[@]}" "PACKAGING_BUILD_DIR=${PACKAGE_BUILD_DIR}")

    prepare_project "${project}"

    case "${format}" in
        deb) local_args+=("SBUILD_LOCAL_PACKAGE_DIR=${ARTIFACT_ROOT}/deb") ;;
        arch) local_args+=("ARCH_LOCAL_PACKAGE_DIR=${ARTIFACT_ROOT}/arch") ;;
        rpm) local_args+=("MOCK_LOCAL_PACKAGE_DIR=${ARTIFACT_ROOT}/rpm") ;;
    esac

    echo
    echo "==> ${project}: make ${make_target}"
    "${MAKE_CMD}" -C "${ROOT_DIR}/${project}" "${make_target}" "${local_args[@]}"

    if [[ -n "${format}" && "${make_target}" == "${format}" ]]; then
        collect_artifacts "${project}" "${format}"
    fi
}

build_deb() {
    local project="$1"
    run_make "${project}" deb deb
}

write_profile

if [[ "${CREATE_SBUILD}" -eq 1 && ("${TARGET}" == "deb" || "${TARGET}" == "all") ]]; then
    run_make "${PROJECTS[0]}" sbuild
fi

for project in "${PROJECTS[@]}"; do
    case "${TARGET}" in
        deb)
            build_deb "${project}"
            ;;
        arch)
            run_make "${project}" arch arch
            ;;
        rpm)
            run_make "${project}" rpm rpm
            ;;
        all)
            build_deb "${project}"
            run_make "${project}" arch arch
            run_make "${project}" rpm rpm
            ;;
    esac
done

write_manifest

echo
echo "All requested package targets completed."
echo "Artifacts: ${ARTIFACT_ROOT}"
