#!/usr/bin/env bash
set -euo pipefail

if [[ "${RPM_SUITE_SNAPSHOT:-}" != "$0" ]]; then
    snapshot="$(mktemp /tmp/rpm-suite-builder.XXXXXX)"
    cp "$0" "$snapshot"
    chmod 755 "$snapshot"
    exec env RPM_SUITE_SNAPSHOT="$snapshot" "$snapshot" "$@"
fi
trap 'rm -f -- "$RPM_SUITE_SNAPSHOT"' EXIT

arch_target="${1:-generic}"
start_package="${2:-core}"
build_jobs="${RPM_BUILD_NCPUS:-4}"
suite_lock="${RPM_SUITE_LOCK:-/packages.lock}"
command -v flock >/dev/null 2>&1 || {
    echo "flock is required to protect the shared RPM build root" >&2
    exit 127
}
mkdir -p "$(dirname -- "$suite_lock")"
exec 9>"$suite_lock"
if ! flock -n 9; then
    echo "Another RPM package suite is using /work and /packages" >&2
    exit 75
fi
if [[ "$start_package" == "core" ]]; then
    rm -rf /work /packages
fi
mkdir -p /work /packages

build_package() {
    local label="$1"
    local spec_source="$2"
    local archive_source="$3"
    local source_name="$4"
    local topdir="/work/${label}/rpmbuild"

    mkdir -p "$topdir"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}
    cp "$spec_source" "$topdir/SPECS/"
    cp "$archive_source" "$topdir/SOURCES/$source_name"
    chown -R builder:builder "/work/${label}"
    runuser -u builder -- rpmbuild -ba \
        --define "_topdir $topdir" \
        --define "_smp_build_ncpus $build_jobs" \
        --define "veejay_arch_target $arch_target" \
        "$topdir/SPECS/$(basename "$spec_source")"
    find "$topdir/RPMS" -type f -name '*.rpm' -exec cp -f {} /packages/ \;
}

install_packages() {
    dnf -y install "$@"
}

case "$start_package" in
    core)
        build_package core /src/veejay-core/veejay-core.spec /src/veejay-core/veejaycore-1.6.0.tar.gz veejay-core-1.6.0.tar.gz
        install_packages /packages/veejay-core-1.6.0-*.rpm /packages/veejay-core-devel-1.6.0-*.rpm
        ;&
    server)
        build_package server /src/veejay-server/veejay.spec /src/veejay-server/veejay-1.6.0.tar.gz veejay-1.6.0.tar.gz
        install_packages /packages/veejay-1.6.0-*.rpm /packages/veejay-devel-1.6.0-*.rpm
        ;&
    client)
        build_package client /src/veejay-client/veejay-client.spec /src/veejay-client/reloaded-1.6.0.tar.gz veejay-client-1.6.0.tar.gz
        ;&
    utils)
        build_package utils /src/veejay-utils/veejay-utils.spec /src/veejay-utils/veejay-utils-1.6.0.tar.gz veejay-utils-1.6.0.tar.gz
        ;&
    eidolon)
        build_package eidolon /src/veejay-eidolon/veejay-eidolon.spec /src/veejay-eidolon/veejay-eidolon-1.6.0.tar.gz veejay-eidolon-1.6.0.tar.gz
        ;&
    director)
        build_package director /src/veejay-director/veejay-director.spec /src/veejay-director/reloaded-1.6.0.tar.gz veejay-director-1.6.0.tar.gz
        ;;
    *)
        echo "Unknown start package: $start_package" >&2
        exit 2
        ;;
esac
