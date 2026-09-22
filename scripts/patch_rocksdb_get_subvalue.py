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
  //   - Returns NotFound if key does not exist.
  //   - Returns InvalidArgument if offset >= value.size().
  //   - If offset+length > value.size() the available suffix is returned.
  // This is a convenience wrapper around Get(); it reads the full value and
  // then extracts the requested range.  Suitable for KB-sized values such as
  // committed HotStuff blocks used by the PoRA storage-replication challenge.
  Status GetSubValue(const ReadOptions& options,
                     ColumnFamilyHandle* column_family, const Slice& key,
                     size_t offset, size_t length, std::string* out) {
    std::string full_value;
    Status s = Get(options, column_family, key, &full_value);
    if (!s.ok()) return s;
    if (offset >= full_value.size()) {
      return Status::InvalidArgument("GetSubValue: offset out of range");
    }
    const size_t actual_len =
        (length < full_value.size() - offset) ? length
                                               : (full_value.size() - offset);
    out->assign(full_value.data() + offset, actual_len);
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
