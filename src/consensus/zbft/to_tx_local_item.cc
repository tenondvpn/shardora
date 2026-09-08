#include "consensus/zbft/to_tx_local_item.h"

#include "common/encode.h"
#include "common/hash.h"
#include "shardoravm/execution.h"
#include "shardoravm/host_journal_stack.h"
#include "shardoravm/reversible_feistel_address.h"
#include "shardoravm/shardoravm_utils.h"

namespace shardora {

namespace consensus {

int ToTxLocalItem::HandleTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    pools::protobuf::ToTxMessageItem to_tx_item;
    if (!to_tx_item.ParseFromString(tx_info->value())) {
        block_tx.set_status(kConsensusError);
        SHARDORA_WARN("local get to txs info failed: %s, unique: %s",
            common::Encode::HexEncode(tx_info->value()).c_str(),
            common::Encode::HexEncode(tx_info->key()).c_str());
        return consensus::kConsensusSuccess;
    }

    uint64_t src_to_balance = 0;
    uint64_t src_to_nonce = 0;
    GetTempAccountBalance(shardora_host, block_tx.to(), acc_balance_map, &src_to_balance, &src_to_nonce);
    auto& unique_hash = tx_info->key();
    std::string val;
    if (shardora_host.GetKeyValue(block_tx.to(), unique_hash, &val) == shardoravm::kShardoravmSuccess) {
        SHARDORA_DEBUG("unique hash has consensus: %s, %s, %lu", 
            common::Encode::HexEncode(unique_hash).c_str(),
            common::Encode::HexEncode(to_tx_item.des()).c_str(),
            to_tx_item.amount());
        if (!acc_balance_map[block_tx.to()]->has_balance()) {
            acc_balance_map.erase(block_tx.to());
        }
        
        return consensus::kConsensusError;
    }

