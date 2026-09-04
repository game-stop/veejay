#!/usr/bin/env bash
set -euo pipefail

[[ $# -eq 2 ]] || {
    printf 'usage: %s RELEASE_TARGET ARCH_TARGET\n' "$0" >&2
    exit 2
}

release_target="$1"
arch_target="$2"
host_arch="$(uname -m)"
compiler="${CC:-cc}"
compiler_flag=""

case "${release_target}:${arch_target}" in
    amd64-generic:generic)
        expected_host=x86_64
        ;;
    amd64-x86-64-v3:x86-64-v3)
        expected_host=x86_64
        compiler_flag=-march=x86-64-v3
        ;;
    arm64-generic:generic)
        expected_host=aarch64
        ;;
    arm64-cortex-a72:cortex-a72)
        expected_host=aarch64
        compiler_flag=-mcpu=cortex-a72
        ;;
    *)
        printf "ERROR: '%s' with arch target '%s' is not a standard GitHub-hosted release profile\n" \
            "$release_target" "$arch_target" >&2
        exit 2
        ;;
esac

if [[ "$host_arch" != "$expected_host" ]]; then
    printf "ERROR: release target '%s' requires host architecture '%s', found '%s'\n" \
        "$release_target" "$expected_host" "$host_arch" >&2
    exit 1
fi

if [[ -n "$compiler_flag" ]]; then
    test_dir="$(mktemp -d)"
    trap 'rm -rf -- "$test_dir"' EXIT
    printf '%s\n' 'int main(void) { return 0; }' | \
        "$compiler" "$compiler_flag" -x c - -o "${test_dir}/target-probe"
    "${test_dir}/target-probe"
fi

printf 'release_target=%s arch_target=%s host_arch=%s\n' \
    "$release_target" "$arch_target" "$host_arch"
