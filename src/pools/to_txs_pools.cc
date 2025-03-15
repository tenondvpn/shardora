#include "pools/to_txs_pools.h"

#include "block/account_manager.h"
#include "consensus/consensus_utils.h"
#include "common/global_info.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "pools/tx_pool_manager.h"
#include "protos/get_proto_hash.h"
#include <protos/pools.pb.h>

namespace shardora {

namespace pools {

ToTxsPools::ToTxsPools(
        std::shared_ptr<db::Db>& db,
        const std::string& local_id,
        uint32_t max_sharding_id,
        std::shared_ptr<pools::TxPoolManager>& pools_mgr,
        std::shared_ptr<block::AccountManager>& acc_mgr)
        : db_(db), local_id_(local_id), pools_mgr_(pools_mgr), acc_mgr_(acc_mgr) {
    prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
    if (pools_mgr_ != nullptr) {
        LoadLatestHeights();
    }
}

ToTxsPools::~ToTxsPools() {}

void ToTxsPools::NewBlock(
        const std::shared_ptr<view_block::protobuf::ViewBlockItem>& view_block_ptr) {
#ifdef TEST_NO_CROSS
    return;
#endif
    auto& block = view_block_ptr->block_info();
    if (view_block_ptr->qc().network_id() != common::GlobalInfo::Instance()->network_id() &&
            view_block_ptr->qc().network_id() + network::kConsensusWaitingShardOffset !=
            common::GlobalInfo::Instance()->network_id()) {
        ZJC_DEBUG("network invalid: %d, local: %d", view_block_ptr->qc().network_id(), common::GlobalInfo::Instance()->network_id());
        return;
    }

    // 更新 pool 的 max height
    auto pool_idx = view_block_ptr->qc().pool_index();
    if (block.height() > pool_max_heihgts_[pool_idx]) {
        pool_max_heihgts_[pool_idx] = block.height();
    }

    if (pool_consensus_heihgts_[pool_idx] + 1 == block.height()) {
        ++pool_consensus_heihgts_[pool_idx];
        for (; pool_consensus_heihgts_[pool_idx] <= pool_max_heihgts_[pool_idx];
                ++pool_consensus_heihgts_[pool_idx]) {
            auto iter = added_heights_[pool_idx].find(
                    pool_consensus_heihgts_[pool_idx] + 1);
            if (iter == added_heights_[pool_idx].end()) {
                break;
            }
        }
    }

#ifndef NDEBUG
    transport::protobuf::ConsensusDebug cons_debug;
    cons_debug.ParseFromString( view_block_ptr->debug());
    ZJC_DEBUG("to txs new block coming pool: %u, height: %lu, "
        "cons height: %lu, tx size: %d, propose_debug: %s, step: %d, tx status: %d",
        pool_idx, 
        block.height(), 
        pool_consensus_heihgts_[pool_idx], 
        view_block_ptr->block_info().tx_list_size(),
        ProtobufToJson(cons_debug).c_str(),
        (view_block_ptr->block_info().tx_list_size() > 0 ? view_block_ptr->block_info().tx_list(0).step() : -1),
        (view_block_ptr->block_info().tx_list_size() > 0 ? view_block_ptr->block_info().tx_list(0).status() : -1));
#endif
    StatisticToInfo(*view_block_ptr);

    added_heights_[pool_idx].insert(std::make_pair<>(
        block.height(), 
        view_block_ptr->block_info().timestamp()));
    auto added_heights_iter = added_heights_[pool_idx].begin();
    while (added_heights_iter != added_heights_[pool_idx].end()) {
        if (added_heights_iter->first > erased_max_heights_[pool_idx]) {
            break;
        }

        added_heights_iter = added_heights_[pool_idx].erase(added_heights_iter);
    }

    auto cross_map_iter = cross_sharding_map_[pool_idx].begin();
    while (cross_map_iter != cross_sharding_map_[pool_idx].end()) {
        if (cross_map_iter->first > erased_max_heights_[pool_idx]) {
            break;
        }

        cross_map_iter = cross_sharding_map_[pool_idx].erase(cross_map_iter);
    }

    CHECK_MEMORY_SIZE_WITH_MESSAGE(added_heights_[pool_idx], std::to_string(pool_idx).c_str());
    valided_heights_[pool_idx].insert(block.height());
}

void ToTxsPools::StatisticToInfo(
        const view_block::protobuf::ViewBlockItem& view_block) {
    auto& block = view_block.block_info();
    const auto& tx_list = block.tx_list();
#ifndef ENABLE_HOTSTUFF
    if (tx_list.empty()) {
        assert(false);
        ZJC_DEBUG("tx list empty!");
        return;
    }
#endif

    // one block must be one consensus pool
    uint32_t consistent_pool_index = common::kInvalidPoolIndex;
    std::unordered_map<uint32_t, std::unordered_set<CrossItem, CrossItemRecordHash>> cross_map;
    // ZJC_DEBUG("now handle block net: %u, pool: %u, height: %lu, tx size: %u",
    //     common::GlobalInfo::Instance()->network_id(), pool_idx, height, tx_list.size());
    for (int32_t i = 0; i < tx_list.size(); ++i) {
        if (tx_list[i].status() != consensus::kConsensusSuccess) {
            ZJC_INFO("tx status error: %d, gid: %s, net: %u, pool: %u, height: %lu, hash: %s",
                tx_list[i].status(), common::Encode::HexEncode(tx_list[i].gid()).c_str(),
                view_block.qc().network_id(), view_block.qc().pool_index(), block.height(),
                common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str());
//                 assert(false);
            continue;
        }

        HandleCrossShard(IsRootNode(), view_block, tx_list[i], cross_map);
        // ZJC_DEBUG("now handle block net: %u, pool: %u, height: %lu, step: %u",
        //     common::GlobalInfo::Instance()->network_id(), pool_idx, height, tx_list[i].step());
        switch (tx_list[i].step()) {
        case pools::protobuf::kNormalTo:
            HandleNormalToTx(view_block, tx_list[i]);
            break;
        case pools::protobuf::kCreateLibrary:
        case pools::protobuf::kContractCreate:
            HandleCreateContractUserCall(view_block, tx_list[i]);
            break;
        case pools::protobuf::kContractCreateByRootFrom:
            HandleCreateContractByRootFrom(view_block, tx_list[i]);
            break;
        case pools::protobuf::kContractGasPrepayment:
            HandleContractGasPrepayment(view_block, tx_list[i]);
            break;
        case pools::protobuf::kNormalFrom:
            HandleNormalFrom(view_block, tx_list[i]);
            break;
        case pools::protobuf::kRootCreateAddress:
            HandleRootCreateAddress(view_block, tx_list[i]);
            break;
        case pools::protobuf::kContractExcute:
            HandleContractExecute(view_block, tx_list[i]);
            break;
        case pools::protobuf::kJoinElect:
            HandleJoinElect(view_block, tx_list[i]);
            break;
        default:
            break;
        }
    }

    if (!cross_map.empty()) {
        cross_sharding_map_[view_block.qc().pool_index()][block.height()] = cross_map;
        CHECK_MEMORY_SIZE(cross_sharding_map_[view_block.qc().pool_index()]);
    }
}

void ToTxsPools::HandleJoinElect(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx) {
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kJoinElectVerifyG2) {
            // distinct with transfer transaction
            ZJC_DEBUG("success add join elect value: %d, %s, %d",
                network::kRootCongressNetworkId, 
                tx.storages(i).key().c_str(), 
                tx.storages(i).value().size());
            AddTxToMap(
                view_block,
                tx.from(),
                tx.step(),
                0,
                network::kRootCongressNetworkId,
                view_block.qc().pool_index(),
                tx.storages(i).value(), "", "", 0);
        }
    }
}

