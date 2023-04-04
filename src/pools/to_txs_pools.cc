#include "pools/to_txs_pools.h"

#include "common/global_info.h"
#include "common/user_property_key_define.h"
#include "network/network_utils.h"
#include "network/route.h"

namespace zjchain {

namespace pools {

ToTxsPools::ToTxsPools(
        std::shared_ptr<db::Db>& db,
        const std::string& local_id,
        uint32_t max_sharding_id) : db_(db), local_id_(local_id) {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    PoolMap pool_map;
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        pool_map[i] = HeightMap();
    }

    for (uint32_t i = network::kRootCongressNetworkId; i <= max_sharding_id; ++i) {
        network_txs_pools_[i] = pool_map;
    }

    LoadLatestHeights();
    address_map_.Init(10240, 16);
}

ToTxsPools::~ToTxsPools() {}

void ToTxsPools::NewBlock(const block::protobuf::Block& block, db::DbWriteBatch& db_batch) {
    if (block.network_id() != common::GlobalInfo::Instance()->network_id()) {
        ZJC_DEBUG("network invalid!");
        return;
    }

    if (block.height() > pool_max_heihgts_[block.pool_index()]) {
        pool_max_heihgts_[block.pool_index()] = block.height();
    }

    if (pool_consensus_heihgts_[block.pool_index()] + 1 == block.height()) {
        ++pool_consensus_heihgts_[block.pool_index()];
        for (; pool_consensus_heihgts_[block.pool_index()] <= pool_max_heihgts_[block.pool_index()];
                ++pool_consensus_heihgts_[block.pool_index()]) {
            auto iter = added_heights_[block.pool_index()].find(
                    pool_consensus_heihgts_[block.pool_index()] + 1);
            if (iter == added_heights_[block.pool_index()].end()) {
                break;
            }

            added_heights_[block.pool_index()].erase(iter);
        }
    } else {
        added_heights_[block.pool_index()].insert(block.height());
    }

    const auto& tx_list = block.tx_list();
    if (tx_list.empty()) {
        ZJC_DEBUG("tx list empty!");
        return;
    }
    
    // one block must be one consensus pool
    uint32_t consistent_pool_index = common::kInvalidPoolIndex;
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        if (tx_list[i].step() == pools::protobuf::kNormalTo ||
                tx_list[i].step() == pools::protobuf::kRootCreateAddressCrossSharding) {
            HandleNormalToTx(block, tx_list[i], db_batch);
        }

        if (tx_list[i].step() == pools::protobuf::kContractUserCreateCall) {
            // save contract address contract info
            if (tx_list[i].has_contract_code()) {
                prefix_db_->SaveAddressTmpBytesCode(
                    tx_list[i].to(),
                    tx_list[i].SerializeAsString(),
                    db_batch);
            }

            HandleNormalFrom(block, tx_list[i], db_batch);
        }

        if (tx_list[i].step() == pools::protobuf::kNormalFrom) {
            HandleNormalFrom(block, tx_list[i], db_batch);
        }

        if (tx_list[i].step() == pools::protobuf::kRootCreateAddress) {
            HandleRootCreateAddress(block, tx_list[i], db_batch);
        }
    }
}

void ToTxsPools::HandleNormalFrom(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    if (tx.amount() <= 0) {
        ZJC_DEBUG("from transfer amount invalid!");
        return;
    }

    uint32_t sharding_id = common::kInvalidUint32;
    auto addr_info = GetAddressInfo(tx.to());
    if (addr_info == nullptr) {
        sharding_id = network::kRootCongressNetworkId;
    } else {
        sharding_id = addr_info->sharding_id();
    }

    AddTxToMap(block, tx, sharding_id, -1);
}

void ToTxsPools::HandleRootCreateAddress(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        db::DbWriteBatch& db_batch) {
    if (tx.amount() <= 0) {
        ZJC_DEBUG("from transfer amount invalid!");
        return;
    }

    uint32_t sharding_id = common::kInvalidUint32;
    int32_t pool_index = common::kInvalidPoolIndex;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kRootCreateAddressKey) {
            auto* data = (const uint32_t*)tx.storages(i).val_hash().c_str();
            sharding_id = data[0];
            pool_index  = data[1];
            break;
        }
    }

    if (sharding_id == common::kInvalidUint32 || pool_index == common::kInvalidPoolIndex) {
        assert(false);
        return;
    }

    AddTxToMap(block, tx, sharding_id, pool_index);
}

