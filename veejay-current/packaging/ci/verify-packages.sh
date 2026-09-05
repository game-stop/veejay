#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${VEEJAY_SOURCE_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)}"
REPO_HINT="$(cd -- "${ROOT_DIR}/.." && pwd -P)"
REPO_ROOT="$(git -c safe.directory="$REPO_HINT" -C "$ROOT_DIR" rev-parse --show-toplevel)"

[[ $# -eq 5 ]] || {
    printf 'usage: %s FORMAT RELEASE_TARGET ARCH_TARGET VARIANT PACKAGE_DIR\n' "$0" >&2
    exit 2
}

format="$1"
release_target="$2"
arch_target="$3"
variant="$4"
package_dir="$5"
version="$("${ROOT_DIR}/packaging/ci/release-version.sh" check)"
source_commit="$(git -c safe.directory="$REPO_ROOT" -C "$ROOT_DIR" rev-parse HEAD)"
host_arch="$(uname -m)"
build_environment="${PACKAGE_BUILD_ENVIRONMENT:-unknown}"
tracked_dirty=no

case "$variant" in
    standard) suite_target="$release_target" ;;
    nvjpeg) suite_target="${release_target}-nvjpeg" ;;
    *)
        printf "ERROR: unknown package variant '%s'\n" "$variant" >&2
        exit 2
        ;;
esac

if [[ -n "$(git -c safe.directory="$REPO_ROOT" -C "$ROOT_DIR" status --short --untracked-files=no)" ]]; then
    tracked_dirty=yes
fi

[[ -d "$package_dir" ]] || {
    printf "ERROR: package directory '%s' does not exist\n" "$package_dir" >&2
    exit 1
}

find_packages() {
    case "$format" in
        deb) find "$package_dir" -maxdepth 1 -type f \( -name '*.deb' -o -name '*.ddeb' \) -print0 ;;
        rpm) find "$package_dir" -maxdepth 1 -type f -name '*.rpm' ! -name '*.src.rpm' -print0 ;;
        arch) find "$package_dir" -maxdepth 1 -type f -name '*.pkg.tar.*' ! -name '*.sig' -print0 ;;
    esac
}

case "$format" in
    deb)
        case "$release_target" in
            amd64-*) expected_arch=amd64 ;;
            arm64-*) expected_arch=arm64 ;;
            ppc64le-*) expected_arch=ppc64el ;;
            *) printf "ERROR: unsupported Debian release target '%s'\n" "$release_target" >&2; exit 2 ;;
        esac
        expected_packages=(
            veejay-core veejay veejay-client veejay-utils
            veejay-eidolon veejay-director veejay-puredata
        )
        package_name() { dpkg-deb -f "$1" Package; }
        package_arch() { dpkg-deb -f "$1" Architecture; }
        ;;
    rpm)
        case "$release_target" in
            amd64-*) expected_arch=x86_64 ;;
            arm64-*) expected_arch=aarch64 ;;
            ppc64le-*) expected_arch=ppc64le ;;
            *) printf "ERROR: unsupported RPM release target '%s'\n" "$release_target" >&2; exit 2 ;;
        esac
        expected_packages=(
            veejay-core veejay-core-devel veejay veejay-devel
            veejay-client veejay-utils veejay-eidolon veejay-director
        )
        package_name() { rpm -qp --queryformat '%{NAME}\n' "$1"; }
        package_arch() { rpm -qp --queryformat '%{ARCH}\n' "$1"; }
        ;;
    arch)
        expected_arch=x86_64
        expected_packages=(
            veejay-core veejay veejay-client veejay-utils
            veejay-eidolon veejay-director veejay-puredata
        )
        package_name() { bsdtar -xOf "$1" .PKGINFO | sed -n 's/^pkgname = //p'; }
        package_arch() { bsdtar -xOf "$1" .PKGINFO | sed -n 's/^arch = //p'; }
        ;;
    *)
        printf "ERROR: unsupported package format '%s'\n" "$format" >&2
        exit 2
        ;;
esac