void ToTxsPools::HandleContractExecute(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx) {
    for (int32_t i = 0; i < tx.contract_txs_size(); ++i) {
        uint32_t sharding_id = common::kInvalidUint32;
        uint32_t pool_index = -1;
        // 如果需要 root 创建则此时没有 addr info 
        protos::AddressInfoPtr addr_info = acc_mgr_->GetAccountInfo(tx.contract_txs(i).to());
        if (addr_info != nullptr) {
            sharding_id = addr_info->sharding_id();
        }

        ZJC_DEBUG("add contract execute to: %s, %lu",
            common::Encode::HexEncode(tx.contract_txs(i).to()).c_str(),
            tx.contract_txs(i).amount());
        
        AddTxToMap(
            view_block,
            tx.contract_txs(i).to(),
            pools::protobuf::kNormalFrom,
            tx.contract_txs(i).amount(),
            sharding_id,
            pool_index,
            "", "", "", 0);
    }
}

void ToTxsPools::HandleContractGasPrepayment(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx) {
    ZJC_DEBUG("now get contract address: %s, from: %s, gid: %s, prepayment: %lu, amount: %lu",
        common::Encode::HexEncode(tx.to()).c_str(),
        common::Encode::HexEncode(tx.from()).c_str(),
        common::Encode::HexEncode(tx.gid()).c_str(),
        tx.contract_prepayment(),
        tx.amount());
    if (tx.amount() > 0) {
        HandleNormalFrom(view_block, tx);
    }

    if (tx.contract_prepayment() > 0) {
        uint32_t sharding_id = common::kInvalidUint32;
        uint32_t pool_index = -1;
        protos::AddressInfoPtr addr_info = acc_mgr_->GetAccountInfo(tx.to());
        if (addr_info != nullptr) {
            sharding_id = addr_info->sharding_id();
            ZJC_DEBUG("success get contract address: %s, from: %s, gid: %s, "
                "prepayment: %lu, sharding_id: %u",
                common::Encode::HexEncode(tx.to()).c_str(),
                common::Encode::HexEncode(tx.from()).c_str(),
                common::Encode::HexEncode(tx.gid()).c_str(),
                tx.contract_prepayment(),
                sharding_id);
        } else {
            ZJC_DEBUG("failed get contract address: %s, from: %s, gid: %s, prepayment: %lu",
                common::Encode::HexEncode(tx.to()).c_str(),
                common::Encode::HexEncode(tx.from()).c_str(),
                common::Encode::HexEncode(tx.gid()).c_str(),
                tx.contract_prepayment());
        }

        // gas prepayment contain contract address and user's address
        AddTxToMap(
            view_block,
            tx.to() + tx.from(),
            pools::protobuf::kContractGasPrepayment,
            tx.contract_prepayment(), // prepayment 通过 amount 字段传递, TODO 改为 prepayment 字段
            sharding_id,
            pool_index,
            "", "", "", 0);
    }
}

