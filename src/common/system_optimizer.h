#pragma once

#include <atomic>
#include <thread>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <queue>
#include <functional>
#include <condition_variable>

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#elif defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <sys/resource.h>
#else
#include <pthread.h>
#include <unistd.h>
#include <sys/resource.h>
#endif

#include "common/log.h"
#include "common/utils.h"

namespace shardora {
namespace common {

// Log level control for runtime optimization
enum class LogLevel : uint8_t {
    DISABLED = 0,
    FATAL = 1,
    ERROR = 2,
    WARN = 3,
    INFO = 4,
    DEBUG = 5
};

// Thread pool configuration
struct ThreadPoolConfig {
    uint32_t core_threads = 4;           // Core thread count
    uint32_t max_threads = 8;            // Maximum thread count
    uint32_t queue_size = 1024;          // Task queue size
    uint32_t keep_alive_ms = 60000;      // Thread keep-alive time
    bool enable_cpu_affinity = true;     // Enable CPU affinity
    std::vector<int> cpu_cores;          // Specific CPU cores to bind
    int thread_priority = 0;             // Thread priority (-20 to 19 on Linux)
};

// CPU affinity manager
class CpuAffinityManager {
public:
    static CpuAffinityManager* Instance();
    
    // Set CPU affinity for current thread
    bool SetThreadAffinity(const std::vector<int>& cpu_cores);
    
    // Set CPU affinity for specific thread
    bool SetThreadAffinity(std::thread::id thread_id, const std::vector<int>& cpu_cores);
    
    // Get optimal CPU cores for thread type
    std::vector<int> GetOptimalCores(const std::string& thread_type);
    
    // Initialize CPU topology
    bool InitializeCpuTopology();
    
    // Get CPU count
    int GetCpuCount() const { return cpu_count_; }
    
    // Get NUMA node count
    int GetNumaNodeCount() const { return numa_node_count_; }

private:
    CpuAffinityManager();
    ~CpuAffinityManager() = default;
    
    int cpu_count_ = 0;
    int numa_node_count_ = 0;
    std::unordered_map<std::string, std::vector<int>> thread_type_cores_;
    std::mutex mutex_;
    
    DISALLOW_COPY_AND_ASSIGN(CpuAffinityManager);
};

// Optimized thread pool with CPU affinity
class OptimizedThreadPool {
public:
    OptimizedThreadPool(const ThreadPoolConfig& config);
    ~OptimizedThreadPool();
    
    // Initialize thread pool
    bool Initialize();
    
    // Shutdown thread pool
    void Shutdown();
    
    // Submit task to thread pool
    template<typename F>
    bool Submit(F&& task);
    
    // Get current thread count
    size_t GetThreadCount() const { return threads_.size(); }
    
    // Get pending task count
    size_t GetPendingTaskCount() const;
    
    // Get thread pool statistics
    struct Statistics {
        uint64_t tasks_submitted = 0;
        uint64_t tasks_completed = 0;
        uint64_t tasks_rejected = 0;
        uint32_t active_threads = 0;
        uint32_t idle_threads = 0;
    };
    
    Statistics GetStatistics() const;

private:
    void WorkerThread(int thread_id);
    bool SetWorkerAffinity(int thread_id);
    
    ThreadPoolConfig config_;
    std::vector<std::unique_ptr<std::thread>> threads_;
    std::queue<std::function<void()>> task_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::atomic<bool> shutdown_{false};
    
    // Statistics
    mutable std::mutex stats_mutex_;
    Statistics stats_;
    
    DISALLOW_COPY_AND_ASSIGN(OptimizedThreadPool);
};

// System-level optimizer
class SystemOptimizer {
public:
    static SystemOptimizer* Instance();
    
    // Initialize system optimizations
    bool Initialize();
    
    // Set global log level for runtime optimization
    void SetLogLevel(LogLevel level);
    LogLevel GetLogLevel() const { return current_log_level_.load(); }
    
    // Check if log level is enabled (for fast path optimization)
    inline bool IsLogLevelEnabled(LogLevel level) const {
        return level <= current_log_level_.load();
    }
    