mapfile -d '' -t package_files < <(find_packages)
((${#package_files[@]} > 0)) || {
    printf "ERROR: no %s packages found in '%s'\n" "$format" "$package_dir" >&2
    exit 1
}

declare -A seen=()
server_package=''
for package in "${package_files[@]}"; do
    name="$(package_name "$package")"
    architecture="$(package_arch "$package")"
    [[ -n "$name" ]] || {
        printf "ERROR: unable to read package name from '%s'\n" "$package" >&2
        exit 1
    }
    [[ "$architecture" == "$expected_arch" || "$architecture" == all || "$architecture" == noarch || "$architecture" == any ]] || {
        printf "ERROR: package '%s' has architecture '%s'; expected '%s'\n" \
            "$package" "$architecture" "$expected_arch" >&2
        exit 1
    }
    [[ -s "$package" ]] || {
        printf "ERROR: package '%s' is empty\n" "$package" >&2
        exit 1
    }
    seen["$name"]=1
    if [[ "$name" == veejay ]]; then
        [[ -z "$server_package" ]] || {
            printf "ERROR: multiple server packages were found in '%s'\n" "$package_dir" >&2
            exit 1
        }
        server_package="$package"
    fi
done

for name in "${expected_packages[@]}"; do
    [[ -n "${seen[$name]:-}" ]] || {
        printf "ERROR: expected package '%s' is missing from '%s'\n" "$name" "$package_dir" >&2
        exit 1
    }
done

if [[ "$format" == arch && -n "${seen[veejay-cuda-build-provider]:-}" ]]; then
    printf 'ERROR: temporary CUDA provider was copied into the Arch release assets\n' >&2
    exit 1
fi

[[ -n "$server_package" ]] || {
    printf "ERROR: server package was not found in '%s'\n" "$package_dir" >&2
    exit 1
}

config_verification="${package_dir}/NVJPEG-CONFIG-VERIFICATION.txt"
[[ -s "$config_verification" ]] || {
    printf "ERROR: nvJPEG configure verification is missing from '%s'\n" "$package_dir" >&2
    exit 1
}
case "$variant" in
    standard)
        grep -Eq '^variant=standard nvjpeg=no cuda_kernel=no ' "$config_verification"
        ;;
    nvjpeg)
        grep -Eq '^variant=nvjpeg nvjpeg=yes cuda_kernel=yes ' "$config_verification"
        ;;
esac

"${ROOT_DIR}/packaging/ci/verify-nvjpeg-package.sh" \
    "$format" "$variant" "$server_package" | \
    tee "${package_dir}/NVJPEG-PACKAGE-VERIFICATION.txt"

cuda_compiler=none
if [[ "$variant" == nvjpeg ]]; then
    nvcc_path="${NVCC:-}"
    if [[ -z "$nvcc_path" ]]; then
        nvcc_path="$(command -v nvcc 2>/dev/null || true)"
    fi
    if [[ -z "$nvcc_path" ]]; then
        for candidate in /opt/cuda/bin/nvcc /usr/local/cuda/bin/nvcc /usr/local/cuda-*/bin/nvcc; do
            if [[ -x "$candidate" ]]; then
                nvcc_path="$candidate"
                break
            fi
        done
    fi
    [[ -x "$nvcc_path" ]] || {
        printf 'ERROR: unable to record the nvcc version for the -nvjpeg suite\n' >&2
        exit 1
    }
    cuda_compiler="$("$nvcc_path" --version | sed -n 's/^.*release \([^,]*\),.*$/CUDA \1/p' | tail -n 1)"
    [[ -n "$cuda_compiler" ]] || cuda_compiler="$("$nvcc_path" --version | tail -n 1)"
    [[ -n "$cuda_compiler" ]] || {
        printf 'ERROR: nvcc did not report a compiler version\n' >&2
        exit 1
    }
fi

profile_tmp="${package_dir}/BUILD-PROFILE.txt.tmp"
{
    printf 'format=%s\n' "$format"
    printf 'release_target=%s\n' "$release_target"
    printf 'suite_target=%s\n' "$suite_target"
    printf 'arch_target=%s\n' "$arch_target"
    printf 'variant=%s\n' "$variant"
    printf 'nvjpeg=%s\n' "$([[ "$variant" == nvjpeg ]] && printf enabled || printf disabled)"
    printf 'nvjpeg_cuda_kernel=%s\n' "$([[ "$variant" == nvjpeg ]] && printf enabled || printf disabled)"
    printf 'cuda_compiler=%s\n' "$cuda_compiler"
    printf 'package_version=%s\n' "$version"
    printf 'source_commit=%s\n' "$source_commit"
    printf 'tracked_worktree_dirty=%s\n' "$tracked_dirty"
    printf 'host_arch=%s\n' "$host_arch"
    printf 'build_environment=%s\n' "$build_environment"
    printf 'package_count=%s\n' "${#package_files[@]}"
    printf 'built_at_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$profile_tmp"
mv -f -- "$profile_tmp" "${package_dir}/BUILD-PROFILE.txt"

checksum_tmp="${package_dir}/SHA256SUMS.tmp"
(
    cd "$package_dir"
    for package in "${package_files[@]}"; do
        sha256sum "${package##*/}"
    done
    sha256sum BUILD-PROFILE.txt \
        NVJPEG-CONFIG-VERIFICATION.txt \
        NVJPEG-PACKAGE-VERIFICATION.txt
) | sort -k2 > "$checksum_tmp"
mv -f -- "$checksum_tmp" "${package_dir}/SHA256SUMS"

printf 'Verified %d %s packages for %s\n' "${#package_files[@]}" "$format" "$suite_target"
