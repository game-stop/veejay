#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${VEEJAY_SOURCE_ROOT:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)}"

[[ $# -eq 2 ]] || {
    printf 'usage: %s DOWNLOADED_ARTIFACTS OUTPUT_DIR\n' "$0" >&2
    exit 2
}

artifact_root="$1"
output_dir="$2"
matrix_file="${ROOT_DIR}/packaging/ci/release-matrix.json"
version="$("${ROOT_DIR}/packaging/ci/release-version.sh" check)"

for command_name in jq sha256sum unzip zip; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf "ERROR: required command '%s' was not found\n" "$command_name" >&2
        exit 127
    }
done

[[ -d "$artifact_root" ]] || {
    printf "ERROR: downloaded artifacts directory '%s' is missing\n" "$artifact_root" >&2
    exit 1
}
artifact_root="$(cd -- "$artifact_root" && pwd -P)"

mkdir -p -- "$(dirname -- "$output_dir")"
output_dir="$(cd -- "$(dirname -- "$output_dir")" && pwd -P)/$(basename -- "$output_dir")"
[[ "$output_dir" != / ]] || {
    printf 'ERROR: refusing to use the filesystem root as output directory\n' >&2
    exit 2
}

rm -rf -- "$output_dir"
mkdir -p "$output_dir"

work_root="$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/veejay-release-assets.XXXXXX")"
trap 'rm -rf -- "$work_root"' EXIT
manifest_tmp="${work_root}/RELEASE-MANIFEST.txt"

printf 'VeeJay %s distro release archives\n\n' "$version" > "$manifest_tmp"
printf 'format\tdistro\trelease_target\tsuite_target\tvariant\tpackage_file\trelease_asset\n' >> "$manifest_tmp"

suite_count=0
package_count=0
while IFS=$'\t' read -r format release_target suite_target variant environment; do
    artifact_name="packages-${format}-${suite_target}"
    suite_dir="${artifact_root}/${artifact_name}"
    [[ -d "$suite_dir" ]] || {
        printf "ERROR: downloaded artifact '%s' is missing\n" "$artifact_name" >&2
        exit 1
    }

    base_environment="${environment%%+*}"
    case "$format" in
        deb)
            distro="${base_environment%-arm}"
            find_command=(find "$suite_dir" -type f \( -name '*.deb' -o -name '*.ddeb' \) -print0)
            ;;
        rpm)
            distro="$base_environment"
            find_command=(find "$suite_dir" -type f -name '*.rpm' ! -name '*.src.rpm' -print0)
            ;;
        arch)
            distro="$base_environment"
            find_command=(find "$suite_dir" -type f -name '*.pkg.tar.*' ! -name '*.sig' -print0)
            ;;
        *)
            printf "ERROR: unknown matrix format '%s'\n" "$format" >&2
            exit 2
            ;;
    esac

    [[ "$distro" =~ ^[[:alnum:]][[:alnum:].-]*$ ]] || {
        printf "ERROR: build environment '%s' produced invalid distro label '%s'\n" \
            "$environment" "$distro" >&2
        exit 2
    }

    archive_name="veejay-${version}-${distro}-${release_target}-${variant}.zip"
    archive_path="${output_dir}/${archive_name}"
    suite_stage="${work_root}/${format}-${suite_target}"
    [[ ! -e "$archive_path" ]] || {
        printf "ERROR: duplicate release archive '%s'\n" "$archive_name" >&2
        exit 1
    }
    mkdir -p "$suite_stage"

    suite_packages=0
    while IFS= read -r -d '' package; do
        original_name="${package##*/}"
        [[ ! -e "${suite_stage}/${original_name}" ]] || {
            printf "ERROR: duplicate package '%s' in artifact '%s'\n" \
                "$original_name" "$artifact_name" >&2
            exit 1
        }
        cp -f -- "$package" "${suite_stage}/${original_name}"
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$format" "$distro" "$release_target" "$suite_target" "$variant" \
            "$original_name" "$archive_name" >> "$manifest_tmp"
        suite_packages=$((suite_packages + 1))
        package_count=$((package_count + 1))
    done < <("${find_command[@]}" | sort -z)

    ((suite_packages > 0)) || {
        printf "ERROR: artifact '%s' contains no packages\n" "$artifact_name" >&2
        exit 1
    }

    mapfile -t profiles < <(find "$suite_dir" -type f -name BUILD-PROFILE.txt -print)
    ((${#profiles[@]} == 1)) || {
        printf "ERROR: artifact '%s' must contain exactly one BUILD-PROFILE.txt\n" "$artifact_name" >&2
        exit 1
    }
    grep -qx "format=${format}" "${profiles[0]}"
    grep -qx "release_target=${release_target}" "${profiles[0]}"
    grep -qx "suite_target=${suite_target}" "${profiles[0]}"
    grep -qx "variant=${variant}" "${profiles[0]}"
    cp -f -- "${profiles[0]}" "${suite_stage}/BUILD-PROFILE.txt"

    for report_name in NVJPEG-CONFIG-VERIFICATION.txt NVJPEG-PACKAGE-VERIFICATION.txt; do
        mapfile -t reports < <(find "$suite_dir" -type f -name "$report_name" -print)
        ((${#reports[@]} == 1)) || {
            printf "ERROR: artifact '%s' must contain exactly one %s\n" \
                "$artifact_name" "$report_name" >&2
            exit 1
        }
        cp -f -- "${reports[0]}" "${suite_stage}/${report_name}"
    done

    (
        cd "$suite_stage"
        find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%P\0' | \
            sort -z | xargs -0 sha256sum
    ) > "${suite_stage}/SHA256SUMS"

    (
        cd "$suite_stage"
        mapfile -d '' -t archive_files < <(
            find . -maxdepth 1 -type f -printf '%P\0' | sort -z
        )
        zip -q -0 -X "$archive_path" -- "${archive_files[@]}"
    )
    unzip -tq "$archive_path" >/dev/null
    suite_count=$((suite_count + 1))
done < <(
    jq -r '.include[] | [.format, .release_target, .suite_target, .variant, .environment] | @tsv' \
        "$matrix_file"
)

expected_suites="$(jq '.include | length' "$matrix_file")"
((suite_count == expected_suites)) || {
    printf 'ERROR: staged %d suites; expected %d\n' "$suite_count" "$expected_suites" >&2
    exit 1
}

mv -f -- "$manifest_tmp" "${output_dir}/RELEASE-MANIFEST.txt"
(
    cd "$output_dir"
    find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%P\0' | \
        sort -z | xargs -0 sha256sum
) > "${output_dir}/SHA256SUMS"

printf 'Staged %d packages in %d release archives\n' "$package_count" "$suite_count"