    // Optimize thread pools
    bool OptimizeThreadPools();
    
    // Set CPU affinity for critical threads
    bool SetCriticalThreadAffinity();
    
    // Optimize memory allocation
    bool OptimizeMemoryAllocation();
    
    // Reduce system call overhead
    bool ReduceSystemCallOverhead();
    
    // Get system optimization statistics
    struct OptimizationStats {
        uint64_t logs_suppressed = 0;
        uint32_t threads_optimized = 0;
        uint32_t cpu_cores_utilized = 0;
        double cpu_efficiency = 0.0;
        uint64_t memory_saved_bytes = 0;
    };
    
    OptimizationStats GetOptimizationStats() const;
    
    // Performance monitoring
    void StartPerformanceMonitoring();
    void StopPerformanceMonitoring();
    
    // Thread pool management
    std::shared_ptr<OptimizedThreadPool> GetThreadPool(const std::string& name);
    bool CreateThreadPool(const std::string& name, const ThreadPoolConfig& config);

private:
    SystemOptimizer();
    ~SystemOptimizer() = default;
    
    void PerformanceMonitoringThread();
    void OptimizeLogOutput();
    void ConfigureThreadPriorities();
    
    std::atomic<LogLevel> current_log_level_{LogLevel::INFO};
    std::atomic<bool> monitoring_active_{false};
    std::unique_ptr<std::thread> monitoring_thread_;
    
    // Thread pools
    std::unordered_map<std::string, std::shared_ptr<OptimizedThreadPool>> thread_pools_;
    std::mutex thread_pools_mutex_;
    
    // Statistics
    mutable std::mutex stats_mutex_;
    OptimizationStats stats_;
    
    DISALLOW_COPY_AND_ASSIGN(SystemOptimizer);
};

// Optimized logging macros that check log level first
#define SHARDORA_DEBUG_OPT(logfmt, ...) \
    do { \
        if (shardora::common::SystemOptimizer::Instance()->IsLogLevelEnabled(shardora::common::LogLevel::DEBUG)) { \
            SHARDORA_DEBUG(logfmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define SHARDORA_INFO_OPT(logfmt, ...) \
    do { \
        if (shardora::common::SystemOptimizer::Instance()->IsLogLevelEnabled(shardora::common::LogLevel::INFO)) { \
            SHARDORA_INFO(logfmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define SHARDORA_WARN_OPT(logfmt, ...) \
    do { \
        if (shardora::common::SystemOptimizer::Instance()->IsLogLevelEnabled(shardora::common::LogLevel::WARN)) { \
            SHARDORA_WARN(logfmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define SHARDORA_ERROR_OPT(logfmt, ...) \
    do { \
        if (shardora::common::SystemOptimizer::Instance()->IsLogLevelEnabled(shardora::common::LogLevel::ERROR)) { \
            SHARDORA_ERROR(logfmt, ##__VA_ARGS__); \
        } \
    } while(0)

// High-frequency path optimization - completely disable debug logs in release
#ifdef NDEBUG
#define SHARDORA_DEBUG_FAST(logfmt, ...) do {} while(0)
#else
#define SHARDORA_DEBUG_FAST(logfmt, ...) SHARDORA_DEBUG_OPT(logfmt, ##__VA_ARGS__)
#endif

// Thread-local storage for performance optimization
extern thread_local uint64_t tls_log_suppression_count;
extern thread_local uint64_t tls_last_log_time;

// Fast log suppression for high-frequency paths
#define SHARDORA_DEBUG_THROTTLED(interval_ms, logfmt, ...) \
    do { \
        uint64_t now = shardora::common::TimeUtils::TimestampMs(); \
        if (now - tls_last_log_time >= interval_ms) { \
            tls_last_log_time = now; \
            SHARDORA_DEBUG_OPT(logfmt, ##__VA_ARGS__); \
        } else { \
            ++tls_log_suppression_count; \
        } \
    } while(0)

}  // namespace common
}  // namespace shardora
