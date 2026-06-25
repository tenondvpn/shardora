#!/usr/bin/env python3
"""
System-Level Optimization Application Script
============================================

This script applies system-level optimizations to the Shardora blockchain codebase:
1. Reduces unnecessary logging in high-frequency paths
2. Optimizes thread pool configurations
3. Sets CPU affinity for critical threads
4. Integrates the SystemOptimizer into the main application

Usage:
    python3 scripts/apply_system_optimizations.py [--dry-run] [--log-level INFO]
"""

import os
import re
import sys
import argparse
import shutil
from pathlib import Path
from typing import List, Dict, Tuple

class SystemOptimizationApplier:
    def __init__(self, root_path: str, dry_run: bool = False):
        self.root_path = Path(root_path)
        self.dry_run = dry_run
        self.changes_made = []
        
        # High-frequency logging patterns to optimize
        self.high_freq_patterns = [
            # Debug logs in hot paths
            (r'SHARDORA_DEBUG\(".*thread.*idx.*%d.*"', 'SHARDORA_DEBUG_FAST'),
            (r'SHARDORA_DEBUG\(".*message.*handled.*hash.*%lu.*"', 'SHARDORA_DEBUG_THROTTLED(1000'),
            (r'SHARDORA_DEBUG\(".*queue.*size.*%u.*"', 'SHARDORA_DEBUG_THROTTLED(5000'),
            (r'SHARDORA_DEBUG\(".*start.*message.*handled.*"', 'SHARDORA_DEBUG_FAST'),
            (r'SHARDORA_DEBUG\(".*end.*message.*handled.*"', 'SHARDORA_DEBUG_FAST'),
            (r'SHARDORA_DEBUG\(".*get.*thread.*index.*"', 'SHARDORA_DEBUG_FAST'),
            
            # Network and transport debug logs
            (r'SHARDORA_DEBUG\(".*tcp.*client.*"', 'SHARDORA_DEBUG_THROTTLED(2000'),
            (r'SHARDORA_DEBUG\(".*send.*failed.*"', 'SHARDORA_DEBUG_THROTTLED(1000'),
            (r'SHARDORA_DEBUG\(".*connection.*"', 'SHARDORA_DEBUG_THROTTLED(3000'),
            
            # Consensus debug logs
            (r'SHARDORA_DEBUG\(".*consensus.*"', 'SHARDORA_DEBUG_THROTTLED(1000'),
            (r'SHARDORA_DEBUG\(".*hotstuff.*"', 'SHARDORA_DEBUG_THROTTLED(1000'),
            (r'SHARDORA_DEBUG\(".*view.*block.*"', 'SHARDORA_DEBUG_THROTTLED(2000'),
        ]
        
        # Files to exclude from optimization (tests, examples, etc.)
        self.exclude_patterns = [
            r'.*test.*\.cc$',
            r'.*test.*\.h$',
            r'.*example.*\.cc$',
            r'.*benchmark.*\.cc$',
        ]

    def should_exclude_file(self, file_path: str) -> bool:
        """Check if file should be excluded from optimization."""
        for pattern in self.exclude_patterns:
            if re.match(pattern, file_path, re.IGNORECASE):
                return True
        return False

    def optimize_logging_in_file(self, file_path: Path) -> int:
        """Optimize logging in a single file."""
        if self.should_exclude_file(str(file_path)):
            return 0
            
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
        except IOError:
            return 0
            
        original_content = content
        changes = 0
        
        # Add include for system optimizer if SHARDORA_DEBUG is used
        if 'SHARDORA_DEBUG(' in content and '#include "common/system_optimizer.h"' not in content:
            # Find the last #include line
            include_lines = []
            other_lines = []
            in_includes = True
            
            for line in content.split('\n'):
                if line.strip().startswith('#include') and in_includes:
                    include_lines.append(line)
                else:
                    if in_includes and line.strip() and not line.strip().startswith('#'):
                        in_includes = False
                    other_lines.append(line)
            
            if include_lines:
                include_lines.append('#include "common/system_optimizer.h"')
                content = '\n'.join(include_lines) + '\n' + '\n'.join(other_lines)
                changes += 1
        
        # Apply high-frequency logging optimizations
        for pattern, replacement in self.high_freq_patterns:
            old_content = content
            if 'SHARDORA_DEBUG_THROTTLED' in replacement:
                # Replace with throttled version
                content = re.sub(pattern, lambda m: m.group(0).replace('SHARDORA_DEBUG(', replacement + '('), content)
            else:
                # Replace with fast version
                content = re.sub(pattern, lambda m: m.group(0).replace('SHARDORA_DEBUG(', replacement + '('), content)
            
            if content != old_content:
                changes += 1
        
        # Optimize specific high-frequency debug logs
        high_freq_replacements = [
            # Thread management logs
            ('SHARDORA_DEBUG("thread handler thread index coming', 'SHARDORA_DEBUG_FAST("thread handler thread index coming'),
            ('SHARDORA_DEBUG("waiting global init success', 'SHARDORA_DEBUG_FAST("waiting global init success'),
            ('SHARDORA_DEBUG("start message handled msg hash', 'SHARDORA_DEBUG_FAST("start message handled msg hash'),
            ('SHARDORA_DEBUG("begin message handled msg hash', 'SHARDORA_DEBUG_FAST("begin message handled msg hash'),
            ('SHARDORA_DEBUG("end message handled msg hash', 'SHARDORA_DEBUG_FAST("end message handled msg hash'),
            
            # Network logs
            ('SHARDORA_DEBUG("message coming hash64', 'SHARDORA_DEBUG_THROTTLED(1000, "message coming hash64'),
            ('SHARDORA_DEBUG("queue size message push success', 'SHARDORA_DEBUG_THROTTLED(2000, "queue size message push success'),
            
            # Consensus logs
            ('SHARDORA_DEBUG("get hotstuff message thread idx', 'SHARDORA_DEBUG_THROTTLED(1000, "get hotstuff message thread idx'),
        ]
        
        for old_log, new_log in high_freq_replacements:
            if old_log in content:
                content = content.replace(old_log, new_log)
                changes += 1
        
        # Write back if changes were made
        if changes > 0 and content != original_content:
            if not self.dry_run:
                with open(file_path, 'w', encoding='utf-8') as f:
                    f.write(content)
            
            self.changes_made.append(f"Optimized {changes} logging calls in {file_path}")
        
        return changes

    def integrate_system_optimizer(self) -> bool:
        """Integrate SystemOptimizer into the main application."""
        
        # 1. Update CMakeLists.txt to include system_optimizer
        cmake_file = self.root_path / "src" / "common" / "CMakeLists.txt"
        if cmake_file.exists():
            try:
                with open(cmake_file, 'r') as f:
                    content = f.read()
                
                if 'system_optimizer.cc' not in content:
                    # Find the source files section and add system_optimizer
                    if 'add_library(common' in content:
                        content = content.replace(
                            'add_library(common',
                            'add_library(common\n    system_optimizer.cc'
                        )
                    elif 'set(COMMON_SOURCES' in content:
                        content = content.replace(
                            'set(COMMON_SOURCES',
                            'set(COMMON_SOURCES\n    system_optimizer.cc'
                        )
                    else:
                        # Add at the end
                        content += '\n# System optimizer\ntarget_sources(common PRIVATE system_optimizer.cc)\n'
                    
                    if not self.dry_run:
                        with open(cmake_file, 'w') as f:
                            f.write(content)
                    
                    self.changes_made.append(f"Updated {cmake_file} to include system_optimizer")
                    return True
            except IOError:
                pass
        
        return False

    def optimize_thread_configurations(self) -> bool:
        """Optimize thread pool configurations in existing code."""
        
        # Update MultiThreadHandler to use SystemOptimizer
        multi_thread_file = self.root_path / "src" / "transport" / "multi_thread.cc"
        if multi_thread_file.exists():
            try:
                with open(multi_thread_file, 'r') as f:
                    content = f.read()
                
                # Add include for system optimizer
                if '#include "common/system_optimizer.h"' not in content:
                    content = content.replace(
                        '#include "transport/multi_thread.h"',
                        '#include "transport/multi_thread.h"\n#include "common/system_optimizer.h"'
                    )
                
                # Initialize system optimizer in MultiThreadHandler::Init
                if 'SystemOptimizer::Instance()->Initialize()' not in content:
                    init_pattern = r'(int MultiThreadHandler::Init.*?\{)'
                    replacement = r'\1\n    // Initialize system optimizations\n    SystemOptimizer::Instance()->Initialize();'
                    content = re.sub(init_pattern, replacement, content, flags=re.DOTALL)
                
                # Set CPU affinity for worker threads
                if 'SetCriticalThreadAffinity' not in content:
                    thread_start_pattern = r'(auto thread_handler = std::make_shared<ThreadHandler>\()'
                    replacement = r'// Set CPU affinity for critical threads\n        SystemOptimizer::Instance()->SetCriticalThreadAffinity();\n        \1'
                    content = re.sub(thread_start_pattern, replacement, content)
                
                if not self.dry_run:
                    with open(multi_thread_file, 'w') as f:
                        f.write(content)
                
                self.changes_made.append(f"Optimized thread configuration in {multi_thread_file}")
                return True
            except IOError:
                pass
        
        return False

    def add_runtime_log_level_control(self) -> bool:
        """Add runtime log level control to main applications."""
        
        # Update main applications to support log level control
        main_files = [
            self.root_path / "src" / "main" / "tx_cli.cc",
            self.root_path / "src" / "main" / "tnet_cli.cc",
            self.root_path / "src" / "main" / "tnet_svr.cc",
        ]
        
        for main_file in main_files:
            if main_file.exists():
                try:
                    with open(main_file, 'r') as f:
                        content = f.read()
                    
                    # Add include
                    if '#include "common/system_optimizer.h"' not in content:
                        content = content.replace(
                            '#include "common/global_info.h"',
                            '#include "common/global_info.h"\n#include "common/system_optimizer.h"'
                        )
                    
                    # Add log level initialization
                    if 'SetLogLevel' not in content:
                        # Find main function and add log level setup
                        main_pattern = r'(int main\(.*?\)\s*\{)'
                        replacement = r'\1\n    // Initialize system optimizations\n    SystemOptimizer::Instance()->Initialize();\n    SystemOptimizer::Instance()->SetLogLevel(LogLevel::INFO);'
                        content = re.sub(main_pattern, replacement, content, flags=re.DOTALL)
                    
                    if not self.dry_run:
                        with open(main_file, 'w') as f:
                            f.write(content)
                    
                    self.changes_made.append(f"Added runtime log control to {main_file}")
                except IOError:
                    continue
        
        return True

    def optimize_high_frequency_paths(self) -> int:
        """Optimize logging in high-frequency code paths."""
        
        total_changes = 0
        
        # Target directories with high-frequency code
        target_dirs = [
            "src/transport",
            "src/consensus",
            "src/pools",
            "src/network",
            "src/dht",
        ]
        
        for dir_name in target_dirs:
            dir_path = self.root_path / dir_name
            if dir_path.exists():
                for file_path in dir_path.rglob("*.cc"):
                    changes = self.optimize_logging_in_file(file_path)
                    total_changes += changes
                
                for file_path in dir_path.rglob("*.h"):
                    changes = self.optimize_logging_in_file(file_path)
                    total_changes += changes
        
        return total_changes

    def create_optimization_config(self) -> bool:
        """Create system optimization configuration file."""
        
        config_content = """# System Optimization Configuration
# ================================

# Logging Configuration
logging:
  # Runtime log level (DISABLED=0, FATAL=1, ERROR=2, WARN=3, INFO=4, DEBUG=5)
  default_level: 4  # INFO
  
  # High-frequency path optimizations
  throttle_intervals:
    thread_debug: 1000      # ms
    network_debug: 2000     # ms
    consensus_debug: 1000   # ms
    queue_debug: 5000       # ms
  
  # Completely disable debug logs in release builds
  disable_debug_in_release: true

# Thread Pool Configuration
thread_pools:
  consensus:
    core_threads: 16        # From hotstuff_thread_count
    max_threads: 32
    queue_size: 2048
    cpu_affinity: true
    thread_priority: 10     # Higher priority
    
  network:
    core_threads: 4         # From tcp_server_thread_count
    max_threads: 8
    queue_size: 1024
    cpu_affinity: true
    thread_priority: 5
    
  database:
    core_threads: 2
    max_threads: 4
    queue_size: 512
    cpu_affinity: true
    thread_priority: 0
    
  general:
    core_threads: 4
    max_threads: 8
    queue_size: 1024
    cpu_affinity: false
    thread_priority: 0

# CPU Affinity Configuration
cpu_affinity:
  enable: true
  
  # Core assignments by thread type
  core_assignments:
    consensus: [0, 1, 2, 3]      # First 4 cores for consensus
    network: [4, 5, 6, 7]       # Next 4 cores for network I/O
    database: [8, 9]             # Cores for database operations
    general: [10, 11, 12, 13, 14, 15]  # Remaining cores

# Memory Optimization
memory:
  enable_optimizations: true
  use_memory_pools: true
  optimize_allocations: true

# Performance Monitoring
monitoring:
  enable: true
  interval_seconds: 10
  collect_statistics: true
  auto_adjust: true
"""
        
        config_file = self.root_path / "system_optimization.yaml"
        if not self.dry_run:
            with open(config_file, 'w') as f:
                f.write(config_content)
        
        self.changes_made.append(f"Created system optimization config: {config_file}")
        return True

    def apply_all_optimizations(self) -> Dict[str, int]:
        """Apply all system optimizations."""
        
        results = {
            'logging_optimizations': 0,
            'thread_optimizations': 0,
            'integration_changes': 0,
            'config_files': 0,
        }
        
        print("Applying system-level optimizations...")
        
        # 1. Optimize high-frequency logging
        print("  - Optimizing high-frequency logging paths...")
        results['logging_optimizations'] = self.optimize_high_frequency_paths()
        
        # 2. Integrate SystemOptimizer
        print("  - Integrating SystemOptimizer...")
        if self.integrate_system_optimizer():
            results['integration_changes'] += 1
        
        # 3. Optimize thread configurations
        print("  - Optimizing thread pool configurations...")
        if self.optimize_thread_configurations():
            results['thread_optimizations'] += 1
        
        # 4. Add runtime log level control
        print("  - Adding runtime log level control...")
        if self.add_runtime_log_level_control():
            results['integration_changes'] += 1
        
        # 5. Create optimization configuration
        print("  - Creating optimization configuration...")
        if self.create_optimization_config():
            results['config_files'] += 1
        
        return results

    def print_summary(self, results: Dict[str, int]):
        """Print optimization summary."""
        
        print("\n" + "="*60)
        print("SYSTEM OPTIMIZATION SUMMARY")
        print("="*60)
        
        print(f"Logging optimizations applied: {results['logging_optimizations']}")
        print(f"Thread optimizations applied: {results['thread_optimizations']}")
        print(f"Integration changes made: {results['integration_changes']}")
        print(f"Configuration files created: {results['config_files']}")
        
        print(f"\nTotal changes made: {len(self.changes_made)}")
        
        if self.dry_run:
            print("\n[DRY RUN] No files were actually modified.")
        
        print("\nDetailed changes:")
        for change in self.changes_made:
            print(f"  - {change}")
        
        print("\n" + "="*60)
        print("NEXT STEPS:")
        print("="*60)
        print("1. Rebuild the project to include system optimizations")
        print("2. Test the optimized build for performance improvements")
        print("3. Monitor system resource usage and adjust configurations")
        print("4. Use SystemOptimizer::Instance()->SetLogLevel() to control logging at runtime")
        print("5. Check system_optimization.yaml for configuration options")

def main():
    parser = argparse.ArgumentParser(description="Apply system-level optimizations to Shardora blockchain")
    parser.add_argument("--dry-run", action="store_true", help="Show what would be changed without modifying files")
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARN", "ERROR"], help="Set log level")
    parser.add_argument("--root-path", default=".", help="Root path of the project")
    
    args = parser.parse_args()
    
    # Validate root path
    root_path = Path(args.root_path).resolve()
    if not (root_path / "src").exists():
        print(f"Error: {root_path} does not appear to be the project root (no src/ directory found)")
        sys.exit(1)
    
    # Apply optimizations
    applier = SystemOptimizationApplier(str(root_path), args.dry_run)
    results = applier.apply_all_optimizations()
    applier.print_summary(results)
    
    return 0

if __name__ == "__main__":
    sys.exit(main())