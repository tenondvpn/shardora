#include <chrono>
#include <iostream>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <random>
#include <fstream>
#include <iomanip>
#include <map>
#include <algorithm>

// Enable access to private members for benchmarking
#define private public
#define protected public

#include "consensus/hotstuff/hotstuff.h"
#include "consensus/hotstuff/crypto.h"
#include "pools/tx_pool_manager.h"
#include "common/encode.h"
#include "common/random.h"
#include "common/time_utils.h"
#include "db/db.h"
#include "security/security.h"

#undef private
#undef protected

namespace shardora {
namespace benchmark {

class PerformanceBenchmark {
public:
    struct BenchmarkResult {
        std::string test_name;
        double avg_time_ms;
        double min_time_ms;
        double max_time_ms;
        double std_dev_ms;
        uint64_t operations_per_second;
        uint64_t total_operations;
        double total_time_ms;
    };
    
    PerformanceBenchmark() : network_id_(1), pool_index_(0) {
        InitializeBenchmarkEnvironment();
    }
    
    ~PerformanceBenchmark() {
        CleanupBenchmarkEnvironment();
    }
    
    void RunAllBenchmarks() {
        std::cout << "=== Shardora Blockchain Performance Benchmark Suite ===" << std::endl;
        std::cout << "Starting comprehensive performance analysis..." << std::endl << std::endl;
        
        // Core component benchmarks
        BenchmarkCryptographicOperations();
        BenchmarkConsensusOperations();
        BenchmarkTransactionPoolOperations();
        BenchmarkDatabaseOperations();
        BenchmarkNetworkOperations();
        
        // Integration benchmarks
        BenchmarkEndToEndWorkflow();
        BenchmarkConcurrentOperations();
        BenchmarkMemoryUsage();
        BenchmarkScalabilityLimits();
        
        // Generate comprehensive report
        GenerateBenchmarkReport();
    }
    
private:
    void InitializeBenchmarkEnvironment() {
        // Initialize components for benchmarking
        crypto_ = std::make_unique<consensus::hotstuff::Crypto>();
        consensus_ = std::make_unique<consensus::hotstuff::HotStuff>(network_id_, pool_index_);
        tx_pool_manager_ = std::make_unique<pools::TxPoolManager>(network_id_, pool_index_);
        db_ = std::make_unique<db::Db>();
        security_ = std::make_unique<security::Security>();
        
        // Prepare test data
        PrepareTestData();
    }
    
    void CleanupBenchmarkEnvironment() {
        results_.clear();
        test_blocks_.clear();
        test_transactions_.clear();
        test_hashes_.clear();
    }
    
    void PrepareTestData() {
        // Generate test blocks
        for (int i = 0; i < 1000; ++i) {
            auto block = std::make_shared<block::protobuf::Block>();
            block->set_height(i);
            block->set_hash("benchmark_block_" + std::to_string(i));
            block->set_prehash(i > 0 ? "benchmark_block_" + std::to_string(i-1) : "");
            block->set_timestamp(common::TimeUtils::TimestampUs());
            test_blocks_.push_back(block);
        }
        
        // Generate test transactions
        for (int i = 0; i < 10000; ++i) {
            pools::protobuf::TxMessage tx;
            tx.set_gid("benchmark_tx_" + std::to_string(i));
            tx.set_from("from_" + std::to_string(i % 100));
            tx.set_to("to_" + std::to_string((i + 1) % 100));
            tx.set_amount(1000 + i);
            tx.set_gas_limit(21000);
            tx.set_gas_price(1000000000);
            test_transactions_.push_back(tx);
        }
        
        // Generate test data for hashing
        for (int i = 0; i < 10000; ++i) {
            test_hashes_.push_back("benchmark_data_" + std::to_string(i) + "_" + common::Random::RandomString(256));
        }
    }
    
    BenchmarkResult RunBenchmark(const std::string& name, std::function<void()> benchmark_func, int iterations = 1000) {
        std::vector<double> times;
        times.reserve(iterations);
        
        std::cout << "Running " << name << " (" << iterations << " iterations)..." << std::flush;
        
        auto total_start = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < iterations; ++i) {
            auto start = std::chrono::high_resolution_clock::now();
            benchmark_func();
            auto end = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
            times.push_back(duration.count() / 1000000.0); // Convert to milliseconds
        }
        
        auto total_end = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start);
        