void ToTxsPools::HandleNormalFrom(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx) {
    if (tx.amount() <= 0) {
        ZJC_DEBUG("from transfer amount invalid!");
        return;
    }

    uint32_t sharding_id = common::kInvalidUint32;
    uint32_t pool_index = -1;
    protos::AddressInfoPtr addr_info = acc_mgr_->GetAccountInfo(tx.to());
    if (addr_info != nullptr) {
        sharding_id = addr_info->sharding_id();
    }

    AddTxToMap(view_block, tx.to(), tx.step(), tx.amount(), sharding_id, pool_index, "", "", "", 0);
}

void ToTxsPools::HandleCreateContractUserCall(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx) {
    uint32_t sharding_id = network::kRootCongressNetworkId;
    uint32_t pool_index = view_block.qc().pool_index();
    if (tx.step() == pools::protobuf::kCreateLibrary) {
        AddTxToMap(
            view_block, tx.to(), tx.step(), tx.amount(), sharding_id, pool_index, "", 
            tx.contract_code(), tx.from(), 0);
    } else {
        AddTxToMap(
            view_block, tx.to(), tx.step(), tx.amount(), sharding_id, pool_index, "", 
            "", "", 0);
    }
    
    for (int32_t i = 0; i < tx.contract_txs_size(); ++i) {
        uint32_t sharding_id = common::kInvalidUint32;
        uint32_t pool_index = -1;
        protos::AddressInfoPtr addr_info = acc_mgr_->GetAccountInfo(tx.contract_txs(i).to());
        if (addr_info != nullptr) {
            sharding_id = addr_info->sharding_id();
        }

        AddTxToMap(
            view_block,
            tx.contract_txs(i).to(),
            pools::protobuf::kNormalFrom,
            tx.contract_txs(i).amount(),
            sharding_id,
            pool_index,
            "", "", "", 0);
    }
}

void ToTxsPools::HandleCreateContractByRootFrom(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx) {
    uint32_t sharding_id = network::kRootCongressNetworkId;
    uint32_t pool_index = view_block.qc().pool_index();

    std::string bytes_code;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kCreateContractBytesCode) {
            bytes_code = tx.storages(i).value();
            break;
        }
    }
    AddTxToMap(view_block, tx.to(), tx.step(), tx.amount(), sharding_id, pool_index, "", bytes_code, tx.from(), tx.contract_prepayment());
}

// Only for Root
void ToTxsPools::HandleRootCreateAddress(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx) {
    // 普通 EOA 账户要求有 amount
    if (tx.amount() <= 0 && !tx.has_contract_code()) {
        ZJC_DEBUG("from transfer amount invalid!");
        return;
    }

    uint32_t sharding_id = common::kInvalidUint32;
    int32_t pool_index = common::kInvalidPoolIndex;
    for (int32_t i = 0; i < tx.storages_size(); ++i) {
        if (tx.storages(i).key() == protos::kRootCreateAddressKey) {
            auto* data = (const uint32_t*)tx.storages(i).value().c_str();
            sharding_id = data[0];
            pool_index  = data[1];
            break;
        }
    }

    if (sharding_id == common::kInvalidUint32 || pool_index == common::kInvalidPoolIndex) {
        assert(false);
        return;
    }

    ZJC_DEBUG("success add root create address: %s sharding: %u, pool: %u", common::Encode::HexEncode(tx.to()).c_str(), sharding_id, pool_index);
	// 对于 contract create，要把 from、contract_code、prepayment 发给对应 shard
    AddTxToMap(view_block, tx.to(), tx.step(), tx.amount(), sharding_id, pool_index, "", tx.contract_code(), tx.from(), tx.contract_prepayment());
}

