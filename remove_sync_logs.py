#!/usr/bin/env python3
"""
Remove all [SYNC] related logging and comments from the codebase.
This includes:
- [SYNC_PERF] logging
- [SYNC_GAP] logging
- [SyncFinish] logging
- [SYNC_OPT] comments
"""

import os
import re
import sys

def remove_sync_logs_from_file(filepath):
    """Remove SYNC related logs from a single file."""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading {filepath}: {e}")
        return False
    
    original_content = content
    
    # Pattern 1: Remove [SYNC_PERF] SHARDORA_WARN/DEBUG lines
    # Matches: SHARDORA_WARN("[SYNC_PERF] ... ", ...);
    content = re.sub(
        r'\s*SHARDORA_WARN\(\s*"\[SYNC_PERF\][^"]*"[^)]*\);?\n?',
        '',
        content
    )
    content = re.sub(
        r'\s*SHARDORA_DEBUG\(\s*"\[SYNC_PERF\][^"]*"[^)]*\);?\n?',
        '',
        content
    )
    
    # Pattern 2: Remove [SYNC_GAP] SHARDORA_WARN lines
    # Matches: SHARDORA_WARN("[SYNC_GAP] ... ", ...);
    content = re.sub(
        r'\s*SHARDORA_WARN\(\s*"\[SYNC_GAP\][^"]*"[^)]*\);?\n?',
        '',
        content
    )
    
    # Pattern 3: Remove [SyncFinish] BLS_DEBUG/INFO lines
    # Matches: BLS_DEBUG("[SyncFinish] ... ", ...);
    content = re.sub(
        r'\s*BLS_DEBUG\(\s*"\[SyncFinish\][^"]*"[^)]*\);?\n?',
        '',
        content
    )
    content = re.sub(
        r'\s*BLS_INFO\(\s*"\[SyncFinish\][^"]*"[^)]*\);?\n?',
        '',
        content
    )
    
    # Pattern 4: Remove [SYNC_OPT] comments
    # Matches: // [SYNC_OPT] ... (single line)
    content = re.sub(
        r'\s*//\s*\[SYNC_OPT\][^\n]*\n',
        '',
        content
    )
    
    # Pattern 5: Remove multi-line [SYNC_OPT] comments
    # Matches: /* [SYNC_OPT] ... */ or // [SYNC_OPT] ... (multiple lines)
    content = re.sub(
        r'/\*\s*\[SYNC_OPT\][^*]*\*/',
        '',
        content
    )
    
    # Clean up multiple consecutive blank lines
    content = re.sub(r'\n\n\n+', '\n\n', content)
    
    if content != original_content:
        try:
            with open(filepath, 'w', encoding='utf-8') as f:
                f.write(content)
            print(f"✓ Updated: {filepath}")
            return True
        except Exception as e:
            print(f"Error writing {filepath}: {e}")
            return False
    else:
        print(f"- No changes: {filepath}")
        return False

def main():
    """Main function to process all .cc files."""
    files_to_process = [
        'src/sync/key_value_sync.cc',
        'src/consensus/hotstuff/view_block_chain.cc',
        'src/bls/bls_manager.cc',
    ]
    
    updated_count = 0
    for filepath in files_to_process:
        if os.path.exists(filepath):
            if remove_sync_logs_from_file(filepath):
                updated_count += 1
        else:
            print(f"✗ File not found: {filepath}")
    
    print(f"\nTotal files updated: {updated_count}")

if __name__ == '__main__':
    main()