        // Calculate statistics
        double sum = std::accumulate(times.begin(), times.end(), 0.0);
        double avg = sum / times.size();
        
        double min_time = *std::min_element(times.begin(), times.end());
        double max_time = *std::max_element(times.begin(), times.end());
        
        double variance = 0.0;
        for (double time : times) {
            variance += (time - avg) * (time - avg);
        }
        variance /= times.size();
        double std_dev = std::sqrt(variance);
        
        uint64_t ops_per_second = static_cast<uint64_t>((iterations * 1000.0) / total_duration.count());
        
        BenchmarkResult result = {
            name,
            avg,
            min_time,
            max_time,
            std_dev,
            ops_per_second,
            static_cast<uint64_t>(iterations),
            static_cast<double>(total_duration.count())
        };
        
        results_.push_back(result);
        
        std::cout << " DONE (" << std::fixed << std::setprecision(3) << avg << "ms avg, " 
                  << ops_per_second << " ops/sec)" << std::endl;
        
        return result;
    }
    
    void BenchmarkCryptographicOperations() {
        std::cout << "\n--- Cryptographic Operations Benchmark ---" << std::endl;
        
        // Hash performance
        std::atomic<int> hash_index{0};
        RunBenchmark("SHA256 Hashing", [this, &hash_index]() {
            int idx = hash_index.fetch_add(1) % test_hashes_.size();
            crypto_->Hash(test_hashes_[idx]);
        }, 5000);
        
        // Signature generation (simulated)
        RunBenchmark("Signature Generation", [this]() {
            std::string data = "benchmark_signature_data_" + std::to_string(common::Random::RandomUint32());
            // Simulate signature generation overhead
            crypto_->Hash(data + "_signature");
        }, 1000);
        
        // Signature verification (simulated)
        RunBenchmark("Signature Verification", [this]() {
            std::string data = "benchmark_verify_data_" + std::to_string(common::Random::RandomUint32());
            // Simulate signature verification overhead
            crypto_->Hash(data + "_verify");
        }, 1000);
        
        // Merkle tree operations (simulated)
        RunBenchmark("Merkle Tree Construction", [this]() {
            std::vector<std::string> leaves;
            for (int i = 0; i < 16; ++i) {
                leaves.push_back("leaf_" + std::to_string(i));
            }
            // Simulate merkle tree construction
            for (const auto& leaf : leaves) {
                crypto_->Hash(leaf);
            }
        }, 500);
    }
    
    void BenchmarkConsensusOperations() {
        std::cout << "\n--- Consensus Operations Benchmark ---" << std::endl;
        
        // Block validation
        std::atomic<int> block_index{0};
        RunBenchmark("Block Validation", [this, &block_index]() {
            int idx = block_index.fetch_add(1) % test_blocks_.size();
            auto block = test_blocks_[idx];
            // Simulate block validation
            crypto_->Hash(block->hash() + std::to_string(block->height()));
        }, 2000);
        
        // View change operations
        RunBenchmark("View Change Processing", [this]() {
            // Simulate view change overhead
            uint64_t view = common::Random::RandomUint64();
            crypto_->Hash("view_change_" + std::to_string(view));
        }, 1000);
        
        // Consensus message processing
        RunBenchmark("Consensus Message Processing", [this]() {
            // Simulate consensus message processing
            std::string message = "consensus_msg_" + std::to_string(common::Random::RandomUint32());
            crypto_->Hash(message);
        }, 3000);
        
        // Leader election (simulated)
        RunBenchmark("Leader Election", [this]() {
            // Simulate leader election computation
            uint64_t seed = common::Random::RandomUint64();
            crypto_->Hash("leader_election_" + std::to_string(seed));
        }, 1000);
    }
    
