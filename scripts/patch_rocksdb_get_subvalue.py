#!/usr/bin/env python3
"""
Patch rocksdb/include/rocksdb/db.h to add GetSubValue(key, offset, length, out).

Injects two inline non-virtual methods right after the last default-CF Get()
override, so they are available to any code that holds a rocksdb::DB*.

Usage:
    python3 patch_rocksdb_get_subvalue.py <path/to/include/rocksdb/db.h>
"""

import sys
import re

PATCH_MARKER = "// shardora-patch: GetSubValue"

NEW_METHODS = r"""
  // shardora-patch: GetSubValue
  // Read bytes [offset, offset+length) from the stored value of key.
  // Always safe: offset and length are clamped to the actual value size.
  //   - Returns NotFound if key does not exist.
  //   - If offset >= value.size(), returns ok() with out->clear().
  //   - If offset+length > value.size(), length is clamped to the available suffix.
  Status GetSubValue(const ReadOptions& options,
                     ColumnFamilyHandle* column_family, const Slice& key,
                     size_t offset, size_t length, std::string* out) {
    out->clear();
    std::string full_value;
    Status s = Get(options, column_family, key, &full_value);
    if (!s.ok()) return s;
    const size_t val_size = full_value.size();
    if (offset >= val_size || length == 0) {
      return s;  // ok() with empty out
    }
    const size_t available = val_size - offset;
    const size_t safe_len  = (length <= available) ? length : available;
    out->assign(full_value.data() + offset, safe_len);
    return s;
  }

  // Convenience overload using the default column family.
  Status GetSubValue(const ReadOptions& options, const Slice& key,
                     size_t offset, size_t length, std::string* out) {
    return GetSubValue(options, DefaultColumnFamily(), key,
                       offset, length, out);
  }

"""

def patch(db_h_path: str) -> None:
    with open(db_h_path, "r", encoding="utf-8") as f:
        content = f.read()

    if PATCH_MARKER in content:
        print(f"[rocksdb patch] Already applied: {db_h_path}")
        return

    # Anchor: the last default-CF Get() that forwards with timestamp parameter.
    # In RocksDB 9.x this block looks like:
    #
    #   virtual Status Get(const ReadOptions& options, const Slice& key,
    #                      std::string* value, std::string* timestamp) final {
    #     return Get(options, DefaultColumnFamily(), key, value, timestamp);
    #   }
    #
    # We insert NEW_METHODS right after the closing brace of this method,
    # before whatever follows (GetEntity / MultiGet / etc.).
    anchor_pattern = re.compile(
        r'(virtual\s+Status\s+Get\s*\([^)]*std::string\s*\*\s*timestamp\s*\)\s*final\s*\{'
        r'[^}]*\})',
        re.DOTALL
    )

    matches = list(anchor_pattern.finditer(content))
    if not matches:
        sys.exit(
            f"[rocksdb patch] ERROR: anchor Get(timestamp) not found in {db_h_path}\n"
            "This patch targets RocksDB >= 9.x; check the commit hash."
        )

    # Use the *last* match (default-CF forwarding overload).
    last_match = matches[-1]
    insert_pos = last_match.end()

    patched = content[:insert_pos] + NEW_METHODS + content[insert_pos:]
    with open(db_h_path, "w", encoding="utf-8") as f:
        f.write(patched)
    print(f"[rocksdb patch] GetSubValue injected into {db_h_path}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(f"Usage: {sys.argv[0]} <path/to/include/rocksdb/db.h>")
    patch(sys.argv[1])
