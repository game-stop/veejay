#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${VEEJAY_SOURCE_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)}"

die() {
    printf 'ERROR: %s\n' "$*" >&2
    exit 1
}

read_one() {
    local label="$1"
    local value="$2"

    [[ -n "$value" ]] || die "unable to read the version from ${label}"
    [[ "$value" != *$'\n'* ]] || die "multiple versions found in ${label}"
    printf '%s\n' "$value"
}

configure_version() {
    local file="$1"
    read_one "$file" "$(sed -n 's/^AC_INIT(\[[^]]*\],\[\([^]]*\)\].*/\1/p' "$file")"
}

make_version() {
    local file="$1"
    read_one "$file" "$(awk '$1 == "VERSION" && $2 == "=" { print $3 }' "$file")"
}

pkgbuild_version() {
    local file="$1"
    local value
    value="$(sed -n 's/^pkgver=//p' "$file")"
    value="${value#\'}"
    value="${value%\'}"
    value="${value#\"}"
    value="${value%\"}"
    read_one "$file" "$value"
}

spec_version() {
    local file="$1"
    read_one "$file" "$(awk '$1 == "Version:" { print $2 }' "$file")"
}

debian_version() {
    local file="$1"
    read_one "$file" "$(sed -n '1s/^[^ ]* (\([^)]*\)).*/\1/p' "$file")"
}

canonical_version() {
    configure_version "${ROOT_DIR}/veejay-core/configure.ac"
}

check_versions() {
    local expected
    local file
    local actual
    local -a configure_files=(
        veejay-core/configure.ac
        veejay-server/configure.ac
        veejay-client/configure.ac
        veejay-utils/configure.ac
        veejay-eidolon/configure.ac
        veejay-director/configure.ac
    )
    local -a pkgbuild_files=(
        veejay-core/PKGBUILD
        veejay-server/PKGBUILD
        veejay-client/PKGBUILD
        veejay-utils/PKGBUILD
        veejay-eidolon/PKGBUILD
        veejay-director/PKGBUILD
        sendVIMS/PKGBUILD
    )
    local -a spec_files=(
        veejay-core/veejay-core.spec
        veejay-server/veejay.spec
        veejay-client/veejay-client.spec
        veejay-utils/veejay-utils.spec
        veejay-eidolon/veejay-eidolon.spec
        veejay-director/veejay-director.spec
        sendVIMS/veejay-puredata.spec
    )
    local -a changelog_files=(
        veejay-core/debian/changelog
        veejay-server/debian/changelog
        veejay-client/debian/changelog
        veejay-utils/debian/changelog
        veejay-eidolon/debian/changelog
        veejay-director/debian/changelog
        sendVIMS/debian/changelog
    )

    expected="$(canonical_version)"
    [[ "$expected" =~ ^[0-9][0-9A-Za-z.+~:-]*$ ]] || die "invalid package version '${expected}'"

    for file in "${configure_files[@]}"; do
        actual="$(configure_version "${ROOT_DIR}/${file}")"
        [[ "$actual" == "$expected" ]] || die "${file} has version ${actual}; expected ${expected}"
    done

    actual="$(make_version "${ROOT_DIR}/sendVIMS/Makefile")"
    [[ "$actual" == "$expected" ]] || die "sendVIMS/Makefile has version ${actual}; expected ${expected}"

    for file in "${pkgbuild_files[@]}"; do
        actual="$(pkgbuild_version "${ROOT_DIR}/${file}")"
        [[ "$actual" == "$expected" ]] || die "${file} has version ${actual}; expected ${expected}"
    done

    for file in "${spec_files[@]}"; do
        actual="$(spec_version "${ROOT_DIR}/${file}")"
        [[ "$actual" == "$expected" ]] || die "${file} has version ${actual}; expected ${expected}"
    done

    for file in "${changelog_files[@]}"; do
        actual="$(debian_version "${ROOT_DIR}/${file}")"
        [[ "$actual" == "$expected" ]] || die "${file} has version ${actual}; expected ${expected}"
    done

    printf '%s\n' "$expected"
}

case "${1:-check}" in
    get)
        canonical_version
        ;;
    check)
        check_versions
        ;;
    check-tag)
        [[ $# -eq 2 ]] || die "usage: $0 check-tag [v]VERSION"
        version="$(check_versions)"
        [[ "$2" == "$version" || "$2" == "v${version}" ]] || die "release tag '$2' does not match package version '${version}'"
        printf '%s\n' "$version"
        ;;
    *)
        die "usage: $0 {get|check|check-tag [v]VERSION}"
        ;;
esac
