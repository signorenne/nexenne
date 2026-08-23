#!/usr/bin/env bash
#
# Repo consistency gate. The build discovers modules from the filesystem, but
# the docs and the umbrella headers are written by hand, so they can fall behind
# a new module or a new header without anything failing. This checks the places
# that cannot check themselves:
#
#   - every module is listed in doc/README.org and doc/module/README.org,
#   - every module has a doc index and an examples directory,
#   - every module's catalogue entry states the deps its module.deps declares,
#   - every public header is reachable from its module umbrella header,
#   - every source under src/ is listed in the module's SOURCES.
#
# Install manifests are not checked here: they are discovered by CMake, and
# scripts/install_smoke.sh compiles the installed result.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

failures=0
module_ok=1
fail() {
    echo "  FAIL $*" >&2
    failures=$((failures + 1))
    module_ok=0
}

for dir in modules/*/; do
    mod="$(basename "$dir")"
    module_ok=1

    grep -q "file:module/${mod}/README.org" doc/README.org \
        || fail "$mod: no row in the doc/README.org module index"
    grep -q "file:${mod}/README.org" doc/module/README.org \
        || fail "$mod: no entry in the doc/module/README.org catalogue"
    [ -f "doc/module/${mod}/README.org" ] \
        || fail "$mod: doc/module/${mod}/README.org is missing"
    [ -d "examples/${mod}" ] \
        || fail "$mod: examples/${mod}/ is missing"

    # Catalogue entries annotate dependencies as "(needs =a=, =b=)" or
    # "(no dependencies)". module.deps is the source of truth.
    # A module with no dependencies ships no module.deps file at all.
    declared=""
    if [ -f "${dir}module.deps" ]; then
        declared="$(sed '/^$/d' "${dir}module.deps" | sort -u | tr '\n' ' ')"
    fi
    entry="$(sed -n "s/^- \[\[file:${mod}\/README\.org\]\[${mod}\]\] (\([^)]*\)).*/\1/p" \
        doc/module/README.org | head -n 1)"
    if [ -n "$entry" ]; then
        case "$entry" in
            *"no dependencies"*) stated="" ;;
            *) stated="$(printf '%s\n' "$entry" | grep -oE '=[a-z_]+=' \
                | tr -d '=' | sort -u | tr '\n' ' ')" ;;
        esac
        [ "$stated" = "$declared" ] \
            || fail "$mod: catalogue says deps [${stated% }], module.deps says [${declared% }]"
    fi

    # Every public header should be reachable from the module umbrella. A header
    # deliberately left out (logging's ESP-IDF sink) still has to be named there,
    # so the omission is a documented decision rather than an oversight.
    umbrella="${dir}include/nexenne/${mod}/${mod}.hpp"
    if [ -f "$umbrella" ]; then
        while IFS= read -r header; do
            base="$(basename "$header")"
            [ "$base" = "${mod}.hpp" ] && continue
            grep -q "$base" "$umbrella" \
                || fail "$mod: ${base} is not mentioned in ${mod}.hpp"
        done < <(find "${dir}include" -name '*.hpp' | sort)
    else
        fail "$mod: umbrella header ${umbrella} is missing"
    fi

    # HEADERS is discovered from the filesystem, SOURCES is hand-listed, and
    # nothing else notices a source that never made it into the list: it simply
    # is not compiled, and the first sign is an undefined reference in whatever
    # consumes it. Compare the two directly.
    if [ -d "${dir}src" ]; then
        while IFS= read -r source; do
            rel="${source#"$dir"}"
            grep -qF "$rel" "${dir}CMakeLists.txt" \
                || fail "$mod: ${rel} is not listed in SOURCES"
        done < <(find "${dir}src" -name '*.cpp' | sort)
    fi

    [ "$module_ok" = "1" ] && echo "  $mod: ok"
done

if [ "$failures" -gt 0 ]; then
    echo "check_sync: $failures problem(s) found" >&2
    exit 1
fi
echo "check_sync: OK"
