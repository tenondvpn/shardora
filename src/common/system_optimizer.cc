#include "common/system_optimizer.h"
#include "common/time_utils.h"
#include "common/global_info.h"
#include <algorithm>
#include <chrono>
#include <queue>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#ifdef __has_include
#if __has_include(<numa.h>)
#include <numa.h>
#define HAVE_NUMA 1
#endif
#else
// Try to include numa.h, but don't fail if it's not available
#include <numa.h>
#define HAVE_NUMA 1
#endif
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace shardora {
namespace common {

// Thread-local storage definitions
thread_local uint64_t tls_log_suppression_count = 0;
thread_local uint64_t tls_last_log_time = 0;

// CpuAffinityManager implementation
CpuAffinityManager* CpuAffinityManager::Instance() {
    static CpuAffinityManager instance;
    return &instance;
}

CpuAffinityManager::CpuAffinityManager() {
    InitializeCpuTopology();
}

bool CpuAffinityManager::InitializeCpuTopology() {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    cpu_count_ = sysinfo.dwNumberOfProcessors;
    numa_node_count_ = 1; // Simplified for Windows
#elif defined(__APPLE__)
    int cpu_count = 0;
    size_t cpu_count_len = sizeof(cpu_count);
    if (sysctlbyname("hw.logicalcpu", &cpu_count, &cpu_count_len, nullptr, 0) == 0 && cpu_count > 0) {
        cpu_count_ = cpu_count;
    } else {
        cpu_count_ = static_cast<int>(std::max(1L, sysconf(_SC_NPROCESSORS_ONLN)));
    }
    numa_node_count_ = 1;
#else
    cpu_count_ = sysconf(_SC_NPROCESSORS_ONLN);
    
    // Check for NUMA support
#ifdef HAVE_NUMA
    if (numa_available() >= 0) {
        numa_node_count_ = numa_max_node() + 1;
    } else {
        numa_node_count_ = 1;
    }
#else
    numa_node_count_ = 1;
#endif
#endif

    // Configure optimal core assignments for different thread types
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Reserve cores for different thread types
    int cores_per_type = std::max(1, cpu_count_ / 4);
    
    // Consensus threads get first cores (most critical)
    std::vector<int> consensus_cores;
    for (int i = 0; i < cores_per_type && i < cpu_count_; ++i) {
        consensus_cores.push_back(i);
    }
    thread_type_cores_["consensus"] = consensus_cores;
    
    // Network I/O threads get next cores
    std::vector<int> network_cores;
    for (int i = cores_per_type; i < 2 * cores_per_type && i < cpu_count_; ++i) {
        network_cores.push_back(i);
    }
    thread_type_cores_["network"] = network_cores;
    
    // Database threads
    std::vector<int> db_cores;
    for (int i = 2 * cores_per_type; i < 3 * cores_per_type && i < cpu_count_; ++i) {
        db_cores.push_back(i);
    }
    thread_type_cores_["database"] = db_cores;
    
    // General purpose threads get remaining cores
    std::vector<int> general_cores;
    for (int i = 3 * cores_per_type; i < cpu_count_; ++i) {
        general_cores.push_back(i);
    }
    thread_type_cores_["general"] = general_cores;
    
    return true;
}

bool CpuAffinityManager::SetThreadAffinity(const std::vector<int>& cpu_cores) {
    if (cpu_cores.empty()) return false;
    
#ifdef _WIN32
    DWORD_PTR mask = 0;
    for (int core : cpu_cores) {
        if (core >= 0 && core < cpu_count_) {
            mask |= (1ULL << core);
        }
    }
    return SetThreadAffinityMask(GetCurrentThread(), mask) != 0;
#elif defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (int core : cpu_cores) {
        if (core >= 0 && core < cpu_count_) {
            CPU_SET(core, &cpuset);
        }
    }
    
    return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) == 0;
#else
    return true;
#endif
}

bool CpuAffinityManager::SetThreadAffinity(std::thread::id thread_id, const std::vector<int>& cpu_cores) {
    if (cpu_cores.empty()) return false;
    
#ifdef _WIN32
    // Windows implementation would need thread handle
    return false; // Not implemented for external threads on Windows
#elif defined(__linux__)
    pthread_t native_handle = pthread_self(); // This is a simplification
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (int core : cpu_cores) {
        if (core >= 0 && core < cpu_count_) {
            CPU_SET(core, &cpuset);
        }
    }
    
    return pthread_setaffinity_np(native_handle, sizeof(cpu_set_t), &cpuset) == 0;
#else
    return true;
#endif
}