void ToTxsPools::AddTxToMap(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx,
        uint64_t sharding_id,
        int32_t pool_index) {
    auto handled_iter = handled_map_.find(sharding_id);
    if (handled_iter != handled_map_.end()) {
        if (handled_iter->second->heights(block.pool_index()) >= block.height()) {
            ZJC_DEBUG("height invalid, %lu: %lu, sharding: %u, pool: %u!",
                handled_iter->second->heights(block.pool_index()),
                block.height(),
                block.network_id(),
                block.pool_index());
            return;
        }
    }

    auto net_iter = network_txs_pools_.find(sharding_id);
    if (net_iter == network_txs_pools_.end()) {
        PoolMap pool_map;
        network_txs_pools_[sharding_id] = pool_map;
        net_iter = network_txs_pools_.find(sharding_id);
    }

    auto pool_iter = net_iter->second.find(block.pool_index());
    if (pool_iter == net_iter->second.end()) {
        HeightMap height_map;
        net_iter->second[block.pool_index()] = height_map;
        pool_iter = net_iter->second.find(block.pool_index());
    }

    auto height_iter = pool_iter->second.find(block.height());
    if (height_iter == pool_iter->second.end()) {
        TxMap tx_map;
        pool_iter->second[block.height()] = tx_map;
        height_iter = pool_iter->second.find(block.height());
    }

    auto to_iter = height_iter->second.find(tx.to());
    if (to_iter == height_iter->second.end()) {
        ToAddressItemInfo item;
        item.amount = 0lu;
        item.pool_index = pool_index;
        item.type = tx.step();
        height_iter->second[tx.to()] = item;
        ZJC_DEBUG("add to %s step: %u", common::Encode::HexEncode(tx.to()).c_str(), tx.step());
    }

    height_iter->second[tx.to()].amount += tx.amount();
}

void ToTxsPools::HandleNormalToTx(
        const block::protobuf::Block& block,
        const block::protobuf::BlockTx& tx_info,
        db::DbWriteBatch& db_batch) {
    for (auto net_iter = network_txs_pools_.begin();
            net_iter != network_txs_pools_.end(); ++net_iter) {
        auto handled_iter = handled_map_.find(net_iter->first);
        if (handled_iter != handled_map_.end()) {
            if (handled_iter->second->heights(block.pool_index()) >= block.height()) {
                continue;
            }
        }

        TxMap tx_map;
        // just clear and reload txs, height must unique
        net_iter->second[block.pool_index()][block.height()] = tx_map;
    }

    if (tx_info.storages_size() <= 0) {
        assert(false);
        return;
    }

    auto heights_ptr = std::make_shared<pools::protobuf::ToTxHeights>();
    auto& heights = *heights_ptr;
    heights.set_block_height(block.height());
    for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
        if (tx_info.storages(i).key() == protos::kNormalTos) {
            std::string to_txs_str;
            if (!prefix_db_->GetTemporaryKv(tx_info.storages(i).val_hash(), &to_txs_str)) {
                ZJC_WARN("get to tx heights failed: %s!",
                    common::Encode::HexEncode(tx_info.storages(i).val_hash()).c_str());
                return;
            }
            
            pools::protobuf::ToTxMessage to_tx;
            if (!to_tx.ParseFromString(to_txs_str)) {
                ZJC_WARN("parse from to txs message failed: %s",
                    common::Encode::HexEncode(tx_info.storages(i).val_hash()).c_str());
                return;
            }

            heights = to_tx.to_heights();
            break;
        }
    }

    if (heights.heights_size() != common::kImmutablePoolSize) {
        ZJC_ERROR("invalid heights size: %d, %d",
            heights.heights_size(), common::kImmutablePoolSize);
        return;
    }

    ZJC_DEBUG("new to tx coming: %lu, sharding id: %u", block.height(), heights.sharding_id());
    auto handled_iter = handled_map_.find(heights.sharding_id());
    if (handled_iter != handled_map_.end()) {
        if (handled_iter->second->block_height() >= block.height()) {
            ZJC_WARN("block_height failed: %lu, %lu!",
                handled_iter->second->block_height(), block.height());
            return;
        }
    }

    handled_map_[heights.sharding_id()] = heights_ptr;
    prefix_db_->SaveLatestToTxsHeights(heights, db_batch);
    auto net_iter = network_txs_pools_.find(heights.sharding_id());
    if (net_iter == network_txs_pools_.end()) {
        ZJC_DEBUG("no sharding exists.");
        return;
    }

    for (int32_t i = 0; i < heights.heights_size(); ++i) {
        auto pool_iter = net_iter->second.find(i);
        if (pool_iter == net_iter->second.end()) {
            continue;
        }

        auto height_iter = pool_iter->second.begin();
        while (height_iter != pool_iter->second.end()) {
            if (height_iter->first > heights.heights(i)) {
                break;
            }

//             ZJC_DEBUG("erase sharding: %u, height: %lu", heights.sharding_id(), height_iter->first);
            pool_iter->second.erase(height_iter++);
        }
    }
}