    InitHost(shardora_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
    block::protobuf::TxHashStatus tx_hash_status;
    tx_hash_status.set_status(block_tx.status());
    auto status_val = tx_hash_status.SerializeAsString();
    shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
    shardora_host.SaveKeyValue(block_tx.to(), unique_hash, "1");
    block_tx.set_unique_hash(unique_hash);
    block_tx.set_nonce(0);
    auto& block_to_txs = *view_block.mutable_block_info()->mutable_local_to();
    CreateLocalToTx(tx_index, view_block, shardora_host, acc_balance_map, to_tx_item, block_to_txs);
    SHARDORA_DEBUG("success call to tx local block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu, %s, %lu", 
        view_block.qc().pool_index(), view_block.qc().view(), src_to_nonce, block_tx.nonce(),
        common::Encode::HexEncode(to_tx_item.des()).c_str(), to_tx_item.amount());
    acc_balance_map[block_tx.to()]->set_balance(src_to_balance);
    acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
    acc_balance_map[block_tx.to()]->set_latest_height(view_block.block_info().height());
    acc_balance_map[block_tx.to()]->set_tx_index(tx_index);
    SHARDORA_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(block_tx.to()).c_str(), 
        ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str());
    SHARDORA_DEBUG("success consensus local transfer to unique hash: %s, %s",
        common::Encode::HexEncode(unique_hash).c_str(), 
        ProtobufToJson(block_to_txs).c_str());
    view_block.mutable_block_info()->add_unique_hashs(block_tx.unique_hash());
    return consensus::kConsensusSuccess;
}

void ToTxLocalItem::CreateLocalToTx(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        const pools::protobuf::ToTxMessageItem& to_tx_item,
        block::protobuf::ConsensusToTxs& block_to_txs) {
    if (to_tx_item.has_base_root_address()) {
        HandleCrossShardBase(tx_index, view_block, shardora_host, acc_balance_map, to_tx_item);
        return;
    }

    if (to_tx_item.des().size() != common::kUnicastAddressLength &&
            to_tx_item.des().size() != common::kPreypamentAddressLength) {
        SHARDORA_ERROR("invalid to tx item: %s", ProtobufToJson(to_tx_item).c_str());
        //assert(false);
        return;
    }

    auto new_addr_func = [&](const std::string& addr, uint64_t amount) {
        uint64_t to_balance = 0;
        uint64_t nonce = 0;
        int balance_status = GetTempAccountBalance(
            shardora_host,
            addr, 
            acc_balance_map, 
            &to_balance, 
            &nonce);
        if (balance_status != kConsensusSuccess) {
            SHARDORA_DEBUG("create new address: %s, balance: %lu",
                common::Encode::HexEncode(addr).c_str(),
                amount);
            to_balance = 0;
            auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
            addr_info->set_addr(addr);
            addr_info->set_sharding_id(view_block.qc().network_id());
            addr_info->set_pool_index(view_block.qc().pool_index());
            addr_info->set_type(address::protobuf::kNormal);
            addr_info->set_latest_height(view_block.block_info().height());
            acc_balance_map[addr] = addr_info;
        } else {
            SHARDORA_DEBUG("success get to balance: %s, %lu",
                common::Encode::HexEncode(addr).c_str(), 
                to_balance);
        }

        if (amount <= 0) {
            SHARDORA_DEBUG("failed just contract set prefund add addr: %s, to item: %s",
                common::Encode::HexEncode(addr).c_str(),
                ProtobufToJson(to_tx_item).c_str());
            return;
        }

        to_balance += amount;
        acc_balance_map[addr]->set_balance(to_balance);
        acc_balance_map[addr]->set_nonce(nonce);
        acc_balance_map[addr]->set_latest_height(view_block.block_info().height());
        acc_balance_map[addr]->set_tx_index(tx_index);
        SHARDORA_DEBUG("success add addr: %s, value: %s, to item: %s", 
            common::Encode::HexEncode(addr).c_str(), 
            ProtobufToJson(*(acc_balance_map[addr])).c_str(),
            ProtobufToJson(to_tx_item).c_str());
        SHARDORA_DEBUG("add local to: %s, balance: %lu, amount: %lu",
            common::Encode::HexEncode(addr).c_str(),
            to_balance,
            amount);
    };

    auto addr = to_tx_item.des();
    if (to_tx_item.des().size() == common::kPreypamentAddressLength) {
        addr = addr.substr(0, common::kUnicastAddressLength);
        new_addr_func(to_tx_item.des(), to_tx_item.prefund());
    }

    new_addr_func(addr, to_tx_item.amount());
}

void ToTxLocalItem::HandleCrossShardBase(
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        const pools::protobuf::ToTxMessageItem& to_tx) {
    const std::string& base_raw = to_tx.base_root_address();  // 20-byte raw
    uint32_t shard_id   = view_block.qc().network_id();
    // pool_index: prefer the caller-specified dest pool (stored in item.pool_index by
    // contract_call.cc); fall back to the current block's pool for legacy items.
    uint32_t pool_index = to_tx.has_pool_index()
                              ? static_cast<uint32_t>(to_tx.pool_index())
                              : view_block.qc().pool_index();

    if (base_raw.size() != 20) {
        SHARDORA_ERROR("CrossShardBase: invalid base_root_address length %zu", base_raw.size());
        return;
    }

    // Look up runtime bytecode from local KV registry (populated at deploy-broadcast time)
    std::string bytecode;
    {
        const std::string sys_str(reinterpret_cast<const char*>(
            shardoravm::kCrossShardSystemExecutor.bytes), 20);
        shardora_host.GetKeyValue(sys_str, "xsb:" + base_raw, &bytecode);
    }
    if (bytecode.empty()) {
        SHARDORA_ERROR("CrossShardBase: bytecode not in registry for base=%s "
                   "(contract not yet broadcast to this shard)",
            common::Encode::HexEncode(base_raw).c_str());
        return;
    }

    // ── 1. 确定目标合约地址 ──────────────────────────────────────────────────
    // If the CrossShardBase root contract itself already lives on this shard
    // (i.e. it was deployed here directly, not via lazy cross-shard clone),
    // operate on it in place instead of deriving a Feistel shadow address.
    // Deriving unconditionally is wrong: it silently credits a shadow clone
    // that nobody queries (balanceOf on the home shard reads the root
    // contract's own storage), so transfers whose destination happens to be
    // the root's home shard/pool were being lost.
    evmc::address base_evmc = shardoravm::StrToEvmcAddr(base_raw);
    std::string sys_exec_str(reinterpret_cast<const char*>(shardoravm::kCrossShardSystemExecutor.bytes), 20);

    bool base_lives_here = false;
    {
        auto bit = acc_balance_map.find(base_raw);
        if (bit != acc_balance_map.end() && !bit->second->bytes_code().empty()) {
            base_lives_here = true;
        } else if (shardora_host.view_block_chain_) {
            auto chain_info = shardora_host.view_block_chain_->ChainGetAccountInfo(base_raw);
            if (chain_info && !chain_info->bytes_code().empty()) {
                base_lives_here = true;
            }
        }
    }

    evmc::address target_evmc = base_evmc;
    std::string target_str = base_raw;

    if (!base_lives_here) {
        target_evmc = shardoravm::DeriveShardAddress(base_evmc, shard_id, pool_index);
        target_str = std::string(reinterpret_cast<const char*>(target_evmc.bytes), 20);

        // ── 2. 懒部署：若分身合约不存在，写入 bytecode + 必要存储槽 ─────────────
        bool needs_deploy = false;
        auto it = acc_balance_map.find(target_str);
        if (it == acc_balance_map.end() || it->second->bytes_code().empty()) {
            // 检查链上是否已有
            if (shardora_host.view_block_chain_) {
                auto chain_info = shardora_host.view_block_chain_->ChainGetAccountInfo(target_str);
                if (!chain_info || chain_info->bytes_code().empty()) {
                    needs_deploy = true;
                }
            } else {
                needs_deploy = true;
            }
        }

        if (needs_deploy) {
            // Solidity CrossShardBase storage layout:
            //   slot 0: IS_ROOT (bool,1B) + BASE_ROOT_ADDRESS (address,20B) packed
            //     bytes[0..10]=0, bytes[11..30]=base_root_address, bytes[31]=IS_ROOT(0)
            //   slot 1: SYSTEM_EXECUTOR (address,20B)
            //     bytes[0..11]=0, bytes[12..31]=system_executor
            //   kIsCrossShardBaseSlot: 1

            evmc::bytes32 slot0_key{};  // bytes32(0)
            evmc::bytes32 slot0_val{};  // IS_ROOT=0, BASE_ROOT_ADDRESS=base_raw
            memcpy(&slot0_val.bytes[11], base_raw.data(), 20);

            evmc::bytes32 slot1_key{};  // bytes32(1)
            slot1_key.bytes[31] = 1;
            evmc::bytes32 slot1_val{};  // SYSTEM_EXECUTOR
            memcpy(&slot1_val.bytes[12], shardoravm::kCrossShardSystemExecutor.bytes, 20);

            evmc::bytes32 marker_val{};
            marker_val.bytes[31] = 1;

            shardora_host.set_storage(target_evmc, slot0_key, slot0_val);
            shardora_host.set_storage(target_evmc, slot1_key, slot1_val);
            shardora_host.set_storage(target_evmc, shardoravm::kIsCrossShardBaseSlot, marker_val);
            shardora_host.accounts_[target_evmc].code =
                evmc::bytes(bytecode.begin(), bytecode.end());

            auto derived_info = std::make_shared<address::protobuf::AddressInfo>();
            derived_info->set_addr(target_str);
            derived_info->set_sharding_id(shard_id);
            derived_info->set_pool_index(pool_index);
            derived_info->set_type(address::protobuf::kNormal);
            derived_info->set_bytes_code(bytecode);
            derived_info->set_latest_height(view_block.block_info().height());
            derived_info->set_tx_index(tx_index);
            // balance/nonce must be explicitly set (even to 0) so that has_balance()/has_nonce()
            // return true and block_acceptor.cc includes this entry in address_array.
            // Without this, the shadow contract bytecode is silently dropped and never committed.
            derived_info->set_balance(0);
            derived_info->set_nonce(0);
            acc_balance_map[target_str] = derived_info;

            SHARDORA_INFO("CrossShardBase lazy-deploy: base=%s derived=%s shard=%u pool=%u",
                common::Encode::HexEncode(base_raw).c_str(),
                common::Encode::HexEncode(target_str).c_str(),
                shard_id, pool_index);
        }
    } else {
        SHARDORA_INFO("CrossShardBase: target is root contract's home shard, "
                   "operating directly on base=%s shard=%u pool=%u",
            common::Encode::HexEncode(base_raw).c_str(),
            shard_id, pool_index);
    }

    // ── 3. ABI 编码 calldata ─────────────────────────────────────────────────
    // 选择器（懒计算，static local）
    static const std::string kSelTransfer = []() {
        std::string h = common::Hash::keccak256(
            "systemExecuteCrossTransfer(address,uint256,uint64)");
        return h.substr(0, 4);
    }();
    static const std::string kSelStorage = []() {
        std::string h = common::Hash::keccak256(
            "systemExecuteCrossStorage(bytes32,bytes,uint64)");
        return h.substr(0, 4);
    }();

    std::string calldata;

    // Encode a uint64 as a left-zero-padded ABI uint256 word (32 bytes).
    auto write_uint256 = [](std::string& out, uint64_t v) {
        out.append(24, '\0');
        for (int i = 7; i >= 0; --i)
            out += static_cast<char>((v >> (8 * i)) & 0xFF);
    };
    // Encode a raw big-endian bytes field as an ABI uint256 word (32 bytes).
    // If bytes256 is exactly 32 bytes, write it directly; otherwise fall back
    // to the truncated uint64 value so old items with no amount256 still work.
    auto write_uint256_bytes = [&write_uint256](std::string& out,
                                                const std::string& bytes256,
                                                uint64_t fallback) {
        if (bytes256.size() == 32) {
            out += bytes256;
        } else {
            write_uint256(out, fallback);
        }
    };
    auto write_addr = [](std::string& out, const std::string& addr) {
        out.append(12, '\0');
        out += addr;  // 20 bytes
    };

    if (to_tx.cross_storage_kv_size() == 0) {
        // systemExecuteCrossTransfer(address to, uint256 amount, uint64 nonce)
        // selector(4) + to(32) + amount(32) + nonce(32) = 100 bytes
        calldata.reserve(100);
        calldata += kSelTransfer;
        write_addr(calldata, to_tx.des().size() == 20 ? to_tx.des()
                                                       : to_tx.des().substr(0, 20));
        write_uint256_bytes(calldata, to_tx.amount256(), to_tx.amount());
        write_uint256(calldata, to_tx.cross_nonce());

        // ── 4. 执行 EVM 调用 ─────────────────────────────────────────────────
        block::protobuf::BlockTx sys_tx;
        sys_tx.set_to(target_str);
        sys_tx.set_from(sys_exec_str);
        InitHost(shardora_host, sys_tx, 200000, 0, view_block);

        evmc::Result exec_res{ evmc_result{} };
        int exec_status = shardoravm::Execution::Instance()->execute(
            bytecode, calldata,
            sys_exec_str, target_str, sys_exec_str,
            0, 200000, 0,
            shardoravm::kJustCall, shardora_host, &exec_res);

        if (exec_status != shardoravm::kShardoravmSuccess ||
                exec_res.status_code != EVMC_SUCCESS) {
            SHARDORA_ERROR("CrossShardBase system call failed: exec=%d evmc=%d, base=%s target=%s",
                exec_status, (int)exec_res.status_code,
                common::Encode::HexEncode(base_raw).c_str(),
                common::Encode::HexEncode(target_str).c_str());
            return;
        }
        SHARDORA_INFO("CrossShardBase system call OK: base=%s target=%s nonce=%lu",
            common::Encode::HexEncode(base_raw).c_str(),
            common::Encode::HexEncode(target_str).c_str(),
            to_tx.cross_nonce());
    } else {
        // systemExecuteCrossStorage — one EVM call per CrossStorageKV entry.
        int n = to_tx.cross_storage_kv_size();
        for (int i = 0; i < n; ++i) {
            const auto& kv  = to_tx.cross_storage_kv(i);
            const std::string& key = kv.key();
            const std::string& val = kv.value();

            uint64_t version    = to_tx.cross_nonce() + static_cast<uint64_t>(i);
            size_t   val_padded = ((val.size() + 31) / 32) * 32;

            calldata.clear();
            calldata.reserve(4 + 32 * 4 + val_padded);
            calldata += kSelStorage;
            calldata.append(32 - std::min(key.size(), size_t(32)), '\0');
            calldata += key.substr(0, std::min(key.size(), size_t(32)));
            calldata.append(31, '\0');
            calldata += static_cast<char>(96);
            write_uint256(calldata, version);
            write_uint256(calldata, static_cast<uint64_t>(val.size()));
            calldata += val;
            if (val.size() < val_padded)
                calldata.append(val_padded - val.size(), '\0');

            block::protobuf::BlockTx sys_tx;
            sys_tx.set_to(target_str);
            sys_tx.set_from(sys_exec_str);
            InitHost(shardora_host, sys_tx, 200000, 0, view_block);

            evmc::Result exec_res{ evmc_result{} };
            int exec_status = shardoravm::Execution::Instance()->execute(
                bytecode, calldata,
                sys_exec_str, target_str, sys_exec_str,
                0, 200000, 0,
                shardoravm::kJustCall, shardora_host, &exec_res);

            if (exec_status != shardoravm::kShardoravmSuccess ||
                    exec_res.status_code != EVMC_SUCCESS) {
                SHARDORA_ERROR("CrossShardBase storage call[%d] failed: exec=%d evmc=%d, base=%s target=%s",
                    i, exec_status, (int)exec_res.status_code,
                    common::Encode::HexEncode(base_raw).c_str(),
                    common::Encode::HexEncode(target_str).c_str());
                return;
            }
            SHARDORA_INFO("CrossShardBase storage call[%d] OK: base=%s target=%s version=%lu",
                i, common::Encode::HexEncode(base_raw).c_str(),
                common::Encode::HexEncode(target_str).c_str(), version);
        }
        return;
    }
}

int ToTxLocalItem::TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) {
    if (!DefaultTxItem(tx_info, block_tx)) {
        return consensus::kConsensusError;
    }

    return consensus::kConsensusSuccess;
}

};  // namespace consensus

};  // namespace shardora




