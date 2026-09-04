#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 3 ]] || {
    printf 'usage: %s {deb|rpm|arch} {standard|nvjpeg} SERVER_PACKAGE\n' "$0" >&2
    exit 2
}

format="$1"
variant="$2"
server_package="$3"

case "$variant" in
    standard|nvjpeg) ;;
    *)
        printf "ERROR: unknown package variant '%s'\n" "$variant" >&2
        exit 2
        ;;
esac

for command_name in find readelf; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf "ERROR: required command '%s' was not found\n" "$command_name" >&2
        exit 127
    }
done

extract_root="$(mktemp -d)"
trap 'rm -rf -- "$extract_root"' EXIT

case "$format" in
    deb)
        command -v dpkg-deb >/dev/null 2>&1
        dpkg-deb -x "$server_package" "$extract_root"
        ;;
    rpm)
        for command_name in cpio rpm2cpio; do
            command -v "$command_name" >/dev/null 2>&1 || {
                printf "ERROR: required command '%s' was not found\n" "$command_name" >&2
                exit 127
            }
        done
        (
            cd "$extract_root"
            rpm2cpio "$server_package" | cpio -idm --quiet
        )
        ;;
    arch)
        command -v bsdtar >/dev/null 2>&1
        bsdtar -xf "$server_package" -C "$extract_root"
        ;;
    *)
        printf "ERROR: unsupported package format '%s'\n" "$format" >&2
        exit 2
        ;;
esac

found_elf=no
needs_nvjpeg=no
needs_cudart=no
while IFS= read -r -d '' candidate; do
    dynamic_section="$(readelf -d "$candidate" 2>/dev/null || true)"
    [[ -n "$dynamic_section" ]] || continue
    found_elf=yes
    grep -Eq 'Shared library: \[libnvjpeg\.so' <<< "$dynamic_section" && needs_nvjpeg=yes
    grep -Eq 'Shared library: \[libcudart\.so' <<< "$dynamic_section" && needs_cudart=yes
done < <(find "$extract_root" -type f -print0)

[[ "$found_elf" == yes ]] || {
    printf "ERROR: no ELF files were found in server package '%s'\n" "$server_package" >&2
    exit 1
}

case "$variant" in
    standard)
        [[ "$needs_nvjpeg" == no && "$needs_cudart" == no ]] || {
            printf 'ERROR: standard server package unexpectedly links CUDA libraries\n' >&2
            exit 1
        }
        ;;
    nvjpeg)
        [[ "$needs_nvjpeg" == yes ]] || {
            printf 'ERROR: -nvjpeg server package does not link libnvjpeg\n' >&2
            exit 1
        }
        [[ "$needs_cudart" == yes ]] || {
            printf 'ERROR: -nvjpeg server package does not link libcudart\n' >&2
            exit 1
        }
        ;;
esac

printf 'variant=%s package=%s libnvjpeg=%s libcudart=%s\n' \
    "$variant" "${server_package##*/}" "$needs_nvjpeg" "$needs_cudart"
