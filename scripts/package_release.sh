#!/usr/bin/env bash
#
# Build release artifacts for a stable or RC nexenne snapshot/module version:
#   dist/nexenne-X.Y.Z-source.tar.gz
#   dist/nexenne-X.Y.Z-cmake.tar.gz
#   dist/nexenne-<module>-X.Y.Z-source.tar.gz
#   dist/nexenne-<module>-X.Y.Z-cmake.tar.gz
#   dist/SHA256SUMS
set -euo pipefail

usage() {
    cat <<'USAGE'
usage: scripts/package_release.sh [--skip-checks] [--dist DIR] [--module NAME] X.Y.Z[-rc.N]

Without --module, packages the global snapshot version NEXENNE_VERSION.
With --module, packages a module release and checks
NEXENNE_MODULE_<NAME>_VERSION. RC suffixes are used only for tags, archive
names, and GitHub prereleases; CMake package versions remain the stable X.Y.Z
core.
USAGE
}

skip_checks=0
dist_dir="dist"
module=""
version=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        --skip-checks)
            skip_checks=1
            ;;
        --dist)
            shift
            if [ "$#" -eq 0 ]; then
                echo "package_release: --dist needs a directory" >&2
                exit 2
            fi
            dist_dir="$1"
            ;;
        --module)
            shift
            if [ "$#" -eq 0 ]; then
                echo "package_release: --module needs a module name" >&2
                exit 2
            fi
            module="$1"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -*)
            echo "package_release: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
        *)
            if [ -n "$version" ]; then
                echo "package_release: only one version argument is allowed" >&2
                exit 2
            fi
            version="$1"
            ;;
    esac
    shift
done

if [ -z "$version" ]; then
    usage >&2
    exit 2
fi

if ! printf '%s\n' "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+(-rc\.[0-9]+)?$'; then
    echo "package_release: version must be X.Y.Z or X.Y.Z-rc.N: $version" >&2
    exit 1
fi

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

module_var_name() {
    local name="$1"
    local upper
    upper="$(printf '%s\n' "$name" | tr '[:lower:]-' '[:upper:]_')"
    printf 'NEXENNE_MODULE_%s_VERSION\n' "$upper"
}

read_manifest_version() {
    local var_name="$1"
    sed -n "s/^set(${var_name}[[:space:]]*\"\\([^\"]*\\)\")$/\\1/p" cmake/nexenne_version.cmake
}

if [ -n "$module" ]; then
    if ! printf '%s\n' "$module" | grep -Eq '^[a-z][a-z0-9_]*$' || [ ! -d "modules/$module" ]; then
        echo "package_release: unknown module: $module" >&2
        exit 2
    fi
    var_name="$(module_var_name "$module")"
    artifact_base="nexenne-$module-$version"
else
    var_name="NEXENNE_VERSION"
    artifact_base="nexenne-$version"
fi

core_version="${version%%-*}"
cmake_version="$(read_manifest_version "$var_name")"
if [ "$cmake_version" != "$core_version" ]; then
    echo "package_release: $var_name is $cmake_version, expected $core_version" >&2
    exit 1
fi

if [ "${NEXENNE_ALLOW_DIRTY:-0}" != "1" ] && [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "package_release: tracked files are dirty; commit or set NEXENNE_ALLOW_DIRTY=1" >&2
    exit 1
fi

if [ "$skip_checks" = "0" ]; then
    bash scripts/install_smoke.sh
fi

rm -rf "$dist_dir"
mkdir -p "$dist_dir"

source_archive="$dist_dir/$artifact_base-source.tar.gz"
git archive --format=tar --prefix="$artifact_base/" HEAD | gzip -n > "$source_archive"

tmp_root="$(mktemp -d)"
build_dir="$tmp_root/build"
install_root="$tmp_root/$artifact_base"
cleanup() {
    rm -rf "$tmp_root"
}
trap cleanup EXIT

cmake -S . -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DNEXENNE_BUILD_TESTS=OFF \
    -DNEXENNE_BUILD_EXAMPLES=OFF \
    -DNEXENNE_BUILD_BENCHMARKS=OFF \
    -DNEXENNE_BUILD_DOCS=OFF \
    -DCMAKE_INSTALL_PREFIX="$install_root"
cmake --build "$build_dir"
cmake --install "$build_dir"

cmake_archive="$dist_dir/$artifact_base-cmake.tar.gz"
tar -C "$tmp_root" -czf "$cmake_archive" "$artifact_base"

(
    cd "$dist_dir"
    sha256sum "$artifact_base-source.tar.gz" "$artifact_base-cmake.tar.gz" > SHA256SUMS
)

echo "package_release: wrote artifacts to $dist_dir"
