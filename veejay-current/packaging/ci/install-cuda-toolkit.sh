#!/usr/bin/env bash
set -euo pipefail

CUDA_SERIES="${CUDA_SERIES:-13-3}"
CUDA_DOTTED_SERIES="${CUDA_SERIES//-/.}"
CUDA_REPOSITORY_ROOT='https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2404'

[[ "$CUDA_SERIES" =~ ^[0-9]+-[0-9]+$ ]] || {
    printf "ERROR: invalid CUDA series '%s'\n" "$CUDA_SERIES" >&2
    exit 2
}

[[ -r /etc/os-release ]] || {
    printf 'ERROR: /etc/os-release is required to validate the CUDA build host\n' >&2
    exit 1
}
# shellcheck disable=SC1091
source /etc/os-release
[[ "${ID:-}" == ubuntu && "${VERSION_ID:-}" == 24.04 ]] || {
    printf 'ERROR: the pinned CUDA repository requires Ubuntu 24.04\n' >&2
    exit 2
}

if [[ $EUID -eq 0 ]]; then
    SUDO=()
else
    command -v sudo >/dev/null 2>&1 || {
        printf 'ERROR: sudo is required to install the CUDA build toolkit\n' >&2
        exit 127
    }
    SUDO=(sudo)
fi

case "$(uname -m)" in
    x86_64)
        repository_arch=x86_64
        cuda_target=x86_64-linux
        ;;
    aarch64)
        repository_arch=sbsa
        cuda_target=sbsa-linux
        ;;
    *)
        printf 'ERROR: CUDA release builds support only x86_64 and aarch64 hosts\n' >&2
        exit 2
        ;;
esac

for command_name in apt-get curl dpkg; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf "ERROR: required command '%s' was not found\n" "$command_name" >&2
        exit 127
    }
done

repository_url="${CUDA_REPOSITORY_ROOT}/${repository_arch}"
keyring_path=/usr/share/keyrings/veejay-cuda-archive-keyring.gpg
source_list_path=/etc/apt/sources.list.d/veejay-cuda.list
pin_path=/etc/apt/preferences.d/veejay-cuda-repository-pin-600
temporary_dir="$(mktemp -d)"
trap 'rm -rf -- "$temporary_dir"' EXIT

curl --fail --location --silent --show-error \
    "${repository_url}/cuda-archive-keyring.gpg" \
    --output "${temporary_dir}/cuda-archive-keyring.gpg"
curl --fail --location --silent --show-error \
    "${repository_url}/cuda-ubuntu2404.pin" \
    --output "${temporary_dir}/cuda-ubuntu2404.pin"
test -s "${temporary_dir}/cuda-archive-keyring.gpg"
test -s "${temporary_dir}/cuda-ubuntu2404.pin"

"${SUDO[@]}" install -m 0644 \
    "${temporary_dir}/cuda-archive-keyring.gpg" "$keyring_path"
"${SUDO[@]}" install -m 0644 \
    "${temporary_dir}/cuda-ubuntu2404.pin" "$pin_path"
printf 'deb [signed-by=%s] %s /\n' "$keyring_path" "$repository_url" | \
    "${SUDO[@]}" tee "$source_list_path" >/dev/null

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y --no-install-recommends \
    "cuda-nvcc-${CUDA_SERIES}" \
    "cuda-cudart-dev-${CUDA_SERIES}" \
    "libnvjpeg-${CUDA_SERIES}" \
    "libnvjpeg-dev-${CUDA_SERIES}"

cuda_home="/usr/local/cuda-${CUDA_DOTTED_SERIES}"
[[ -x "${cuda_home}/bin/nvcc" ]] || {
    printf "ERROR: CUDA compiler was not installed under '%s'\n" "$cuda_home" >&2
    exit 1
}
[[ -f "${cuda_home}/include/nvjpeg.h" || \
   -f "${cuda_home}/targets/$(uname -m)-linux/include/nvjpeg.h" || \
   -f "${cuda_home}/targets/${repository_arch}-linux/include/nvjpeg.h" ]] || {
    printf "ERROR: nvjpeg.h was not installed under '%s'\n" "$cuda_home" >&2
    exit 1
}

cuda_library_path=''
for candidate in \
    "${cuda_home}/lib64" \
    "${cuda_home}/targets/${cuda_target}/lib"; do
    if [[ -e "${candidate}/libnvjpeg.so" && -e "${candidate}/libcudart.so" ]]; then
        cuda_library_path="$candidate"
        break
    fi
done
[[ -n "$cuda_library_path" ]] || {
    printf "ERROR: linkable nvJPEG and CUDA runtime libraries were not installed under '%s'\n" \
        "$cuda_home" >&2
    exit 1
}

"${cuda_home}/bin/nvcc" --version
if [[ -n "${GITHUB_ENV:-}" ]]; then
    {
        printf 'CUDA_HOME=%s\n' "$cuda_home"
        printf 'CUDA_PATH=%s\n' "$cuda_home"
        printf 'NVCC=%s/bin/nvcc\n' "$cuda_home"
        printf 'CUDA_LIBRARY_PATH=%s\n' "$cuda_library_path"
        printf 'LD_LIBRARY_PATH=%s\n' "$cuda_library_path"
    } >> "$GITHUB_ENV"
fi
if [[ -n "${GITHUB_PATH:-}" ]]; then
    printf '%s/bin\n' "$cuda_home" >> "$GITHUB_PATH"
fi

printf 'CUDA_HOME=%s\n' "$cuda_home"
printf 'CUDA_LIBRARY_PATH=%s\n' "$cuda_library_path"