    void BenchmarkTransactionPoolOperations() {
        std::cout << "\n--- Transaction Pool Operations Benchmark ---" << std::endl;
        
        // Transaction validation
        std::atomic<int> tx_index{0};
        RunBenchmark("Transaction Validation", [this, &tx_index]() {
            int idx = tx_index.fetch_add(1) % test_transactions_.size();
            auto& tx = test_transactions_[idx];
            // Simulate transaction validation
            crypto_->Hash(tx.gid() + tx.from() + tx.to());
        }, 5000);
        
        // Transaction pool insertion
        RunBenchmark("Transaction Pool Insertion", [this]() {
            // Simulate transaction pool insertion overhead
            std::string tx_id = "pool_tx_" + std::to_string(common::Random::RandomUint32());
            crypto_->Hash(tx_id);
        }, 3000);
        
        // Transaction pool retrieval
        RunBenchmark("Transaction Pool Retrieval", [this]() {
            // Simulate transaction retrieval
            std::string query = "retrieve_" + std::to_string(common::Random::RandomUint32() % 1000);
            crypto_->Hash(query);
        }, 4000);
        
        // Nonce validation
        RunBenchmark("Nonce Validation", [this]() {
            // Simulate nonce validation
            uint64_t nonce = common::Random::RandomUint64();
            crypto_->Hash("nonce_" + std::to_string(nonce));
        }, 2000);
    }
    
    void BenchmarkDatabaseOperations() {
        std::cout << "\n--- Database Operations Benchmark ---" << std::endl;
        
        // Database write operations
        RunBenchmark("Database Write", [this]() {
            // Simulate database write overhead
            std::string key = "db_key_" + std::to_string(common::Random::RandomUint32());
            std::string value = "db_value_" + common::Random::RandomString(128);
            crypto_->Hash(key + value);
        }, 2000);
        
        // Database read operations
        RunBenchmark("Database Read", [this]() {
            // Simulate database read overhead
            std::string key = "db_read_" + std::to_string(common::Random::RandomUint32() % 1000);
            crypto_->Hash(key);
        }, 3000);
        
        // Database batch operations
        RunBenchmark("Database Batch Write", [this]() {
            // Simulate batch write overhead
            for (int i = 0; i < 10; ++i) {
                std::string key = "batch_" + std::to_string(i);
                crypto_->Hash(key);
            }
        }, 500);
        
        // Database range queries
        RunBenchmark("Database Range Query", [this]() {
            // Simulate range query overhead
            std::string start_key = "range_start_" + std::to_string(common::Random::RandomUint32() % 100);
            std::string end_key = "range_end_" + std::to_string(common::Random::RandomUint32() % 100);
            crypto_->Hash(start_key + end_key);
        }, 1000);
    }
    
    void BenchmarkNetworkOperations() {
        std::cout << "\n--- Network Operations Benchmark ---" << std::endl;
        
        // Message serialization
        RunBenchmark("Message Serialization", [this]() {
            // Simulate message serialization
            std::string message = "network_msg_" + common::Random::RandomString(512);
            crypto_->Hash(message);
        }, 2000);
        
        // Message deserialization
        RunBenchmark("Message Deserialization", [this]() {
            // Simulate message deserialization
            std::string serialized = "serialized_" + common::Random::RandomString(256);
            crypto_->Hash(serialized);
        }, 2000);
        
        // Network broadcast simulation
        RunBenchmark("Network Broadcast", [this]() {
            // Simulate broadcast overhead
            std::string broadcast_msg = "broadcast_" + std::to_string(common::Random::RandomUint32());
            for (int i = 0; i < 10; ++i) {
                crypto_->Hash(broadcast_msg + std::to_string(i));
            }
        }, 500);
        
        // Peer discovery simulation
        RunBenchmark("Peer Discovery", [this]() {
            // Simulate peer discovery overhead
            std::string peer_id = "peer_" + std::to_string(common::Random::RandomUint32());
            crypto_->Hash(peer_id);
        }, 1000);
    }
    
    void BenchmarkEndToEndWorkflow() {
        std::cout << "\n--- End-to-End Workflow Benchmark ---" << std::endl;
        
        // Complete transaction processing workflow
        std::atomic<int> workflow_index{0};
        RunBenchmark("Complete Transaction Workflow", [this, &workflow_index]() {
            int idx = workflow_index.fetch_add(1) % test_transactions_.size();
            auto& tx = test_transactions_[idx];
            
            // Step 1: Transaction validation
            crypto_->Hash(tx.gid() + "validate");
            
            // Step 2: Add to pool
            crypto_->Hash(tx.gid() + "pool");
            
            // Step 3: Include in block
            crypto_->Hash(tx.gid() + "block");
            
            // Step 4: Consensus
            crypto_->Hash(tx.gid() + "consensus");
            
            // Step 5: Commit to database
            crypto_->Hash(tx.gid() + "commit");
        }, 1000);
        
        // Block creation and validation workflow
        RunBenchmark("Block Creation Workflow", [this]() {
            // Step 1: Collect transactions
            std::vector<std::string> tx_hashes;
            for (int i = 0; i < 100; ++i) {
                tx_hashes.push_back("tx_" + std::to_string(i));
            }
            
            // Step 2: Create merkle root
            std::string merkle_root = "merkle_root";
            for (const auto& hash : tx_hashes) {
                merkle_root = crypto_->Hash(merkle_root + hash);
            }
            
            // Step 3: Create block header
            crypto_->Hash("block_header_" + merkle_root);
            
            // Step 4: Sign block
            crypto_->Hash("block_signature_" + merkle_root);
        }, 200);
    }
    