void ToTxsPools::LoadLatestHeights() {
    for (uint32_t i = network::kRootCongressNetworkId;
            i < network::kConsensusShardEndNetworkId; ++i) {
        auto heights_ptr = std::make_shared<pools::protobuf::ToTxHeights>();
        pools::protobuf::ToTxHeights& to_heights = *heights_ptr;
        if (!prefix_db_->GetLatestToTxsHeights(i, &to_heights)) {
            continue;
        }

        handled_map_[i] = heights_ptr;
    }
}

std::shared_ptr<address::protobuf::AddressInfo> ToTxsPools::GetAddressInfo(
        const std::string& addr) {
    // first get from cache
    std::shared_ptr<address::protobuf::AddressInfo> address_info = nullptr;
    if (address_map_.get(addr, &address_info)) {
        return address_info;
    }

    // get from db and add to memory cache
    address_info = prefix_db_->GetAddressInfo(addr);
    if (address_info != nullptr) {
        address_map_.add(addr, address_info);
    }

    return address_info;
}

int ToTxsPools::LeaderCreateToHeights(
        uint32_t sharding_id,
        pools::protobuf::ToTxHeights& to_heights) {
    auto net_iter = network_txs_pools_.find(sharding_id);
    if (net_iter == network_txs_pools_.end()) {
        return kPoolsError;
    }

    to_heights.set_sharding_id(sharding_id);
    bool valid = false;
    std::string heights;
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        auto pool_iter = net_iter->second.find(i);
        auto r_height_iter = pool_iter->second.rbegin();
        if (r_height_iter == pool_iter->second.rend()) {
            heights += std::to_string(0) + " ";
            to_heights.add_heights(0);
        } else {
            to_heights.add_heights(r_height_iter->first);
            heights += std::to_string(r_height_iter->first) + " ";
            valid = true;
        }
    }

    if (!valid) {
        return kPoolsError;
    }

    ZJC_DEBUG("sharding_id: %u, leader success create to heights: %s", sharding_id, heights.c_str());
    return kPoolsSuccess;
}

