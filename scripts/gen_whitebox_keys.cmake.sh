#!/usr/bin/env bash
# Writes a CMake fragment: set(REPLACE_WHITEBOX_PK ...) / SK for init whitebox crypto.
set -euo pipefail
out="${1:?output .cmake path}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

openssl genpkey -algorithm x25519 -out "$tmp/key.pem" 2>/dev/null
sk="$(openssl pkey -in "$tmp/key.pem" -outform DER | tail -c 32 | xxd -p -c 32)"
pk="$(openssl pkey -in "$tmp/key.pem" -pubout -outform DER | tail -c 32 | xxd -p -c 32)"

fmt_c_array() {
  echo "$(echo "$1" | sed 's/../0x&,/g' | sed 's/,$//')"
}

pk_a="$(fmt_c_array "$pk")"
sk_a="$(fmt_c_array "$sk")"

cat >"$out" <<EOF
# Auto-generated; do not commit secrets from production builds.
set(REPLACE_WHITEBOX_PK "${pk_a}" CACHE STRING "whitebox public key (C array)" FORCE)
set(REPLACE_WHITEBOX_SK "${sk_a}" CACHE STRING "whitebox secret key (C array)" FORCE)
EOF