void ToTxsPools::AddTxToMap(
        const view_block::protobuf::ViewBlockItem& view_block,
        const std::string& in_to,
        pools::protobuf::StepType type,
        uint64_t amount,
        uint32_t sharding_id,
        int32_t pool_index,
        const std::string& key,
        const std::string& library_bytes,
        const std::string& from,
        uint64_t prepayment) {
    std::string to(in_to.size() + 4, '\0');
    char* tmp_to_data = to.data();
    memcpy(tmp_to_data + 4, in_to.c_str(), in_to.size());
    uint32_t* tmp_data = (uint32_t*)tmp_to_data;
    tmp_data[0] = type;
    common::AutoSpinLock auto_lock(network_txs_pools_mutex_);
    auto& height_map = network_txs_pools_[view_block.qc().pool_index()];
    auto height_iter = height_map.find(view_block.block_info().height());
    if (height_iter == height_map.end()) {
        TxMap tx_map;
        height_map[view_block.block_info().height()] = tx_map;
        height_iter = height_map.find(view_block.block_info().height());
        ZJC_DEBUG("success add block pool: %u, height: %lu",
            view_block.qc().pool_index(), view_block.block_info().height());
    }

    auto to_iter = height_iter->second.find(to);
    if (to_iter == height_iter->second.end()) {
        ToAddressItemInfo item;
        item.amount = 0lu;
        item.pool_index = pool_index;
        item.type = type;
        item.sharding_id = sharding_id;
        item.elect_join_g2_value = key;
        // for ContractCreate Tx
        if (library_bytes != "") {
            item.library_bytes = library_bytes;
            item.from = from;
            item.prepayment = prepayment;
        }
        
        height_iter->second[to] = item;
        ZJC_DEBUG("add to %s step: %u", common::Encode::HexEncode(to).c_str(), type);
    }
    
    height_iter->second[to].amount += amount;
    ZJC_DEBUG("to block pool: %u, height: %lu, success add block pool: %u, "
        "height: %lu, id: %s, amount: %lu, all amount: %lu, step: %u",
        view_block.qc().pool_index(), 
        view_block.block_info().height(), 
        view_block.qc().pool_index(), 
        view_block.block_info().height(), 
        common::Encode::HexEncode(to).c_str(), 
        amount, 
        height_iter->second[to].amount, 
        type);
}

void ToTxsPools::HandleNormalToTx(
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx_info) {
    if (tx_info.storages_size() <= 0) {
        assert(false);
        return;
    }

    auto heights_ptr = std::make_shared<pools::protobuf::ShardToTxItem>();
    for (int32_t i = 0; i < tx_info.storages_size(); ++i) {
        if (tx_info.storages(i).key() != protos::kNormalToShards) {
            continue;
        }
            
        pools::protobuf::ToTxMessage to_tx;
        if (!to_tx.ParseFromString(tx_info.storages(i).value())) {
            ZJC_WARN("parse from to txs message failed: %s",
                common::Encode::HexEncode(tx_info.storages(i).value()).c_str());
            assert(false);
            continue;
        }

        ZJC_DEBUG("success get normal to key: %s, val: %s, sharding id: %u",
            common::Encode::HexEncode(tx_info.storages(i).key()).c_str(),
            "common::Encode::HexEncode(tx_info.storages(i).value()).c_str()",
            to_tx.to_heights().sharding_id());
        if (to_tx.to_heights().heights_size() != common::kInvalidPoolIndex) {
            ZJC_ERROR("invalid heights size: %d, %d",
                to_tx.to_heights().heights_size(), common::kInvalidPoolIndex);
            assert(false);
            continue;
        }

        *heights_ptr = to_tx.to_heights();
        auto& heights = *heights_ptr;
        heights.set_block_height(view_block.block_info().height());
        ZJC_DEBUG("new to tx coming: %lu, sharding id: %u, to_tx: %s",
            view_block.block_info().height(), 
            heights.sharding_id(), 
            ProtobufToJson(to_tx).c_str());
        for (int32_t i = 0; i < heights.heights_size(); ++i) {
            if (heights.heights(i) > has_statistic_height_[i]) {
                has_statistic_height_[i] = heights.heights(i);
            }

            for (uint64_t erase_height = erased_max_heights_[i];
                    erase_height < heights.heights(i); ++erase_height) {
                valided_heights_[i].erase(erase_height);
            }

            if (heights.heights(i) > pool_consensus_heihgts_[i]) {
                pool_consensus_heihgts_[i] = heights.heights(i);
                for (; pool_consensus_heihgts_[i] <= pool_max_heihgts_[i];
                    ++pool_consensus_heihgts_[i]) {
                    ZJC_DEBUG("set new to tx height pool: %u, height: %lu", i, pool_consensus_heihgts_[i]);
                }
            }

            {
                common::AutoSpinLock auto_lock(network_txs_pools_mutex_);
                auto& height_map = network_txs_pools_[i];
                auto height_iter = height_map.begin();
                while (height_iter != height_map.end()) {
                    if (height_iter->first > heights.heights(i)) {
                        break;
                    }
    
                    ZJC_DEBUG("to block pool: %u, height: %lu, erase sharding: %u, pool: %u, height: %lu",
                        i, height_iter->first, heights.sharding_id(), i, height_iter->first);
                    height_map.erase(height_iter++);
                }
            }

            erased_max_heights_[i] = heights.heights(i) + 1;
        }

        common::AutoSpinLock lock(prev_to_heights_mutex_);
        prev_to_heights_ = heights_ptr;
        break;
    }
}