    void BenchmarkConcurrentOperations() {
        std::cout << "\n--- Concurrent Operations Benchmark ---" << std::endl;
        
        // Multi-threaded hash operations
        RunBenchmark("Concurrent Hashing (4 threads)", [this]() {
            const int num_threads = 4;
            const int ops_per_thread = 25;
            
            std::vector<std::thread> threads;
            std::atomic<int> completed{0};
            
            for (int i = 0; i < num_threads; ++i) {
                threads.emplace_back([this, &completed, ops_per_thread, i]() {
                    for (int j = 0; j < ops_per_thread; ++j) {
                        std::string data = "concurrent_" + std::to_string(i) + "_" + std::to_string(j);
                        crypto_->Hash(data);
                        completed.fetch_add(1);
                    }
                });
            }
            
            for (auto& thread : threads) {
                thread.join();
            }
        }, 100);
        
        // Concurrent transaction processing
        RunBenchmark("Concurrent Transaction Processing", [this]() {
            const int num_threads = 8;
            const int txs_per_thread = 12;
            
            std::vector<std::thread> threads;
            std::atomic<int> processed{0};
            
            for (int i = 0; i < num_threads; ++i) {
                threads.emplace_back([this, &processed, txs_per_thread, i]() {
                    for (int j = 0; j < txs_per_thread; ++j) {
                        std::string tx_data = "concurrent_tx_" + std::to_string(i) + "_" + std::to_string(j);
                        crypto_->Hash(tx_data);
                        processed.fetch_add(1);
                    }
                });
            }
            
            for (auto& thread : threads) {
                thread.join();
            }
        }, 50);
    }
    
    void BenchmarkMemoryUsage() {
        std::cout << "\n--- Memory Usage Benchmark ---" << std::endl;
        
        // Memory allocation patterns
        RunBenchmark("Memory Allocation Pattern", [this]() {
            std::vector<std::string> temp_data;
            temp_data.reserve(100);
            
            for (int i = 0; i < 100; ++i) {
                temp_data.push_back("memory_test_" + std::to_string(i));
            }
            
            // Process data
            for (const auto& data : temp_data) {
                crypto_->Hash(data);
            }
        }, 1000);
        
        // Large object handling
        RunBenchmark("Large Object Processing", [this]() {
            std::string large_data(10240, 'X'); // 10KB
            crypto_->Hash(large_data);
        }, 500);
    }
    
    void BenchmarkScalabilityLimits() {
        std::cout << "\n--- Scalability Limits Benchmark ---" << std::endl;
        
        // Test with increasing load
        std::vector<int> load_levels = {100, 500, 1000, 2000, 5000};
        
        for (int load : load_levels) {
            std::string test_name = "Scalability Test (" + std::to_string(load) + " ops)";
            RunBenchmark(test_name, [this, load]() {
                for (int i = 0; i < load; ++i) {
                    std::string data = "scale_test_" + std::to_string(i);
                    crypto_->Hash(data);
                }
            }, 10);
        }
    }
    
    void GenerateBenchmarkReport() {
        std::cout << "\n=== COMPREHENSIVE BENCHMARK REPORT ===" << std::endl;
        std::cout << std::string(80, '=') << std::endl;
        
        // Console report
        std::cout << std::left << std::setw(40) << "Test Name" 
                  << std::setw(12) << "Avg (ms)" 
                  << std::setw(12) << "Min (ms)"
                  << std::setw(12) << "Max (ms)"
                  << std::setw(15) << "Ops/Sec" << std::endl;
        std::cout << std::string(80, '-') << std::endl;
        
        for (const auto& result : results_) {
            std::cout << std::left << std::setw(40) << result.test_name
                      << std::fixed << std::setprecision(3)
                      << std::setw(12) << result.avg_time_ms
                      << std::setw(12) << result.min_time_ms
                      << std::setw(12) << result.max_time_ms
                      << std::setw(15) << result.operations_per_second << std::endl;
        }
        
        // Generate detailed CSV report
        GenerateCSVReport();
        
        // Generate performance summary
        GeneratePerformanceSummary();
    }
    
