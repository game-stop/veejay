#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${VEEJAY_SOURCE_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)}"
REPO_ROOT="$(git -c safe.directory="${ROOT_DIR}/.." -C "$ROOT_DIR" rev-parse --show-toplevel)"
SOURCE_COMMIT="${SOURCE_COMMIT:-HEAD}"
VERSION="$("${ROOT_DIR}/packaging/ci/release-version.sh" check)"
SOURCE_PREFIX="$(realpath --relative-to="$REPO_ROOT" "$ROOT_DIR")"

git_repo() {
    git -c safe.directory="$REPO_ROOT" -C "$REPO_ROOT" "$@"
}

git_repo rev-parse --verify "${SOURCE_COMMIT}^{commit}" >/dev/null

create_archive() {
    local project="$1"
    local dist_name="$2"
    local destination="${ROOT_DIR}/${project}/${dist_name}-${VERSION}.tar.gz"
    local temporary="${destination}.tmp"

    git_repo archive --format=tar --prefix="${dist_name}-${VERSION}/" \
        "${SOURCE_COMMIT}:${SOURCE_PREFIX}/${project}" | gzip -n > "$temporary"
    mv -f -- "$temporary" "$destination"
    printf '%s\n' "$destination"
}

create_archive veejay-core veejaycore
create_archive veejay-server veejay
create_archive veejay-client reloaded
create_archive veejay-utils veejay-utils
create_archive veejay-eidolon veejay-eidolon
create_archive veejay-director reloaded
create_archive sendVIMS veejay-puredata