void ToTxsPools::LoadLatestHeights() {
    if (common::GlobalInfo::Instance()->network_id() == common::kInvalidUint32) {
        // assert(false);
        return;
    }

    auto heights_ptr = std::make_shared<pools::protobuf::ShardToTxItem>();
    pools::protobuf::ShardToTxItem& to_heights = *heights_ptr;
    auto net_id = common::GlobalInfo::Instance()->network_id();
    if (net_id >= network::kConsensusShardEndNetworkId) {
        net_id = net_id - network::kConsensusWaitingShardOffset;
    }

    if (!prefix_db_->GetLatestToTxsHeights(net_id, &to_heights)) {
        // assert(false);
        return;
    }

    {
        common::AutoSpinLock lock(prev_to_heights_mutex_);
        prev_to_heights_ = heights_ptr;
    }
    uint32_t max_pool_index = common::kImmutablePoolSize;
    // if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
    //     ++max_pool_index;
    // }

    if (heights_ptr != nullptr) {
        auto& this_net_heights = heights_ptr->heights();
        for (int32_t i = 0; i < this_net_heights.size(); ++i) {
            pool_consensus_heihgts_[i] = this_net_heights[i];
            has_statistic_height_[i] = this_net_heights[i];
            ZJC_DEBUG("set consensus height: %u, height: %lu", i, this_net_heights[i]);
        }
    }

    for (uint32_t i = 0; i <= max_pool_index; ++i) {
        uint64_t pool_latest_height = pools_mgr_->latest_height(i);
        if (pool_latest_height == common::kInvalidUint64) {
            continue;
        }

        bool consensus_stop = false;
        for (uint64_t height = pool_consensus_heihgts_[i];
                height <= pool_latest_height; ++height) {
            auto view_block_ptr = std::make_shared<view_block::protobuf::ViewBlockItem>();
            auto& view_block = *view_block_ptr;
            if (!prefix_db_->GetBlockWithHeight(
                    common::GlobalInfo::Instance()->network_id(), i, height, &view_block)) {
                consensus_stop = true;
            } else {
                NewBlock(view_block_ptr);
            }

            if (!consensus_stop) {
                pool_consensus_heihgts_[i] = height;
            }
        }
    }

    std::string init_consensus_height;
    for (uint32_t i = 0; i <= max_pool_index; ++i) {
        init_consensus_height += std::to_string(pool_consensus_heihgts_[i]) + " ";
    }

    ZJC_DEBUG("to txs get consensus heights: %s", init_consensus_height.c_str());
}

void ToTxsPools::HandleElectJoinVerifyVec(
        const std::string& g2_value,
        std::vector<bls::protobuf::JoinElectInfo>& verify_reqs) {
    bls::protobuf::JoinElectInfo join_info;
    if (!join_info.ParseFromString(g2_value)) {
        assert(false);
        return;
    }

    if (join_info.shard_id() != network::kRootCongressNetworkId) {
        return;
    }

    verify_reqs.push_back(join_info);
}

int ToTxsPools::LeaderCreateToHeights(pools::protobuf::ShardToTxItem& to_heights) {
#ifdef TEST_NO_CROSS
    return kPoolsError;
#endif
    bool valid = false;
    auto timeout = common::TimeUtils::TimestampMs();
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        uint64_t cons_height = pool_consensus_heihgts_[i];
        while (cons_height > 0) {
            auto exist_iter = added_heights_[i].find(cons_height);
            if (exist_iter != added_heights_[i].end()) {
                if (exist_iter->second + 5000lu > timeout) {
                    --cons_height;
                    continue;
                }
            }

            if (valided_heights_[i].find(cons_height) == valided_heights_[i].end()) {
                ZJC_DEBUG("leader get to heights error, pool: %u, height: %lu", i, cons_height);
                return kPoolsError;
            }

            valid = true;
            break;
        }

        to_heights.add_heights(cons_height);
    }

    if (!valid) {
        ZJC_DEBUG("final leader get to heights error, pool: %u, height: %lu", 0, 0);
        return kPoolsError;
    }

    return kPoolsSuccess;
}