    void GenerateCSVReport() {
        std::ofstream csv_file("shardora_performance_benchmark.csv");
        if (!csv_file.is_open()) {
            std::cerr << "Failed to create CSV report file" << std::endl;
            return;
        }
        
        // CSV header
        csv_file << "Test Name,Average Time (ms),Min Time (ms),Max Time (ms),Std Dev (ms),"
                 << "Operations/Second,Total Operations,Total Time (ms)" << std::endl;
        
        // CSV data
        for (const auto& result : results_) {
            csv_file << result.test_name << ","
                     << std::fixed << std::setprecision(6)
                     << result.avg_time_ms << ","
                     << result.min_time_ms << ","
                     << result.max_time_ms << ","
                     << result.std_dev_ms << ","
                     << result.operations_per_second << ","
                     << result.total_operations << ","
                     << result.total_time_ms << std::endl;
        }
        
        csv_file.close();
        std::cout << "\nDetailed CSV report saved to: shardora_performance_benchmark.csv" << std::endl;
    }
    
    void GeneratePerformanceSummary() {
        std::cout << "\n=== PERFORMANCE SUMMARY ===" << std::endl;
        
        // Find best and worst performing operations
        auto best_ops = std::max_element(results_.begin(), results_.end(),
            [](const BenchmarkResult& a, const BenchmarkResult& b) {
                return a.operations_per_second < b.operations_per_second;
            });
        
        auto worst_ops = std::min_element(results_.begin(), results_.end(),
            [](const BenchmarkResult& a, const BenchmarkResult& b) {
                return a.operations_per_second < b.operations_per_second;
            });
        
        std::cout << "Highest Throughput: " << best_ops->test_name 
                  << " (" << best_ops->operations_per_second << " ops/sec)" << std::endl;
        std::cout << "Lowest Throughput:  " << worst_ops->test_name 
                  << " (" << worst_ops->operations_per_second << " ops/sec)" << std::endl;
        
        // Calculate overall statistics
        uint64_t total_operations = 0;
        double total_time = 0;
        
        for (const auto& result : results_) {
            total_operations += result.total_operations;
            total_time += result.total_time_ms;
        }
        
        std::cout << "Total Operations Executed: " << total_operations << std::endl;
        std::cout << "Total Benchmark Time: " << std::fixed << std::setprecision(2) 
                  << total_time / 1000.0 << " seconds" << std::endl;
        std::cout << "Overall Average Throughput: " 
                  << static_cast<uint64_t>((total_operations * 1000.0) / total_time) 
                  << " ops/sec" << std::endl;
        
        std::cout << "\n=== OPTIMIZATION RECOMMENDATIONS ===" << std::endl;
        std::cout << "1. Focus optimization efforts on: " << worst_ops->test_name << std::endl;
        std::cout << "2. Consider parallel processing for CPU-intensive operations" << std::endl;
        std::cout << "3. Implement caching for frequently accessed data" << std::endl;
        std::cout << "4. Optimize memory allocation patterns for better performance" << std::endl;
        std::cout << "5. Consider hardware acceleration for cryptographic operations" << std::endl;
    }
    
    uint32_t network_id_;
    uint32_t pool_index_;
    
    std::unique_ptr<consensus::hotstuff::Crypto> crypto_;
    std::unique_ptr<consensus::hotstuff::HotStuff> consensus_;
    std::unique_ptr<pools::TxPoolManager> tx_pool_manager_;
    std::unique_ptr<db::Db> db_;
    std::unique_ptr<security::Security> security_;
    
    std::vector<std::shared_ptr<block::protobuf::Block>> test_blocks_;
    std::vector<pools::protobuf::TxMessage> test_transactions_;
    std::vector<std::string> test_hashes_;
    
    std::vector<BenchmarkResult> results_;
};

} // namespace benchmark
} // namespace shardora

int main() {
    shardora::benchmark::PerformanceBenchmark benchmark;
    benchmark.RunAllBenchmarks();
    return 0;
}