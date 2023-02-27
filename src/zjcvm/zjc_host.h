#pragma once

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include <evmc/evmc.hpp>

namespace zjchain {

namespace zjcvm {

using bytes = std::basic_string<uint8_t>;

struct storage_value {
    std::string str_val;
    evmc::bytes32 value;
    bool dirty{false};
    storage_value() noexcept = default;
    storage_value(const evmc::bytes32& _value, bool _dirty = false) noexcept
      : value{_value}, dirty{_dirty}
    {}
};

struct MockedAccount {
    int nonce = 0;
    bytes code;
    evmc::bytes32 codehash;
    evmc::uint256be balance;
    std::unordered_map<evmc::bytes32, storage_value> storage;
    std::unordered_map<std::string, storage_value> str_storage;
    void set_balance(uint64_t x) noexcept {
        balance = evmc::uint256be{};
        for (std::size_t i = 0; i < sizeof(x); ++i)
            balance.bytes[sizeof(balance) - 1 - i] = static_cast<uint8_t>(x >> (8 * i));
    }
};

class ZjchainHost : public evmc::Host {
public:
    struct log_record {
        evmc::address creator;
        bytes data;
        std::vector<evmc::bytes32> topics;
        bool operator==(const log_record& other) const noexcept {
            return creator == other.creator && data == other.data && topics == other.topics;
        }
    };

    struct selfdestuct_record {
        evmc::address selfdestructed;
        evmc::address beneficiary;
        bool operator==(const selfdestuct_record& other) const noexcept
        {
            return selfdestructed == other.selfdestructed && beneficiary == other.beneficiary;
        }
    };

    virtual bool account_exists(const evmc::address& addr) const noexcept override;
    virtual evmc::bytes32 get_storage(
        const evmc::address& addr,
        const evmc::bytes32& key) const noexcept override;
    virtual evmc_storage_status set_storage(
        const evmc::address& addr,
        const evmc::bytes32& key,
        const evmc::bytes32& value) noexcept override;
    virtual evmc::uint256be get_balance(const evmc::address& addr) const noexcept override;
    virtual size_t get_code_size(const evmc::address& addr) const noexcept override;
    virtual evmc::bytes32 get_code_hash(const evmc::address& addr) const noexcept override;
    virtual size_t copy_code(
        const evmc::address& addr,
        size_t code_offset,
        uint8_t* buffer_data,
        size_t buffer_size) const noexcept override;
    virtual bool selfdestruct(
        const evmc::address& addr,
        const evmc::address& beneficiary) noexcept override;
    virtual evmc::Result call(const evmc_message& msg) noexcept override;
    virtual evmc_tx_context get_tx_context() const noexcept override;
    virtual evmc::bytes32 get_block_hash(int64_t block_number) const noexcept override;
    virtual void emit_log(
        const evmc::address& addr,
        const uint8_t* data,
        size_t data_size,
        const evmc::bytes32 topics[],
        size_t topics_count) noexcept override;

    virtual evmc_access_status access_account(const evmc::address& addr) noexcept {
        std::string addr_str = std::string((char*)addr.bytes, sizeof(addr.bytes));
        auto iter = access_account_cold_map_.find(addr_str);
        if (iter != access_account_cold_map_.end()) {
            return EVMC_ACCESS_WARM;
        }

        access_account_cold_map_[addr_str] = true;
        return EVMC_ACCESS_COLD;
    }

    virtual evmc_access_status access_storage(const evmc::address& addr, const evmc::bytes32& key) noexcept {
        std::string addr_str = std::string((char*)addr.bytes, sizeof(addr.bytes));
        std::string key_str = std::string((char*)key.bytes, sizeof(key.bytes));
        std::string cold_key = addr_str + key_str;
        auto iter = access_storage_cold_map_.find(cold_key);
        if (iter != access_storage_cold_map_.end()) {
            return EVMC_ACCESS_WARM;
        }

        access_storage_cold_map_[cold_key] = true;
        return EVMC_ACCESS_COLD;
    }

    // tmp item
    void AddTmpAccountBalance(const std::string& address, uint64_t balance);
    int SaveKeyValue(const std::string& id, const std::string& key, const std::string& val);
    int GetKeyValue(const std::string& id, const std::string& key, std::string* val);

    std::unordered_map<evmc::address, MockedAccount> accounts_;
    evmc_tx_context tx_context_ = {};
    evmc::bytes32 block_hash_ = {};
    std::vector<log_record> recorded_logs_;
    std::vector<selfdestuct_record> recorded_selfdestructs_;

    std::string my_address_;
    uint64_t gas_price_{ 0 };
    std::string origin_address_;
    uint32_t depth_{ 0 };
    std::unordered_map<std::string, std::unordered_map<std::string, uint64_t>> to_account_value_;
    std::unordered_map<evmc::address, evmc::uint256be> account_balance_;
    std::string create_bytes_code_;
    std::unordered_map<std::string, bool> access_account_cold_map_;
    std::unordered_map<std::string, bool> access_storage_cold_map_;
    uint8_t thread_idx_;
};

}  // namespace zjcvm

}  // namespace zjchain