void ToTxsPools::HandleCrossShard(
        bool is_root,
        const view_block::protobuf::ViewBlockItem& view_block,
        const block::protobuf::BlockTx& tx,
        std::unordered_map<uint32_t, std::unordered_set<CrossItem, CrossItemRecordHash>>& cross_map) {
    if (tx.status() != consensus::kConsensusSuccess) {
        ZJC_DEBUG("success handle block pool: %u, height: %lu, tm height: %lu, status: %d, step: %d",
            view_block.qc().pool_index(), view_block.block_info().height(), view_block.block_info().timeblock_height(), tx.status(), tx.step());
        return;
    }

    CrossStatisticItem cross_item;
    switch (tx.step()) {
    case pools::protobuf::kNormalTo: {
        if (!is_root) {
            for (int32_t i = 0; i < tx.storages_size(); ++i) {
                if (tx.storages(i).key() == protos::kNormalToShards) {
                    pools::protobuf::ToTxMessage to_tx;
                    if (!to_tx.ParseFromString(tx.storages(i).value())) {
                        return;
                    }

                    cross_item = CrossStatisticItem(to_tx.to_heights().sharding_id());
                    ZJC_DEBUG("step: %d, success add cross shard pool: %u, height: %lu, des: %u",
                        tx.step(), view_block.qc().pool_index(), view_block.block_info().height(), to_tx.to_heights().sharding_id());
                    break;
                }
            }
        }

        break;
    }
    case pools::protobuf::kRootCross: {
        if (is_root) {
            for (int32_t i = 0; i < tx.storages_size(); ++i) {
                if (tx.storages(i).key() == protos::kRootCross) {
                    cross_item = CrossStatisticItem(0);
                    cross_item.cross_ptr = std::make_shared<pools::protobuf::CrossShardStatistic>();
                    pools::protobuf::CrossShardStatistic& cross = *cross_item.cross_ptr;
                    if (!cross.ParseFromString(tx.storages(i).value())) {
                        assert(false);
                        break;
                    }
                }

                break;
            }
            ZJC_DEBUG("step: %d, success add cross shard pool: %u, height: %lu, des: %u",
                tx.step(), view_block.qc().pool_index(), view_block.block_info().height(), 0);
        }
        break;
    }
    case pools::protobuf::kJoinElect: {
        if (!is_root) {
            cross_item = CrossStatisticItem(network::kRootCongressNetworkId);
            ZJC_DEBUG("step: %d, success add cross shard pool: %u, height: %lu, des: %u",
                tx.step(), view_block.qc().pool_index(), view_block.block_info().height(), network::kRootCongressNetworkId);
        }
        
        break;
    }
    case pools::protobuf::kCreateLibrary: {
        if (is_root) {
            cross_item = CrossStatisticItem(network::kNodeNetworkId);
        } else {
            cross_item = CrossStatisticItem(network::kRootCongressNetworkId);
        }

        ZJC_DEBUG("step: %d, success add cross shard pool: %u, height: %lu, des: %u",
            tx.step(), view_block.qc().pool_index(), view_block.block_info().height(),
            cross_item.des_net);
        break;
    }
    case pools::protobuf::kRootCreateAddress:
    case pools::protobuf::kConsensusRootElectShard: {
        if (!is_root) {
            return;
        }

        cross_item = CrossStatisticItem(network::kNodeNetworkId);
        ZJC_DEBUG("step: %d, success add cross shard pool: %u, height: %lu, des: %u",
            tx.step(), view_block.qc().pool_index(), view_block.block_info().height(), network::kNodeNetworkId);
        break;
    }
    default:
        break;
    }

    uint32_t src_shard = common::GlobalInfo::Instance()->network_id();
    if (common::GlobalInfo::Instance()->network_id() >=
            network::kConsensusShardEndNetworkId) {
        src_shard -= network::kConsensusWaitingShardOffset;
    }

    if (cross_item.des_net != 0) {
        CrossItem tmp_cross_item{src_shard, view_block.qc().pool_index(), view_block.block_info().height()};
        cross_map[cross_item.des_net].insert(tmp_cross_item);
        ZJC_DEBUG("succcess add cross statistic shard: %u, pool: %u, height: %lu, des: %u",
            src_shard, view_block.qc().pool_index(), view_block.block_info().height(), cross_item.des_net);
    } else if (cross_item.cross_ptr != nullptr) {
        for (int32_t i = 0; i < cross_item.cross_ptr->crosses_size(); ++i) {
            CrossItem tmp_cross_item{
                cross_item.cross_ptr->crosses(i).src_shard(), 
                cross_item.cross_ptr->crosses(i).src_pool(), 
                cross_item.cross_ptr->crosses(i).height()};
            cross_map[cross_item.cross_ptr->crosses(i).des_shard()].insert(tmp_cross_item);
            ZJC_DEBUG("succcess add cross statistic shard: %u, pool: %u, height: %lu, des: %u",
                cross_item.cross_ptr->crosses(i).src_shard(),
                cross_item.cross_ptr->crosses(i).src_pool(),
                cross_item.cross_ptr->crosses(i).height(),
                cross_item.cross_ptr->crosses(i).des_shard());
        }
    }
}

