#!/usr/bin/env bash
set -euo pipefail

detect_jobs() {
    local jobs=""

    if command -v nproc >/dev/null 2>&1; then
        jobs="$(nproc)"
    elif command -v getconf >/dev/null 2>&1; then
        jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
    fi

    if [[ "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
        printf '%s\n' "${jobs}"
    else
        printf '1\n'
    fi
}

JOBS="${JOBS:-$(detect_jobs)}"
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_STATE_DIR="${ROOT_DIR}/.veejay-build-state"
CONFIGURE_ARGS=("$@")

mkdir -p "${BUILD_STATE_DIR}"

project_stamp() {
    local dir="$1"
    local key="${dir//\//_}"
    printf '%s/%s.built\n' "${BUILD_STATE_DIR}" "${key}"
}

was_built_before() {
    local dir="$1"
    local stamp

    stamp="$(project_stamp "${dir}")"

    [[ -f "${stamp}" ]] ||
    [[ -f config.status ]] ||
    [[ -n "$(find . -type f \( -name '*.o' -o -name '*.lo' -o -name '*.la' \) -print -quit)" ]]
}

clean_previous_build() {
    local dir="$1"

    if was_built_before "${dir}"; then
        if [[ -f Makefile || -f makefile || -f GNUmakefile ]]; then
            echo "==> Previous build detected; cleaning ${dir}"
            make clean
        else
            echo "==> Previous build detected for ${dir}, but no Makefile is available to clean"
        fi
    fi
}

mark_built() {
    local dir="$1"
    touch "$(project_stamp "${dir}")"
}

build_autogen_project() {
    local dir="$1"

    echo
    echo "==> Building ${dir}"
    cd "${ROOT_DIR}/${dir}"

    clean_previous_build "${dir}"
    ./autogen.sh
    ./configure "${CONFIGURE_ARGS[@]}"
    make -j"${JOBS}"
    sudo make install
    mark_built "${dir}"
}

build_make_project() {
    local dir="$1"

    echo
    echo "==> Building ${dir}"
    cd "${ROOT_DIR}/${dir}"

    clean_previous_build "${dir}"
    make -j"${JOBS}"
    sudo make install
    mark_built "${dir}"
}

echo "Using ${JOBS} parallel build jobs."

sudo -v

build_autogen_project "veejay-core"
build_autogen_project "veejay-server"
build_autogen_project "veejay-client"
build_autogen_project "veejay-utils"
build_autogen_project "veejay-eidolon"
build_autogen_project "veejay-director"

build_make_project "sendVIMS"

build_autogen_project "plugin-packs/lvdasciiart"
build_autogen_project "plugin-packs/lvdcrop"
build_autogen_project "plugin-packs/lvdgmic"
build_autogen_project "plugin-packs/lvdshared"

echo
echo "All VeeJay components built and installed successfully."