std::vector<int> CpuAffinityManager::GetOptimalCores(const std::string& thread_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = thread_type_cores_.find(thread_type);
    if (it != thread_type_cores_.end()) {
        return it->second;
    }
    
    // Return general cores as fallback
    auto general_it = thread_type_cores_.find("general");
    if (general_it != thread_type_cores_.end()) {
        return general_it->second;
    }
    
    // Last resort: return all cores
    std::vector<int> all_cores;
    for (int i = 0; i < cpu_count_; ++i) {
        all_cores.push_back(i);
    }
    return all_cores;
}

// OptimizedThreadPool implementation
OptimizedThreadPool::OptimizedThreadPool(const ThreadPoolConfig& config) 
    : config_(config) {
}

OptimizedThreadPool::~OptimizedThreadPool() {
    Shutdown();
}

bool OptimizedThreadPool::Initialize() {
    if (!threads_.empty()) {
        return false; // Already initialized
    }
    
    // Create worker threads
    for (uint32_t i = 0; i < config_.core_threads; ++i) {
        auto thread = std::make_unique<std::thread>(&OptimizedThreadPool::WorkerThread, this, i);
        threads_.push_back(std::move(thread));
    }
    
    return true;
}

void OptimizedThreadPool::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        shutdown_ = true;
    }
    condition_.notify_all();
    
    for (auto& thread : threads_) {
        if (thread && thread->joinable()) {
            thread->join();
        }
    }
    threads_.clear();
}

template<typename F>
bool OptimizedThreadPool::Submit(F&& task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (shutdown_ || task_queue_.size() >= config_.queue_size) {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            ++stats_.tasks_rejected;
            return false;
        }
        
        task_queue_.emplace(std::forward<F>(task));
        {
            std::lock_guard<std::mutex> stats_lock(stats_mutex_);
            ++stats_.tasks_submitted;
        }
    }
    condition_.notify_one();
    return true;
}

size_t OptimizedThreadPool::GetPendingTaskCount() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}

OptimizedThreadPool::Statistics OptimizedThreadPool::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void OptimizedThreadPool::WorkerThread(int thread_id) {
    // Set CPU affinity for this worker thread
    SetWorkerAffinity(thread_id);
    
    // Set thread priority if specified
    if (config_.thread_priority != 0) {
#ifdef _WIN32
        int priority = THREAD_PRIORITY_NORMAL;
        if (config_.thread_priority > 0) {
            priority = THREAD_PRIORITY_ABOVE_NORMAL;
        } else {
            priority = THREAD_PRIORITY_BELOW_NORMAL;
        }
        SetThreadPriority(GetCurrentThread(), priority);
#else
        struct sched_param param;
        param.sched_priority = config_.thread_priority;
        pthread_setschedparam(pthread_self(), SCHED_OTHER, &param);
#endif
    }
    
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return shutdown_ || !task_queue_.empty(); });
            
            if (shutdown_ && task_queue_.empty()) {
                break;
            }
            
            if (!task_queue_.empty()) {
                task = std::move(task_queue_.front());
                task_queue_.pop();
            }
        }
        
        if (task) {
            {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                ++stats_.active_threads;
            }
            
            try {
                task();
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                ++stats_.tasks_completed;
                --stats_.active_threads;
            } catch (...) {
                std::lock_guard<std::mutex> stats_lock(stats_mutex_);
                --stats_.active_threads;
                // Log error but continue
            }
        }
    }
}

bool OptimizedThreadPool::SetWorkerAffinity(int thread_id) {
    if (!config_.enable_cpu_affinity) {
        return true;
    }
    
    std::vector<int> cores;
    if (!config_.cpu_cores.empty()) {
        cores = config_.cpu_cores;
    } else {
        // Use optimal cores for general purpose threads
        cores = CpuAffinityManager::Instance()->GetOptimalCores("general");
    }
    
    return CpuAffinityManager::Instance()->SetThreadAffinity(cores);
}

// SystemOptimizer implementation
SystemOptimizer* SystemOptimizer::Instance() {
    static SystemOptimizer instance;
    return &instance;
}

SystemOptimizer::SystemOptimizer() {
    // Initialize with INFO level by default for production
    current_log_level_ = LogLevel::INFO;
}

bool SystemOptimizer::Initialize() {
    // Initialize CPU affinity manager
    if (!CpuAffinityManager::Instance()->InitializeCpuTopology()) {
        return false;
    }
    
    // Set up default thread pools
    ThreadPoolConfig consensus_config;
    consensus_config.core_threads = GlobalInfo::Instance()->hotstuff_thread_count();
    consensus_config.max_threads = consensus_config.core_threads * 2;
    consensus_config.cpu_cores = CpuAffinityManager::Instance()->GetOptimalCores("consensus");
    consensus_config.thread_priority = 10; // Higher priority for consensus
    CreateThreadPool("consensus", consensus_config);
    
    ThreadPoolConfig network_config;
    network_config.core_threads = GlobalInfo::Instance()->tcp_server_thread_count();
    network_config.max_threads = network_config.core_threads * 2;
    network_config.cpu_cores = CpuAffinityManager::Instance()->GetOptimalCores("network");
    network_config.thread_priority = 5;
    CreateThreadPool("network", network_config);
    
    ThreadPoolConfig general_config;
    general_config.core_threads = 4;
    general_config.max_threads = 8;
    general_config.cpu_cores = CpuAffinityManager::Instance()->GetOptimalCores("general");
    CreateThreadPool("general", general_config);
    
    // Start performance monitoring
    StartPerformanceMonitoring();
    
    return true;
}