int ToTxsPools::CreateToTxWithHeights(
        uint32_t sharding_id,
        uint64_t elect_height,
        const pools::protobuf::ShardToTxItem& leader_to_heights,
        pools::protobuf::ToTxMessage& to_tx) {
#ifdef TEST_NO_CROSS
    return kPoolsError;
#endif
    if (leader_to_heights.heights_size() != common::kInvalidPoolIndex) {
        assert(false);
        return kPoolsError;
    }

    std::map<std::string, ToAddressItemInfo> acc_amount_map;
    // std::unordered_set<CrossItem, CrossItemRecordHash> cross_set;
    for (int32_t pool_idx = 0; pool_idx < leader_to_heights.heights_size(); ++pool_idx) {
        uint64_t min_height = 1llu;
        std::shared_ptr<pools::protobuf::ShardToTxItem> prev_to_heights = nullptr;
        {
            common::AutoSpinLock lock(prev_to_heights_mutex_);
            prev_to_heights = prev_to_heights_;
        }

        if (prev_to_heights != nullptr) {
            min_height = prev_to_heights->heights(pool_idx) + 1;
        }

        uint64_t max_height = leader_to_heights.heights(pool_idx);
        if (max_height > pool_consensus_heihgts_[pool_idx]) {
            ZJC_DEBUG("pool %u, invalid height: %lu, consensus height: %lu",
                pool_idx,
                max_height,
                pool_consensus_heihgts_[pool_idx]);
            return kPoolsError;
        }

        common::AutoSpinLock auto_lock(network_txs_pools_mutex_);
        auto& height_map = network_txs_pools_[pool_idx];
        for (auto height = min_height; height <= max_height; ++height) {
            auto hiter = height_map.find(height);
            if (hiter == height_map.end()) {
//                 ZJC_DEBUG("find pool index: %u height: %lu failed!", pool_idx, height);
                continue;
            }

            for (auto to_iter = hiter->second.begin();
                    to_iter != hiter->second.end(); ++to_iter) {
                auto des_sharding_id = to_iter->second.sharding_id;
#ifndef NDEBUG
                uint32_t* tmp_data = (uint32_t*)to_iter->first.c_str();
                uint32_t step = tmp_data[0];
                std::string to(to_iter->first.c_str() + 4, to_iter->first.size() - 4);
                ZJC_DEBUG("statistic shard: %u, new tx coming sharding id: %u, to: %s, step: %u, pool: %u, min height: %lu, max height: %lu",
                    sharding_id, des_sharding_id, common::Encode::HexEncode(to).c_str(), step, pool_idx, min_height, max_height);
#endif
                if (to_iter->second.sharding_id == common::kInvalidUint32) {
                    uint32_t* tmp_data = (uint32_t*)to_iter->first.c_str();
                    uint32_t step = tmp_data[0];
                    std::string to(to_iter->first.c_str() + 4, to_iter->first.size() - 4);
                    protos::AddressInfoPtr account_info = acc_mgr_->GetAccountInfo(to);
                    if (account_info == nullptr) {
                        if (sharding_id != network::kRootCongressNetworkId) {
                            continue;
                        }
                        // 找不到账户，则将聚合 Tos 交易发送给 root
                        des_sharding_id = network::kRootCongressNetworkId;
                    } else {
                        to_iter->second.sharding_id = account_info->sharding_id();
                        des_sharding_id = to_iter->second.sharding_id;
                    }
                }

                if (des_sharding_id != sharding_id) {
                    ZJC_DEBUG("find pool index: %u height: %lu sharding: %u, %u failed id: %s, amount: %lu",
                        pool_idx, height, des_sharding_id,
                        sharding_id, common::Encode::HexEncode(to_iter->first).c_str(),
                        to_iter->second.amount);
                    continue;
                }
                
                auto amount_iter = acc_amount_map.find(to_iter->first);
                if (amount_iter == acc_amount_map.end()) {
                    ZJC_DEBUG("len: %u, addr: %s",
                        to_iter->first.size(), common::Encode::HexEncode(to_iter->first).c_str());
                    acc_amount_map[to_iter->first] = to_iter->second;
                    if (!to_iter->second.elect_join_g2_value.empty()) {
                        HandleElectJoinVerifyVec(
                            to_iter->second.elect_join_g2_value,
                            acc_amount_map[to_iter->first].verify_reqs);
                    }

                    ZJC_DEBUG("to block pool: %u, height: %lu, success add account "
                        "transfer amount height: %lu, id: %s, amount: %lu",
                        pool_idx, height,
                        height, common::Encode::HexEncode(to_iter->first).c_str(),
                        to_iter->second.amount);
                } else {
                    amount_iter->second.amount += to_iter->second.amount;
                    if (!to_iter->second.elect_join_g2_value.empty()) {
                        HandleElectJoinVerifyVec(
                            to_iter->second.elect_join_g2_value,
                            amount_iter->second.verify_reqs);
                    }

                    ZJC_DEBUG("to block pool: %u, height: %lu, success add account "
                        "transfer amount height: %lu, id: %s, amount: %lu, all: %lu",
                        pool_idx, height,
                        height, common::Encode::HexEncode(to_iter->first).c_str(),
                        to_iter->second.amount,
                        amount_iter->second.amount);
                }
            }
        }
    }

    // if (acc_amount_map.empty() && cross_set.empty()) {
    if (acc_amount_map.empty()) {
//         assert(false);
        ZJC_DEBUG("acc amount map empty.");
        return kPoolsError;
    }

    ZJC_DEBUG("not acc amount map empty.");
    // for (auto iter = cross_set.begin(); iter != cross_set.end(); ++iter) {
    //     auto cross_item = to_tx.add_crosses();
    //     cross_item->set_src_shard((*iter).src_shard);
    //     cross_item->set_src_pool((*iter).src_pool);
    //     cross_item->set_height((*iter).height);
    //     cross_item->set_des_shard(sharding_id);
    // }

    for (auto iter = acc_amount_map.begin(); iter != acc_amount_map.end(); ++iter) {
        uint32_t* tmp_data = (uint32_t*)iter->first.c_str();
        uint32_t step = tmp_data[0];
        std::string to(iter->first.c_str() + 4, iter->first.size() - 4);
        auto to_item = to_tx.add_tos();
        to_item->set_des(to); // 20 bytes，对于 prepayment tx 是 to + from（40 bytes）
        to_item->set_amount(iter->second.amount);
        to_item->set_pool_index(iter->second.pool_index);
        to_item->set_step(iter->second.type);
        // create contract just in caller sharding
        if (iter->second.type == pools::protobuf::kContractCreate) {
            assert(common::GlobalInfo::Instance()->network_id() > network::kRootCongressNetworkId);
            protos::AddressInfoPtr account_info = acc_mgr_->GetAccountInfo(to);
            if (account_info == nullptr) {
                to_tx.mutable_tos()->ReleaseLast();
                continue;
            }

            to_item->set_library_bytes(account_info->bytes_code());
            auto net_id = common::GlobalInfo::Instance()->network_id();
            to_item->set_sharding_id(net_id);
            ZJC_DEBUG("create contract use caller sharding address: %s, %u, step: %d",
                common::Encode::HexEncode(to).c_str(),
                common::GlobalInfo::Instance()->network_id(),
                iter->second.type);        
        } else if (iter->second.type == pools::protobuf::kCreateLibrary) {
            assert(common::GlobalInfo::Instance()->network_id() > network::kRootCongressNetworkId);
            to_item->set_library_bytes(iter->second.library_bytes);
            auto net_id = common::GlobalInfo::Instance()->network_id();
            to_item->set_sharding_id(net_id);
            ZJC_DEBUG("create library use caller sharding address: %s, %u, step: %d",
                common::Encode::HexEncode(to).c_str(),
                common::GlobalInfo::Instance()->network_id(),
                iter->second.type);        
        } else if (iter->second.type == pools::protobuf::kContractCreateByRootFrom) {
            assert(common::GlobalInfo::Instance()->network_id() > network::kRootCongressNetworkId);
            ZJC_DEBUG("library bytes: %s, to: %s, from: %s",
                common::Encode::HexEncode(iter->second.library_bytes).c_str(),
                common::Encode::HexEncode(to).c_str(),
                common::Encode::HexEncode(iter->second.from).c_str());
            if (memcmp(iter->second.library_bytes.c_str(),
                    protos::kContractBytesStartCode.c_str(),
                    protos::kContractBytesStartCode.size()) == 0) {
                to_item->set_library_bytes(iter->second.library_bytes);
                // ContractCreate 需要 from 地址，用于 prepayment 创建
                to_item->set_contract_from(iter->second.from);
                to_item->set_prepayment(iter->second.prepayment);
            }
            auto net_id = common::kInvalidUint32; // ContractCreate 不在直接分配 sharding，由 root 分配
            to_item->set_sharding_id(net_id);
            ZJC_DEBUG("create contract use caller sharding address: %s, %u",
                common::Encode::HexEncode(to).c_str(),
                common::GlobalInfo::Instance()->network_id());
		} else if (iter->second.type == pools::protobuf::kRootCreateAddress) {
            assert(sharding_id != network::kRootCongressNetworkId);
            ZJC_DEBUG(
                "==== 0.2 library bytes: %s, to: %s, from: %s",
                common::Encode::HexEncode(iter->second.library_bytes).c_str(),
                common::Encode::HexEncode(to).c_str(),
                common::Encode::HexEncode(iter->second.from).c_str());
            // for contract create tx
			if (memcmp(iter->second.library_bytes.c_str(),
                    protos::kContractBytesStartCode.c_str(),
                    protos::kContractBytesStartCode.size()) == 0) {
                to_item->set_library_bytes(iter->second.library_bytes);
                to_item->set_contract_from(iter->second.from);
                to_item->set_prepayment(iter->second.prepayment);
            }
            to_item->set_sharding_id(sharding_id);
            ZJC_DEBUG("root create sharding address: %s, %u, pool: %u",
                common::Encode::HexEncode(to).c_str(),
                sharding_id,
                iter->second.pool_index);
        } else if (iter->second.type == pools::protobuf::kJoinElect) {
            to_item->set_sharding_id(sharding_id);
            for (uint32_t i = 0; i < iter->second.verify_reqs.size(); ++i) {
                auto* req = to_item->add_join_infos();
                *req = iter->second.verify_reqs[i];
            }

            ZJC_DEBUG("send join elect to other shard des: %u, iter->second.verify_reqs.size: %u",
                sharding_id, iter->second.verify_reqs.size());
        } else {
            auto net_id = common::kInvalidUint32;
            to_item->set_sharding_id(iter->second.sharding_id);
        }

        ZJC_DEBUG("set to %s amount %lu, sharding id: %u, des sharding id: %d, pool index: %d",
            common::Encode::HexEncode(to).c_str(),
            iter->second.amount, to_item->sharding_id(), sharding_id, iter->second.pool_index);
    }

    to_tx.set_elect_height(elect_height);
    *to_tx.mutable_to_heights() = leader_to_heights;
    to_tx.mutable_to_heights()->set_sharding_id(sharding_id);
    return kPoolsSuccess;
}

};  // namespace pools

};  // namespace shardora
