#!/usr/bin/env bash
#
# Scaffold a new nexenne module by copying template/module/ into modules/<name>/
# and substituting the __MODULE__ placeholder in file paths and contents.
#
# Usage:
#   ./template/new-module.sh <module-name>
#
# The module name must be lowercase snake_case. The script refuses to
# overwrite an existing module directory.
#
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: new-module.sh <module-name>

Scaffolds a new nexenne module by copying template/module/ into modules/<name>/
and substituting the __MODULE__ placeholder.

Constraints:
  - <module-name> must match [a-z][a-z0-9_]*
  - modules/<name>/ must not already exist
EOF
}

if [[ $# -ne 1 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 1
fi

name="$1"

if [[ ! "$name" =~ ^[a-z][a-z0-9_]*$ ]]; then
    echo "error: module name must match [a-z][a-z0-9_]*, got: $name" >&2
    exit 1
fi

if [[ "$name" == "module" || "$name" == "all" || "$name" == "nexenne" ]]; then
    echo "error: '$name' is a reserved name" >&2
    exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
template_dir="$script_dir/module"
target_dir="$repo_root/modules/$name"

if [[ ! -d "$template_dir" ]]; then
    echo "error: template not found at $template_dir" >&2
    exit 1
fi

if [[ -e "$target_dir" ]]; then
    echo "error: target already exists: $target_dir" >&2
    exit 1
fi

cp -R "$template_dir" "$target_dir"

# Rename placeholder dirs and files (deepest-first so parents are still valid).
while IFS= read -r -d '' path; do
    new_path="$(dirname "$path")/${name}$(basename "$path" | sed 's/^__MODULE__//')"
    mv "$path" "$new_path"
done < <(find "$target_dir" -depth -name '__MODULE__*' -print0)

# Substitute placeholder inside file contents.
find "$target_dir" -type f -print0 \
    | xargs -0 sed -i "s/__MODULE__/${name}/g"

cat <<EOF
created module nexenne::${name} at modules/${name}/

next steps:
  1. edit modules/${name}/include/nexenne/${name}/example.hpp
     (delete the placeholder and add your real public API)
  2. update modules/${name}/CMakeLists.txt:
       - the DESCRIPTION string
       - add inter-module deps via 'DEPENDS <name>' if needed
       - keep the HEADERS list in sync with files you add
  3. add real tests in modules/${name}/tests/
  4. configure and verify:
       cmake --preset dev
       cmake --build --preset dev
       ctest --preset dev
EOF
