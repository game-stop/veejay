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
manifest_tmp="${output_dir}.manifest.tmp"

command -v jq >/dev/null 2>&1 || {
    printf "ERROR: required command 'jq' was not found\n" >&2
    exit 127
}

rm -rf -- "$output_dir"
mkdir -p "$output_dir"
printf 'VeeJay %s distro release assets\n\n' "$version" > "$manifest_tmp"
printf 'format\tsuite_target\tvariant\tpackage_file\trelease_asset\n' >> "$manifest_tmp"

suite_count=0
package_count=0
while IFS=$'\t' read -r format release_target suite_target variant; do
    artifact_name="packages-${format}-${suite_target}"
    suite_dir="${artifact_root}/${artifact_name}"
    [[ -d "$suite_dir" ]] || {
        printf "ERROR: downloaded artifact '%s' is missing\n" "$artifact_name" >&2
        exit 1
    }

    case "$format" in
        deb)
            find_command=(find "$suite_dir" -type f \( -name '*.deb' -o -name '*.ddeb' \) -print0)
            ;;
        rpm)
            find_command=(find "$suite_dir" -type f -name '*.rpm' ! -name '*.src.rpm' -print0)
            ;;
        arch)
            find_command=(find "$suite_dir" -type f -name '*.pkg.tar.*' ! -name '*.sig' -print0)
            ;;
        *)
            printf "ERROR: unknown matrix format '%s'\n" "$format" >&2
            exit 2
            ;;
    esac

    suite_packages=0
    while IFS= read -r -d '' package; do
        original_name="${package##*/}"
        asset_name="${format}-${suite_target}--${original_name}"
        [[ ! -e "${output_dir}/${asset_name}" ]] || {
            printf "ERROR: duplicate release asset '%s'\n" "$asset_name" >&2
            exit 1
        }
        cp -f -- "$package" "${output_dir}/${asset_name}"
        printf '%s\t%s\t%s\t%s\t%s\n' \
            "$format" "$suite_target" "$variant" "$original_name" "$asset_name" >> "$manifest_tmp"
        suite_packages=$((suite_packages + 1))
        package_count=$((package_count + 1))
    done < <("${find_command[@]}")

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
    cp -f -- "${profiles[0]}" \
        "${output_dir}/${format}-${suite_target}--BUILD-PROFILE.txt"

    for report_name in NVJPEG-CONFIG-VERIFICATION.txt NVJPEG-PACKAGE-VERIFICATION.txt; do
        mapfile -t reports < <(find "$suite_dir" -type f -name "$report_name" -print)
        ((${#reports[@]} == 1)) || {
            printf "ERROR: artifact '%s' must contain exactly one %s\n" \
                "$artifact_name" "$report_name" >&2
            exit 1
        }
        cp -f -- "${reports[0]}" \
            "${output_dir}/${format}-${suite_target}--${report_name}"
    done
    suite_count=$((suite_count + 1))
done < <(jq -r '.include[] | [.format, .release_target, .suite_target, .variant] | @tsv' "$matrix_file")

expected_suites="$(jq '.include | length' "$matrix_file")"
((suite_count == expected_suites)) || {
    printf 'ERROR: staged %d suites; expected %d\n' "$suite_count" "$expected_suites" >&2
    exit 1
}

mv -f -- "$manifest_tmp" "${output_dir}/RELEASE-MANIFEST.txt"
(
    cd "$output_dir"
    find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 | \
        sort -z | xargs -0 sha256sum
) > "${output_dir}/SHA256SUMS"

printf 'Staged %d packages from %d build suites\n' "$package_count" "$suite_count"
