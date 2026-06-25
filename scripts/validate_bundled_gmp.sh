#!/usr/bin/env bash
# Quarantine libgmp.a / libgmpxx.a in deps_inst when built for another CPU arch.
# Syncing deps_inst from macOS (or another machine) leaves ELF objects that Linux
# cannot link; -lgmp then resolves to the broken archive before system libgmp.
set -euo pipefail

quarantine_if_mismatch() {
    local lib="$1"
    [[ -f "$lib" ]] || return 0
    [[ "$lib" == *.wrong_arch ]] && return 0

    local obj file_out host_arch
    host_arch="$(uname -m)"
    obj="$(ar t "$lib" 2>/dev/null | head -1 || true)"
    [[ -n "$obj" ]] || return 0

    local tmpdir
    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' RETURN
    ar p "$lib" "$obj" >"$tmpdir/$obj" 2>/dev/null || return 0
    file_out="$(file -b "$tmpdir/$obj")"

    local ok=0
    case "$host_arch" in
        x86_64|amd64)
            [[ "$file_out" == *x86-64* ]] && ok=1
            ;;
        aarch64|arm64)
            [[ "$file_out" == *aarch64* || "$file_out" == *ARM64* ]] && ok=1
            ;;
        *)
            ok=1
            ;;
    esac

    if [[ "$ok" -eq 0 ]]; then
        echo "validate_bundled_gmp: quarantining wrong-arch $(basename "$lib") -> ${lib}.wrong_arch"
        mv -f "$lib" "${lib}.wrong_arch"
    fi
}

for lib in "$@"; do
    quarantine_if_mismatch "$lib"
done