int ToTxsPools::CreateToTxWithHeights(
        uint32_t sharding_id,
        const pools::protobuf::ToTxHeights& leader_to_heights,
        std::string* to_hash) {
    auto net_iter = network_txs_pools_.find(sharding_id);
    if (net_iter == network_txs_pools_.end()) {
        assert(false);
        return kPoolsError;
    }

    if (leader_to_heights.heights_size() != common::kImmutablePoolSize) {
        assert(false);
        return kPoolsError;
    }

    auto handled_iter = handled_map_.find(sharding_id);
    std::map<std::string, ToAddressItemInfo> acc_amount_map;
    for (uint32_t pool_idx = 0; pool_idx < common::kImmutablePoolSize; ++pool_idx) {
        auto pool_iter = net_iter->second.find(pool_idx);
        if (pool_iter == net_iter->second.end()) {
            assert(false);
            return kPoolsError;
        }

        uint64_t min_height = 1;
        if (handled_iter != handled_map_.end()) {
            min_height = handled_iter->second->heights(pool_idx) + 1;
        }

        uint64_t max_height = leader_to_heights.heights(pool_idx);
        if (max_height > pool_consensus_heihgts_[pool_idx]) {
            ZJC_DEBUG("pool %u, invalid height: %lu, consensus height: %lu",
                pool_idx, max_height, pool_consensus_heihgts_[pool_idx]);
            return kPoolsError;
        }

        if (max_height > 0) {
            ZJC_DEBUG("sharding_id: %u, pool: %d, min_height: %lu, max_height: %lu", sharding_id, pool_idx, min_height, max_height);
        }

        for (auto height = min_height; height <= max_height; ++height) {
            auto hiter = pool_iter->second.find(height);
            for (auto to_iter = hiter->second.begin();
                    to_iter != hiter->second.end(); ++to_iter) {
                auto amount_iter = acc_amount_map.find(to_iter->first);
                if (amount_iter == acc_amount_map.end()) {
                    acc_amount_map[to_iter->first] = to_iter->second;
                } else {
                    amount_iter->second.amount += to_iter->second.amount;
                }
            }
        }
    }

    if (acc_amount_map.empty()) {
//         assert(false);
        return kPoolsError;
    }

    std::string str_for_hash;
    str_for_hash.reserve(common::kImmutablePoolSize * 8 + acc_amount_map.size() * 48);
    std::string test_heights;
    for (uint32_t i = 0; i < common::kImmutablePoolSize; ++i) {
        auto height = leader_to_heights.heights(i);
        str_for_hash.append((char*)&height, sizeof(height));
        test_heights += std::to_string(height) + " ";
    }

    pools::protobuf::ToTxMessage to_tx;
    for (auto iter = acc_amount_map.begin(); iter != acc_amount_map.end(); ++iter) {
        str_for_hash.append(iter->first);
        str_for_hash.append((char*)&iter->second.amount, sizeof(iter->second.amount));
        str_for_hash.append((char*)&sharding_id, sizeof(sharding_id));
        str_for_hash.append((char*)&iter->second.pool_index, sizeof(iter->second.pool_index));
        str_for_hash.append((char*)&iter->second.type, sizeof(iter->second.type));
        auto to_item = to_tx.add_tos();
        to_item->set_des(iter->first);
        to_item->set_amount(iter->second.amount);
        to_item->set_pool_index(iter->second.pool_index);
        // create contract just in caller sharding
        if (iter->second.type == pools::protobuf::kContractUserCreateCall) {
            assert(common::GlobalInfo::Instance()->network_id() > network::kRootCongressNetworkId);
            to_item->set_sharding_id(common::GlobalInfo::Instance()->network_id());
            ZJC_DEBUG("create contract use caller sharding address: %s, %u",
                common::Encode::HexEncode(iter->first).c_str(),
                common::GlobalInfo::Instance()->network_id());
        } else if (iter->second.type == pools::protobuf::kRootCreateAddress) {
            to_item->set_sharding_id(sharding_id);
            ZJC_DEBUG("root create sharding address: %s, %u, pool: %u",
                common::Encode::HexEncode(iter->first).c_str(),
                sharding_id,
                iter->second.pool_index);
        } else {
            to_item->set_sharding_id(common::kInvalidUint32);
        }

        ZJC_DEBUG("set to %s amount %lu, sharding id: %u, pool index: %d",
            common::Encode::HexEncode(iter->first).c_str(),
            iter->second.amount, to_item->sharding_id(), iter->second.pool_index);
    }

    *to_hash = common::Hash::keccak256(str_for_hash);
    ZJC_DEBUG("backup sharding: %u to_hash: %s, test_heights: %s",
        sharding_id,
        common::Encode::HexEncode(*to_hash).c_str(),
        test_heights.c_str());
    to_tx.set_heights_hash(*to_hash);
    *to_tx.mutable_to_heights() = leader_to_heights;
    auto val = to_tx.SerializeAsString();
    prefix_db_->SaveTemporaryKv(*to_hash, val);
    return kPoolsSuccess;
}

};  // namespace pools

};  // namespace zjchain