void SystemOptimizer::SetLogLevel(LogLevel level) {
    current_log_level_ = level;
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    // Reset suppression count when log level changes
    tls_log_suppression_count = 0;
}

bool SystemOptimizer::OptimizeThreadPools() {
    // Get current system load and adjust thread pool sizes
    int cpu_count = CpuAffinityManager::Instance()->GetCpuCount();
    
    std::lock_guard<std::mutex> lock(thread_pools_mutex_);
    for (auto& [name, pool] : thread_pools_) {
        // Adjust thread pool based on current load
        auto stats = pool->GetStatistics();
        
        // If queue is consistently full, consider scaling up (if not at max)
        if (stats.tasks_rejected > stats.tasks_completed * 0.1) {
            // High rejection rate indicates need for more capacity
            // This would require implementing dynamic scaling
        }
    }
    
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.threads_optimized = thread_pools_.size();
    stats_.cpu_cores_utilized = cpu_count;
    
    return true;
}

bool SystemOptimizer::SetCriticalThreadAffinity() {
    // This would be called from the main thread initialization
    // to set affinity for critical system threads
    
    auto consensus_cores = CpuAffinityManager::Instance()->GetOptimalCores("consensus");
    auto network_cores = CpuAffinityManager::Instance()->GetOptimalCores("network");
    
    // Set affinity for current thread (assuming it's a critical thread)
    bool success = CpuAffinityManager::Instance()->SetThreadAffinity(consensus_cores);
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    if (success) {
        ++stats_.threads_optimized;
    }
    
    return success;
}

bool SystemOptimizer::OptimizeMemoryAllocation() {
    // Enable memory optimizations
    // This could include setting up memory pools, adjusting allocator settings, etc.
    
#ifdef _WIN32
    // Windows-specific memory optimizations
    SetProcessWorkingSetSize(GetCurrentProcess(), -1, -1);
#else
    // Linux-specific memory optimizations
    // Could set memory allocation policies, enable transparent huge pages, etc.
#endif
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.memory_saved_bytes += 1024 * 1024; // Placeholder
    
    return true;
}

bool SystemOptimizer::ReduceSystemCallOverhead() {
    // Optimize system call usage
    // This could include batching operations, using more efficient APIs, etc.
    
    return true;
}

SystemOptimizer::OptimizationStats SystemOptimizer::GetOptimizationStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto stats = stats_;
    stats.logs_suppressed = tls_log_suppression_count;
    return stats;
}

void SystemOptimizer::StartPerformanceMonitoring() {
    if (monitoring_active_) {
        return;
    }
    
    monitoring_active_ = true;
    monitoring_thread_ = std::make_unique<std::thread>(&SystemOptimizer::PerformanceMonitoringThread, this);
}

void SystemOptimizer::StopPerformanceMonitoring() {
    monitoring_active_ = false;
    if (monitoring_thread_ && monitoring_thread_->joinable()) {
        monitoring_thread_->join();
    }
    monitoring_thread_.reset();
}

std::shared_ptr<OptimizedThreadPool> SystemOptimizer::GetThreadPool(const std::string& name) {
    std::lock_guard<std::mutex> lock(thread_pools_mutex_);
    auto it = thread_pools_.find(name);
    return (it != thread_pools_.end()) ? it->second : nullptr;
}

bool SystemOptimizer::CreateThreadPool(const std::string& name, const ThreadPoolConfig& config) {
    std::lock_guard<std::mutex> lock(thread_pools_mutex_);
    
    if (thread_pools_.find(name) != thread_pools_.end()) {
        return false; // Already exists
    }
    
    auto pool = std::make_shared<OptimizedThreadPool>(config);
    if (!pool->Initialize()) {
        return false;
    }
    
    thread_pools_[name] = pool;
    return true;
}

void SystemOptimizer::PerformanceMonitoringThread() {
    while (monitoring_active_) {
        // Monitor system performance and adjust optimizations
        OptimizeThreadPools();
        
        // Sleep for monitoring interval
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

void SystemOptimizer::OptimizeLogOutput() {
    // This could implement log batching, async logging, etc.
}

void SystemOptimizer::ConfigureThreadPriorities() {
    // Configure thread priorities based on importance
}

}  // namespace common
}  // namespace shardora
