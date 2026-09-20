#!/bin/bash
# Copy libfluidsynth and everything it depends on into the app bundle, and
# rewrite the install names to point there, so the .app runs on a Mac that
# doesn't have homebrew.
#
# usage: ./vendor-dylibs.sh Jammer.app

set -e

APP="$1"
if [ -z "$APP" ]; then
    echo "usage: $0 Jammer.app"
    exit 1
fi

BIN="$APP/Contents/MacOS/jammer"
FRAMEWORKS="$APP/Contents/Frameworks"
mkdir -p "$FRAMEWORKS"

# Anything under these prefixes is ours to carry; /usr/lib and /System are part
# of macOS and are always there.
is_vendorable() {
    case "$1" in
        /opt/homebrew/*|/usr/local/*) return 0 ;;
        *) return 1 ;;
    esac
}

deps_of() {
    otool -L "$1" | tail -n +2 | awk '{print $1}'
}

# Breadth-first walk of the dependency graph, copying as we go.
to_process=()
for dep in $(deps_of "$BIN"); do
    if is_vendorable "$dep"; then to_process+=("$dep"); fi
done

# A library can be reached by more than one path (an opt/ symlink and the
# Cellar/ path behind it), so dedupe on the file name we'd copy it to.
declare -a copied=()
declare -a copied_names=()
already_copied() {
    local needle
    needle=$(basename "$1")
    for c in "${copied_names[@]}"; do
        if [ "$c" = "$needle" ]; then return 0; fi
    done
    return 1
}

while [ ${#to_process[@]} -gt 0 ]; do
    dep="${to_process[0]}"
    to_process=("${to_process[@]:1}")
    if already_copied "$dep"; then continue; fi
    base=$(basename "$dep")
    copied+=("$dep")
    copied_names+=("$base")
    cp -f "$dep" "$FRAMEWORKS/$base"
    chmod u+w "$FRAMEWORKS/$base"
    echo "  vendored $base"

    for sub in $(deps_of "$dep"); do
        if is_vendorable "$sub"; then to_process+=("$sub"); fi
    done
done

# Point everything at the bundled copies.
rewrite() {
    local target="$1"
    for dep in $(deps_of "$target"); do
        if is_vendorable "$dep"; then
            install_name_tool -change "$dep" \
                "@rpath/$(basename "$dep")" "$target" 2>/dev/null || true
        fi
    done
}

for dep in "${copied[@]}"; do
    lib="$FRAMEWORKS/$(basename "$dep")"
    install_name_tool -id "@rpath/$(basename "$dep")" "$lib" 2>/dev/null || true
    rewrite "$lib"
done

rewrite "$BIN"
install_name_tool -add_rpath "@executable_path/../Frameworks" "$BIN" \
    2>/dev/null || true

echo "  vendored ${#copied[@]} libraries"
