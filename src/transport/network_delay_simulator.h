#pragma once

#include <cstdint>
#include <random>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <cstring>

namespace shardora {
namespace transport {

/**
 * NetworkDelaySimulator: 应用层网络延迟注入
 * 
 * 用于模拟网络延迟、抖动和丢包，避免 TCP 层报文破坏
 * 通过环境变量配置:
 *   SHARDORA_NETWORK_ENABLED=1        # 启用网络模拟 (0=禁用)
 *   SHARDORA_NETWORK_DELAY_MS=25      # 单向延迟 (ms)
 *   SHARDORA_NETWORK_JITTER_MS=10     # 抖动 (ms)
 *   SHARDORA_NETWORK_LOSS_RATE=0.0001 # 丢包率 (0-1)
 */
class NetworkDelaySimulator {
public:
    NetworkDelaySimulator() {
        // 从环境变量读取配置
        const char* enabled_str = std::getenv("SHARDORA_NETWORK_ENABLED");
        enabled_ = enabled_str ? std::atoi(enabled_str) : 0;
        
        const char* delay_str = std::getenv("SHARDORA_NETWORK_DELAY_MS");
        delay_ms_ = delay_str ? std::atoi(delay_str) : 0;
        
        const char* jitter_str = std::getenv("SHARDORA_NETWORK_JITTER_MS");
        jitter_ms_ = jitter_str ? std::atoi(jitter_str) : 0;
        
        const char* loss_str = std::getenv("SHARDORA_NETWORK_LOSS_RATE");
        loss_rate_ = loss_str ? std::atof(loss_str) : 0.0;
        
        // 初始化随机数生成器
        rng_.seed(std::chrono::system_clock::now().time_since_epoch().count());
    }
    
    /**
     * 检查是否应该丢弃报文
     * @return true 表示应该丢弃，false 表示保留
     */
    bool ShouldDropPacket() {
        if (!enabled_ || loss_rate_ <= 0.0) {
            return false;
        }
        
        std::uniform_real_distribution<> dist(0.0, 1.0);
        return dist(rng_) < loss_rate_;
    }
    
    /**
     * 获取实际延迟时间 (毫秒)
     * 延迟 = 基础延迟 + 随机抖动
     * @return 延迟时间 (毫秒)
     */
    uint32_t GetDelayMs() {
        if (!enabled_ || delay_ms_ == 0) {
            return 0;
        }
        
        // 生成 [-jitter_ms_, +jitter_ms_] 范围内的随机抖动
        std::uniform_int_distribution<> jitter_dist(-static_cast<int>(jitter_ms_), 
                                                     static_cast<int>(jitter_ms_));
        int32_t jitter = jitter_dist(rng_);
        
        // 实际延迟 = 基础延迟 + 抖动，最小为 0
        int32_t actual_delay = static_cast<int32_t>(delay_ms_) + jitter;
        return std::max(0, actual_delay);
    }
    
    /**
     * 应用网络延迟
     * 通过 sleep 实现延迟
     */
    void ApplyDelay() {
        uint32_t delay = GetDelayMs();
        if (delay > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        }
    }
    
    /**
     * 获取配置状态 (用于日志)
     */
    bool IsEnabled() const {
        return enabled_;
    }
    
    uint32_t GetDelayMsConfig() const {
        return delay_ms_;
    }
    
    uint32_t GetJitterMsConfig() const {
        return jitter_ms_;
    }
    
    double GetLossRateConfig() const {
        return loss_rate_;
    }

private:
    bool enabled_;           // 是否启用网络模拟
    uint32_t delay_ms_;      // 基础延迟 (毫秒)
    uint32_t jitter_ms_;     // 抖动 (毫秒)
    double loss_rate_;       // 丢包率 (0-1)
    std::mt19937 rng_;       // 随机数生成器
};

}  // namespace transport
}  // namespace shardora
