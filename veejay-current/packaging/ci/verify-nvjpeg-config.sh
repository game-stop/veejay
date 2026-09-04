#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 2 ]] || {
    printf 'usage: %s {standard|nvjpeg} SERVER_BUILD_ROOT\n' "$0" >&2
    exit 2
}

variant="$1"
build_root="$2"

case "$variant" in
    standard|nvjpeg) ;;
    *)
        printf "ERROR: unknown package variant '%s'\n" "$variant" >&2
        exit 2
        ;;
esac

[[ -d "$build_root" ]] || {
    printf "ERROR: server build root '%s' does not exist\n" "$build_root" >&2
    exit 1
}

mapfile -d '' -t candidates < <(find "$build_root" -type f -name config.h -print0)
config_header=''
for candidate in "${candidates[@]}"; do
    if grep -q '^#define PACKAGE_NAME "veejay"$' "$candidate"; then
        [[ -z "$config_header" ]] || {
            printf "ERROR: multiple configured VeeJay server headers under '%s'\n" "$build_root" >&2
            exit 1
        }
        config_header="$candidate"
    fi
done

[[ -n "$config_header" ]] || {
    printf "ERROR: configured VeeJay server config.h was not found under '%s'\n" "$build_root" >&2
    exit 1
}

nvjpeg=no
cuda_kernel=no
grep -q '^#define HAVE_NVJPEG 1$' "$config_header" && nvjpeg=yes
grep -q '^#define HAVE_NVJPEG_CUDA_KERNEL 1$' "$config_header" && cuda_kernel=yes

case "$variant" in
    standard)
        [[ "$nvjpeg" == no && "$cuda_kernel" == no ]] || {
            printf 'ERROR: standard build unexpectedly enabled nvJPEG or its CUDA kernel\n' >&2
            exit 1
        }
        ;;
    nvjpeg)
        [[ "$nvjpeg" == yes ]] || {
            printf 'ERROR: -nvjpeg build did not define HAVE_NVJPEG\n' >&2
            exit 1
        }
        [[ "$cuda_kernel" == yes ]] || {
            printf 'ERROR: -nvjpeg build did not define HAVE_NVJPEG_CUDA_KERNEL\n' >&2
            exit 1
        }
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
            printf 'ERROR: -nvjpeg build completed without a verifiable nvcc compiler\n' >&2
            exit 1
        }
        ;;
esac

printf 'variant=%s nvjpeg=%s cuda_kernel=%s config=%s\n' \
    "$variant" "$nvjpeg" "$cuda_kernel" "${config_header##*/}"
