#!/usr/bin/env bash
# argtable2 arg_int.c: GCC 13+ / C17 needs <stdbool.h> for bool; static isspace/toupper
# clash with <ctype.h> macros. Rename helpers before building libbls deps.
set -euo pipefail

ARG_INT="${1:-}"
if [[ -z "$ARG_INT" ]]; then
  ARG_INT="$(find "${SHARDORA_ROOT:-.}" -path '*/argtable2/src/arg_int.c' 2>/dev/null | head -1 || true)"
fi
if [[ -z "$ARG_INT" || ! -f "$ARG_INT" ]]; then
  echo "patch_argtable2_arg_int: arg_int.c not found (skip)" >&2
  exit 0
fi

if grep -q 'argtable_isspace_local' "$ARG_INT"; then
  echo "patch_argtable2_arg_int: already patched: $ARG_INT"
  exit 0
fi

if ! grep -q 'static bool isspace' "$ARG_INT"; then
  echo "patch_argtable2_arg_int: no static bool isspace in $ARG_INT (skip)"
  exit 0
fi

python3 - "$ARG_INT" <<'PY'
import re
import sys

path = sys.argv[1]
with open(path, encoding="utf-8", errors="replace") as f:
    text = f.read()

if "argtable_isspace_local" in text:
    sys.exit(0)

if "#include <stdbool.h>" not in text:
    if "#include <limits.h>" in text:
        text = text.replace(
            "#include <limits.h>",
            "#include <limits.h>\n#include <stdbool.h>\n#include <ctype.h>",
            1,
        )
    else:
        text = "#include <stdbool.h>\n#include <ctype.h>\n" + text

text = re.sub(r"\bstatic bool isspace\b", "static bool argtable_isspace_local", text)
text = re.sub(r"\bstatic char toupper\b", "static char argtable_toupper_local", text)
text = re.sub(r"\bisspace\s*\(", "argtable_isspace_local(", text)
text = re.sub(r"\btoupper\s*\(", "argtable_toupper_local(", text)

with open(path, "w", encoding="utf-8") as f:
    f.write(text)

print(f"patch_argtable2_arg_int: patched {path}")
PY
