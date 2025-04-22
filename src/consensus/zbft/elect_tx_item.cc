#include "consensus/zbft/elect_tx_item.h"

#include "common/fts_tree.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "elect_tx_item.h"
#include "protos/get_proto_hash.h"
#include <bls/bls_utils.h>
#include <google/protobuf/util/json_util.h>

namespace shardora {

namespace consensus {

inline bool ElectNodeBalanceCompare(const NodeDetailPtr& left, const NodeDetailPtr& right) {
    return left->stoke < right->stoke;
}

inline bool ElectNodeBalanceDiffCompare(
        const NodeDetailPtr& left,
        const NodeDetailPtr& right) {
    return left->stoke_diff < right->stoke_diff;
}

int ElectTxItem::TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) {
    ZJC_DEBUG("pools statistic tag tx consensus coming: %s, nonce: %lu, val: %s", 
        common::Encode::HexEncode(tx_info.to()).c_str(), 
        tx_info.nonce(),
        common::Encode::HexEncode(tx_info.value()).c_str());
    if (!DefaultTxItem(tx_info, block_tx)) {
        return consensus::kConsensusError;
    }

    // change
    if (tx_info.key().empty() || tx_info.value().empty()) {
        return consensus::kConsensusError;
    }

    

    return consensus::kConsensusSuccess;
}

int ElectTxItem::HandleTx(
        view_block::protobuf::ViewBlockItem& view_block,
        zjcvm::ZjchainHost& zjc_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    view_block_chain_ = zjc_host.view_block_chain_;
    g2_ = std::make_shared<std::mt19937_64>(vss_mgr_->EpochRandom());
    InitHost(zjc_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
    auto& unique_hash = tx_info->key();
    if (!elect_statistic_.ParseFromString(tx_info->value())) {
        ZJC_DEBUG("elect tx parse elect info failed!");
        return consensus::kConsensusError;
    }

    ZJC_DEBUG("get sharding statistic info sharding: %u, statistic_height: %lu, new node size: %u, %s, unique_hash: %s",
        elect_statistic_.sharding_id(), 
        elect_statistic_.statistic_height(), 
        elect_statistic_.join_elect_nodes_size(),
        ProtobufToJson(elect_statistic_).c_str(),
        common::Encode::HexEncode(unique_hash).c_str());
    uint64_t to_balance = 0;
    uint64_t to_nonce = 0;
    GetTempAccountBalance(zjc_host, block_tx.to(), acc_balance_map, &to_balance, &to_nonce);
    auto str_key = block_tx.to() + unique_hash;
    std::string val;
    if (zjc_host.GetKeyValue(block_tx.to(), unique_hash, &val) == zjcvm::kZjcvmSuccess) {
        ZJC_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
        return consensus::kConsensusError;
    }

    block_tx.set_unique_hash(unique_hash);
    auto res = processElect(zjc_host, view_block, block_tx);
    if (res != consensus::kConsensusSuccess) {
        return kConsensusError;
    }

    zjc_host.SaveKeyValue(block_tx.to(), unique_hash, "1");
    block_tx.set_unique_hash(unique_hash);
    block_tx.set_nonce(to_nonce + 1);
    ZJC_WARN("success call elect block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
        view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
    acc_balance_map[block_tx.to()]->set_balance(to_balance);
    acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
    // prefix_db_->AddAddressInfo(block_tx.to(), *(acc_balance_map[block_tx.to()]), zjc_host.db_batch_);
    ZJC_DEBUG("success add addr: %s, value: %s", 
        common::Encode::HexEncode(block_tx.to()).c_str(), 
        ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str());
    zjc_host.elect_tx_ = &block_tx;
    *view_block.mutable_elect_statistic() = elect_statistic_;
    *view_block.mutable_elect_block() = elect_block_;
    return consensus::kConsensusSuccess;
}

int ElectTxItem::processElect(
        zjcvm::ZjchainHost& zjc_host,
        view_block::protobuf::ViewBlockItem& view_block,
        shardora::block::protobuf::BlockTx &block_tx) {
    auto& block = *view_block.mutable_block_info();
    const pools::protobuf::PoolStatisticItem *statistic = nullptr;
    shardora::common::MembersPtr members = nullptr;
    int retVal = getMaxElectHeightInfo(statistic, members);
    if ( retVal != kConsensusSuccess) {
        ZJC_DEBUG("getMaxElectHeightInfo failed ret val: %d", retVal);
        assert(false);
        return retVal;
    }

    elect_members_ = members;
    for (auto iter = members->begin(); iter != members->end(); ++iter) {
        added_nodes_.insert((*iter)->pubkey);
        ZJC_DEBUG("success add now elect member: %s, %s",
            common::Encode::HexEncode(sec_ptr_->GetAddress((*iter)->pubkey)).c_str(),
            common::Encode::HexEncode((*iter)->pubkey).c_str());
    }

    uint32_t min_area_weight = common::kInvalidUint32;
    uint32_t min_tx_count = common::kInvalidUint32;
    std::vector<NodeDetailPtr> elect_nodes(members->size(), nullptr);
    // TODO: add weedout
    int res = CheckWeedout(members, *statistic, &min_area_weight, &min_tx_count, elect_nodes);
    if (res != kConsensusSuccess) {
        assert(false);
        return res;
    }

    uint64_t gas_for_root = 0llu;
    MiningToken(
        elect_statistic_.sharding_id(),
        elect_nodes,
        elect_statistic_.gas_amount(),
        &gas_for_root);
    min_area_weight += 1;
    min_tx_count += 1;
    std::vector<NodeDetailPtr> src_elect_nodes_to_choose;
    for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
        if (elect_nodes[i] != nullptr) {
            src_elect_nodes_to_choose.push_back(elect_nodes[i]);
        }
    }

    JoinNewNodes2ElectNodes(members, elect_nodes, min_area_weight, min_tx_count);
    std::string random_str;
    int32_t expect_leader_count = (int32_t)pow(
        2.0,
        (double)((int32_t)log2(double(elect_nodes.size() / 3))));
    if (expect_leader_count > (int32_t)common::kImmutablePoolSize) {
        expect_leader_count = (int32_t)common::kImmutablePoolSize;
    }

    assert(expect_leader_count > 0);
    std::set<uint32_t> leader_nodes;
    {
        std::string ids;
        for (uint32_t i = 0; i < src_elect_nodes_to_choose.size(); ++i) {
            ids += common::Encode::HexEncode(src_elect_nodes_to_choose[i]->pubkey) + ":" + std::to_string(src_elect_nodes_to_choose[i]->index) + ",";
        }

        ZJC_DEBUG("befor get leader: %s", ids.c_str());
    }

    FtsGetNodes(src_elect_nodes_to_choose, false, expect_leader_count, leader_nodes);
    ZJC_DEBUG("net: %u, elect use height to random order: %lu, leader size: %d, nodes count: %u, leader size: %d, random_str: %s, leader index: %d",
        elect_statistic_.sharding_id(), vss_mgr_->EpochRandom(), expect_leader_count, elect_nodes.size(), leader_nodes.size(), random_str.c_str(), *leader_nodes.begin());
    if (leader_nodes.size() != (uint32_t)expect_leader_count) {
        ZJC_ERROR("choose leader failed: %u", elect_statistic_.sharding_id());
        return kConsensusError;
    }

    int32_t mode_idx = 0;
    for (auto iter = src_elect_nodes_to_choose.begin(); iter != src_elect_nodes_to_choose.end(); ++iter) {
        if (leader_nodes.find((*iter)->index) != leader_nodes.end()) {
            (*iter)->leader_mod_index = mode_idx++;
        }
    }

    {
        std::string ids;
        int count = 0;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            if (elect_nodes[i] == nullptr) {
                continue;
            }
            count++;
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ",";
        }

        ZJC_DEBUG("LLLLLL before CreateNewElect: count %d, %s", count, ids.c_str());
    }

    CreateNewElect(
        zjc_host,
        block,
        elect_nodes,
        gas_for_root,
        block_tx);

    {
        std::string ids;
        int count = 0;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            if (elect_nodes[i] == nullptr) {
                continue;
            }
            count++;
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ",";
        }

        ZJC_DEBUG("LLLLL after CreateNewElect: count: %d ,%s", count, ids.c_str());
    }
    ZJC_DEBUG("consensus elect tx success: %u, proto: %s",
        elect_statistic_.sharding_id(), 
        ProtobufToJson(elect_statistic_).c_str());
    return kConsensusSuccess;
}

int ElectTxItem::getMaxElectHeightInfo(
        const shardora::pools::protobuf::PoolStatisticItem *&statistic, 
        shardora::common::MembersPtr &members) {
    uint64_t max_elect_height = 0;
    auto &max_stat = *std::max_element(
        elect_statistic_.statistics().begin(), 
        elect_statistic_.statistics().end(),
        [](const auto &a, const auto &b) { return a.elect_height() < b.elect_height(); });
    statistic = &max_stat;
    max_elect_height = max_stat.elect_height();
    uint64_t now_elect_height = elect_mgr_->latest_height(elect_statistic_.sharding_id());
    // 根据最新的选举块高度获取相关的 members
    members = elect_mgr_->GetNetworkMembersWithHeight(
        now_elect_height,
        elect_statistic_.sharding_id(),
        nullptr,
        nullptr);
    if (members == nullptr) {
        ZJC_WARN("get members failed, elect height: %lu, net: %u",
            now_elect_height, elect_statistic_.sharding_id());
        // assert(false);
        return kConsensusError;
    }

    if (max_elect_height < now_elect_height) {
        auto older_members = elect_mgr_->GetNetworkMembersWithHeight(
            max_elect_height,
            elect_statistic_.sharding_id(),
            nullptr,
            nullptr);
        if (older_members == nullptr) {
            ZJC_WARN("get members failed, elect height: %lu, net: %u",
                max_elect_height, elect_statistic_.sharding_id());
            // assert(false);
            return kConsensusError;
        }

        struct ElectItemInfo {
            uint32_t tx_count;
            uint64_t stoke;
            int32_t pos_x;
            int32_t pos_y;
            uint64_t gas_sum;
            uint64_t credit;
            uint64_t consensus_gap;
        };

        std::unordered_map<std::string, ElectItemInfo> elect_info_map;
        for (int32_t i = 0; i < statistic->tx_count_size(); ++i) {
            ElectItemInfo tmp_info;
            tmp_info.tx_count = statistic->tx_count(i);
            tmp_info.stoke = statistic->stokes(i);
            tmp_info.pos_x = statistic->area_point(i).x();
            tmp_info.pos_y = statistic->area_point(i).y();
            tmp_info.gas_sum = statistic->gas_sum(i);
            tmp_info.credit = statistic->credit(i);
            tmp_info.consensus_gap = statistic->consensus_gap(i);
            elect_info_map[(*older_members)[i]->id] = tmp_info;
        }

        auto new_statistic = elect_statistic_.add_statistics();
        for (uint32_t i = 0; i < members->size(); ++i) {
            auto iter = elect_info_map.find((*members)[i]->id);
            if (iter != elect_info_map.end()) {
                new_statistic->add_tx_count(iter->second.tx_count);
                new_statistic->add_stokes(iter->second.stoke);
                auto area_point = new_statistic->add_area_point();
                area_point->set_x(iter->second.pos_x);
                area_point->set_y(iter->second.pos_y);
                new_statistic->add_gas_sum(iter->second.gas_sum);
                new_statistic->add_credit(iter->second.credit);
                new_statistic->add_consensus_gap(iter->second.consensus_gap);
            } else {
                new_statistic->add_tx_count(0);
                new_statistic->add_stokes(0);
                auto area_point = new_statistic->add_area_point();
                area_point->set_x(0);
                area_point->set_y(0);
                new_statistic->add_gas_sum(0);
                new_statistic->add_credit(0);
                new_statistic->add_consensus_gap(0);
            }
        }

        statistic = new_statistic;
        max_elect_height = now_elect_height;
    }

    // TODO: check if elect height valid
    if (max_elect_height != now_elect_height) {
        ZJC_DEBUG("old elect coming max_elect_height: %lu, now_elect_height: %lu",
            max_elect_height, now_elect_height);
        return kConsensusError;
    }

    ZJC_DEBUG("success check old elect coming max_elect_height: %lu, now_elect_height: %lu",
        max_elect_height, now_elect_height);
    int32_t member_count = members->size();
    if (member_count != statistic->tx_count_size() ||
            member_count != statistic->stokes_size() ||
            member_count != statistic->area_point_size()) {
        ZJC_DEBUG("now_elect_height: %lu, member size error: %u, %u, %u, %u",
            now_elect_height, members->size(), statistic->tx_count_size(),
            statistic->stokes_size(), statistic->area_point_size());
        assert(false);
        return kConsensusError;
    }
    return kConsensusSuccess;
}

void ElectTxItem::JoinNewNodes2ElectNodes(
        shardora::common::MembersPtr &members,
        std::vector<shardora::consensus::NodeDetailPtr> &elect_nodes,
        uint32_t min_area_weight,
        uint32_t min_tx_count) {
    // 计算新加入节点数量
    // 如果未到达最大节点数量，新加入的节点为当前节点数量 * 10%
    uint32_t join_count = 0;
    if (members->size() < common::kEachShardMaxNodeCount) {
        if (members->size() < kFtsMinDoubleNodeCount) {
            join_count += members->size();
        } else {
            join_count += members->size() * kFtsNewElectJoinRate / 100;
        }

        if (join_count <= 0) {
            ++join_count;
        }
    }

    if (join_count + elect_nodes.size() > common::kEachShardMaxNodeCount) {
        join_count = common::kEachShardMaxNodeCount - elect_nodes.size();
    }

    ZJC_DEBUG("add new node count: %u", join_count);
    for (uint32_t i = 0; i < join_count; ++i) {
        elect_nodes.push_back(nullptr);
    }
    // 先填满有固定位置的新节点，再填满随机节点
    ChooseNodeForEachIndex(
        true,
        min_area_weight,
        min_tx_count,
        elect_nodes);
    ChooseNodeForEachIndex(
        false,
        min_area_weight,
        min_tx_count,
        elect_nodes);

    {
        std::string ids;
        int count = 0;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            if (elect_nodes[i] == nullptr) {
                continue;
            }
            count++;
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ",";
        }

        ZJC_DEBUG("LLLLLL after join elect: count:%d, %s", count, ids.c_str());
    }
}

void ElectTxItem::ChooseNodeForEachIndex(
        bool hold_pos,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        std::vector<NodeDetailPtr> &elect_nodes) {
    for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
        if (elect_nodes[i] != nullptr) {
            continue;
        }

        std::vector<NodeDetailPtr> elect_nodes_to_choose;
        if (hold_pos) {
            GetIndexNodes(
                i,
                min_area_weight,
                min_tx_count,
                &elect_nodes_to_choose);
        } else {
            GetIndexNodes(
                common::kInvalidUint32,
                min_area_weight,
                min_tx_count,
                &elect_nodes_to_choose);
        }

        ZJC_DEBUG("elect add new node: %u, index: %d, hold pos: %d",
                  elect_nodes_to_choose.size(), i, hold_pos);
        if (elect_nodes_to_choose.empty()) {
            continue;
        }

        auto res = GetJoinElectNodesCredit(
            i,
            min_area_weight,
            min_tx_count,
            elect_nodes_to_choose,
            elect_nodes);
        if (res != kConsensusSuccess) {
            assert(false);
            return;
        }

        if (elect_nodes[i] != nullptr) {
            ZJC_DEBUG("LLLLL elect add new node: %s",
                  common::Encode::HexEncode(elect_nodes[i]->pubkey).c_str());
            added_nodes_.insert(elect_nodes[i]->pubkey);
        }
    }
}

void ElectTxItem::GetIndexNodes(
        uint32_t index,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        std::vector<NodeDetailPtr> *elect_nodes_to_choose) {
    for (int32_t i = 0; i < elect_statistic_.join_elect_nodes_size(); ++i) {
        ZJC_DEBUG("join new node: %s, des shard: %u, statistic shrad: %u",
                  common::Encode::HexEncode(elect_statistic_.join_elect_nodes(i).pubkey()).c_str(),
                  elect_statistic_.join_elect_nodes(i).shard(),
                  elect_statistic_.sharding_id());

        // 已经在委员会中跳过
        auto iter = added_nodes_.find(elect_statistic_.join_elect_nodes(i).pubkey());
        if (iter != added_nodes_.end()) {
            ZJC_DEBUG("join new node failed: %s, already in committee", common::Encode::HexEncode(elect_statistic_.join_elect_nodes(i).pubkey()).c_str());
            continue;
        }

        if (elect_statistic_.join_elect_nodes(i).shard() != elect_statistic_.sharding_id()) {
            ZJC_DEBUG("join new node failed: %s, not in this sharding", common::Encode::HexEncode(elect_statistic_.join_elect_nodes(i).pubkey()).c_str());
            continue;
        }

        if (index != common::kInvalidUint32) {
            // 当指定了index时，只选择指定index的节点
            // 不指定 index 时，选择所有节点
            if (elect_statistic_.join_elect_nodes(i).elect_pos() != (int32_t)index) {
                ZJC_DEBUG("join new node failed: %s, not in this index, new node index :%d, need index:%d",
                          common::Encode::HexEncode(elect_statistic_.join_elect_nodes(i).pubkey()).c_str(),
                          elect_statistic_.join_elect_nodes(i).elect_pos(),
                          index);
                continue;
            }
        }

        auto id = sec_ptr_->GetAddress(elect_statistic_.join_elect_nodes(i).pubkey());
        protos::AddressInfoPtr account_info = view_block_chain_->ChainGetAccountInfo(id);
        if (account_info == nullptr) {
            assert(false);
            return;
        }

        auto node_info = std::make_shared<ElectNodeInfo>();
        node_info->area_weight = min_area_weight;
        node_info->stoke = elect_statistic_.join_elect_nodes(i).stoke();
        node_info->tx_count = min_tx_count;
        node_info->credit = elect_statistic_.join_elect_nodes(i).credit();
        node_info->pubkey = elect_statistic_.join_elect_nodes(i).pubkey();
        // xufeisofly 新增节点的 bls_agg_pk
        auto agg_bls_pk_proto = elect_statistic_.join_elect_nodes(i).agg_bls_pk();
        node_info->agg_bls_pk = *bls::Proto2BlsPublicKey(agg_bls_pk_proto);
        auto proof_proto = elect_statistic_.join_elect_nodes(i).agg_bls_pk_proof();
        node_info->agg_bls_pk_proof = *bls::Proto2BlsPopProof(proof_proto);
        
        node_info->index = index;
        node_info->consensus_gap = elect_statistic_.join_elect_nodes(i).consensus_gap(); 
        elect_nodes_to_choose->push_back(node_info);
    }
}

void ElectTxItem::MiningToken(
        uint32_t statistic_sharding_id,
        std::vector<NodeDetailPtr> &elect_nodes,
        uint64_t all_gas_amount,
        uint64_t *gas_for_root) {
    uint64_t all_tx_count = 0;
    uint64_t max_tx_count = 0;
    std::vector<NodeDetailPtr> valid_nodes;
    for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
        if (elect_nodes[i] == nullptr) {
            continue;
        }

        auto tx_count = elect_nodes[i]->tx_count;
        if (tx_count == 0) {
            tx_count = 1;
        }

        if (tx_count > max_tx_count) {
            max_tx_count = tx_count;
        }

        all_tx_count += tx_count;
        valid_nodes.push_back(elect_nodes[i]);
    }

    if (max_tx_count <= 0) {
        return;
    }

    //    uint64_t gas_for_mining = all_gas_amount - (all_gas_amount / network_count_);
    uint64_t gas_for_mining = all_gas_amount;
    // root shard use statistic gas amount.
    if (statistic_sharding_id == network::kRootCongressNetworkId) {
        gas_for_mining = all_gas_amount;
    } else {
        *gas_for_root = all_gas_amount - gas_for_mining;
    }

    auto now_ming_count = GetMiningMaxCount(max_tx_count);
    uint64_t tmp_all_gas_amount = 0;
    if (!stop_mining_) {
        for (uint32_t i = 0; i < valid_nodes.size(); ++i) {
            auto id = sec_ptr_->GetAddress(valid_nodes[i]->pubkey);
            protos::AddressInfoPtr account_info = view_block_chain_->ChainGetAccountInfo(id);
            if (account_info == nullptr) {
                ZJC_DEBUG("get account info failed: %s",
                          common::Encode::HexEncode(id).c_str());
                assert(false);
                continue;
            }

            auto tx_count = valid_nodes[i]->tx_count;
            if (tx_count == 0) {
                tx_count = 1;
            }

            auto mining_token = now_ming_count * tx_count / max_tx_count;
            valid_nodes[i]->mining_token = mining_token;
            //            auto gas_token = tx_count * gas_for_mining / all_tx_count;
            //          只有 leader 节点才会获得所有的 gas
            auto gas_token = valid_nodes[i]->gas_sum;
            if (i + 1 == valid_nodes.size()) {
                assert(gas_for_mining >= tmp_all_gas_amount);
                gas_token = gas_for_mining - tmp_all_gas_amount;
            }

            valid_nodes[i]->mining_token += gas_token;
            tmp_all_gas_amount += gas_token;
            ZJC_DEBUG("elect mining %s, mining: %lu, gas mining: %lu, all gas: %lu, src: %lu",
                      common::Encode::HexEncode(id).c_str(),
                      mining_token, gas_token, tmp_all_gas_amount, gas_for_mining);
        }
    }

    assert(tmp_all_gas_amount == gas_for_mining);
}

void ElectTxItem::SetPrevElectInfo(
        const elect::protobuf::ElectBlock &elect_block,
        block::protobuf::Block &block_item) {
    view_block::protobuf::ViewBlockItem view_block_item;
    auto res = prefix_db_->GetBlockWithHeight(
        network::kRootCongressNetworkId,
        elect_block.shard_network_id() % common::kImmutablePoolSize,
        elect_block.prev_members().prev_elect_height(),
        &view_block_item);
    if (!res) {
        ELECT_ERROR("get prev block error[%d][%d][%lu].",
                    network::kRootCongressNetworkId,
                    common::kImmutablePoolSize,
                    elect_block.prev_members().prev_elect_height());
        return;
    }

    auto& prev_block_item = view_block_item.block_info();
    if (!prev_block_item.has_elect_block()) {
        ELECT_ERROR("not has tx list size.");
        assert(false);
        return;
    }

    *block_item.mutable_prev_elect_block() = prev_block_item.elect_block();
}

uint64_t ElectTxItem::GetMiningMaxCount(uint64_t max_tx_count) {
    if (common::GlobalInfo::Instance()->network_id() == network::kRootCongressNetworkId) {
        max_tx_count += 10000lu;
    }

    auto now_ming_count = static_cast<uint64_t>(
                              common::kMiningTokenMultiplicationFactor * log2((double)max_tx_count)) *
                          common::kZjcMiniTransportUnit;
    return now_ming_count;
}

int ElectTxItem::CreateNewElect(
        zjcvm::ZjchainHost& zjc_host,
        block::protobuf::Block &block,
        const std::vector<NodeDetailPtr> &elect_nodes,
        uint64_t gas_for_root,
        block::protobuf::BlockTx &block_tx) {
    auto& elect_block = elect_block_;
    for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
        if (elect_nodes[i] == nullptr) {
            if (i >= elect_members_->size()) {
                break;
            }

            auto in = elect_block.add_in();
            in->set_pubkey((*elect_members_)[i]->pubkey);
            // xufeisofly
            auto agg_bls_pk_proto = bls::BlsPublicKey2Proto((*elect_members_)[i]->agg_bls_pk);
            if (agg_bls_pk_proto) {
                in->mutable_agg_bls_pk()->CopyFrom(*agg_bls_pk_proto);
            }
            auto proof_proto = bls::BlsPopProof2Proto((*elect_members_)[i]->agg_bls_pk_proof);
            if (proof_proto) {
                in->mutable_agg_bls_pk_proof()->CopyFrom(*proof_proto);
            }
            
            in->set_pool_idx_mod_num(-1);
            in->set_mining_amount(0);
        } else {
            auto in = elect_block.add_in();
            in->set_pubkey(elect_nodes[i]->pubkey);
            auto agg_bls_pk_proto = bls::BlsPublicKey2Proto(elect_nodes[i]->agg_bls_pk);
            if (agg_bls_pk_proto) {
                in->mutable_agg_bls_pk()->CopyFrom(*agg_bls_pk_proto);
            }
            auto proof_proto = bls::BlsPopProof2Proto(elect_nodes[i]->agg_bls_pk_proof);
            if (proof_proto) {
                in->mutable_agg_bls_pk_proof()->CopyFrom(*proof_proto);
            }
            
            in->set_pool_idx_mod_num(elect_nodes[i]->leader_mod_index);
            in->set_mining_amount(elect_nodes[i]->mining_token);
            in->set_fts_value(elect_nodes[i]->fts_value);
        }
    }

    elect_block.set_gas_for_root(gas_for_root);
    elect_block.set_shard_network_id(elect_statistic_.sharding_id());
    elect_block.set_elect_height(block.height());
    elect_block.set_all_gas_amount(elect_statistic_.gas_amount());
    if (bls_mgr_->AddBlsConsensusInfo(elect_block) != bls::kBlsSuccess) {
        ZJC_WARN("add prev elect bls consensus info failed sharding id: %u",
                 elect_statistic_.sharding_id());
    } else {
        ZJC_DEBUG("success add bls consensus info: %u, %lu",
                  elect_statistic_.sharding_id(),
                  elect_block.prev_members().prev_elect_height());
        SetPrevElectInfo(elect_block, block);
        prefix_db_->SaveElectHeightCommonPk(
            elect_block.shard_network_id(),
            elect_block.prev_members().prev_elect_height(),
            elect_block.prev_members(),
            zjc_host.db_batch_);
    }
    
    return kConsensusSuccess;
}
/**
 * @param members
 * @param statistic_item 来自共识节点的统计信息
 * @param min_area_weight return 节点间的最小逻辑距离
 * @param min_tx_count 返回结果 最小交易量
 * @param elect_nodes 返回结果 剔除了少于 max_tx_count/2 的节点，原节点位置保持不变
 * @return
 */
int ElectTxItem::CheckWeedout(
        common::MembersPtr &members,
        const pools::protobuf::PoolStatisticItem &statistic_item,
        uint32_t *min_area_weight,
        uint32_t *min_tx_count,
        std::vector<NodeDetailPtr> &elect_nodes) {
    {
        std::string dugstr;
        for (auto &members : *members) {
            dugstr += common::Encode::HexEncode(members->pubkey) + " ";
        }
        ZJC_DEBUG("LLLLL before WeedOut count %d : %s", statistic_item.tx_count_size(), dugstr.c_str());
    }

    uint32_t weed_out_count = statistic_item.tx_count_size() * kFtsWeedoutDividRate / 100; // 旧委员会有 10% 会被淘汰
    uint32_t direct_weed_out_count = weed_out_count / 2;

    // 计算最大交易量 按交易量降序排列
    uint32_t max_tx_count = 0;
    typedef std::pair<uint32_t, uint32_t> TxItem;
    std::vector<TxItem> member_tx_count;
    for (int32_t member_idx = 0; member_idx < statistic_item.tx_count_size(); ++member_idx) {
        max_tx_count = std::max(max_tx_count, statistic_item.tx_count(member_idx));
        *min_tx_count = (std::min)(*min_tx_count, statistic_item.tx_count(member_idx));

        member_tx_count.push_back(std::make_pair(member_idx, statistic_item.tx_count(member_idx)));
    }
    std::stable_sort(member_tx_count.begin(), member_tx_count.end(),
                     [](const TxItem &l, const TxItem &r) { return l.second > r.second; });

    uint32_t direct_weedout_tx_count = max_tx_count / 2;
    std::set<uint32_t> invalid_nodes;

    // 第一次淘汰
    // 最多因为交易量少直接淘汰 direct_weedout_count 个节点
    for (uint32_t i = 0; i < direct_weed_out_count; ++i) {
        if (member_tx_count[i].second < direct_weedout_tx_count) {
            invalid_nodes.insert(member_tx_count[i].first);
            ZJC_DEBUG("direct weedout: %s, tx count: %u, max_tx_count: %u",
                      common::Encode::HexEncode(sec_ptr_->GetAddress((*members)[member_tx_count[i].first]->pubkey)).c_str(),
                      statistic_item.tx_count(member_tx_count[i].first), max_tx_count);
        }
    }

    std::vector<NodeDetailPtr> elect_nodes_to_choose;
    for (int32_t member_idx = 0; member_idx < statistic_item.tx_count_size(); ++member_idx) {
        if (invalid_nodes.find(member_idx) != invalid_nodes.end()) {
            continue;
        }

        // 计算当前于其他节点之间的最小距离
        uint32_t min_dis = common::kInvalidUint32;
        for (int32_t idx = 0; idx < statistic_item.tx_count_size(); ++idx) {
            if (member_idx == idx) {
                continue;
            }

            auto &point0 = statistic_item.area_point(member_idx);
            auto &point1 = statistic_item.area_point(idx);
            uint32_t dis = (point0.x() - point1.x()) * (point0.x() - point1.x()) +
                           (point0.y() - point1.y()) * (point0.y() - point1.y());
            if (min_dis > dis) {
                min_dis = dis;
            }
        }
        // 构建节点信息，并更新全局最小节点距离
        protos::AddressInfoPtr account_info = view_block_chain_->ChainGetAccountInfo((*members)[member_idx]->id);
        if (account_info == nullptr) {
            ZJC_ERROR("get account info failed: %s",
                      common::Encode::HexEncode((*members)[member_idx]->id).c_str());
            assert(false);
            return kConsensusError;
        }

        auto node_info = std::make_shared<ElectNodeInfo>();
        node_info->gas_sum = statistic_item.gas_sum(member_idx);
        node_info->area_weight = min_dis;
        node_info->tx_count = statistic_item.tx_count(member_idx);
        node_info->stoke = statistic_item.stokes(member_idx);
        node_info->credit = statistic_item.credit(member_idx);
        node_info->index = member_idx;
        // 此处增加上一轮已有节点的 bls_agg_pk
        node_info->pubkey = (*members)[member_idx]->pubkey;
        node_info->agg_bls_pk = (*members)[member_idx]->agg_bls_pk;
        node_info->agg_bls_pk_proof = (*members)[member_idx]->agg_bls_pk_proof;
        node_info->pubkey = (*members)[member_idx]->pubkey;
        node_info->consensus_gap = statistic_item.consensus_gap(member_idx); 

        if (*min_area_weight > min_dis) {
            *min_area_weight = min_dis;
        }

        elect_nodes_to_choose.push_back(node_info);
    }

    if (elect_nodes_to_choose.empty()) {
        ZJC_WARN("elect sharding nodes empty.");
        return kConsensusError;
    }

    std::set<uint32_t> weedout_nodes;

    // 第二次淘汰
    // TODO: add weedout nodes
    FtsGetNodes(elect_nodes_to_choose, true, weed_out_count - invalid_nodes.size(), weedout_nodes);
    for (auto iter = elect_nodes_to_choose.begin(); iter != elect_nodes_to_choose.end(); ++iter) {
        if (weedout_nodes.find((*iter)->index) != weedout_nodes.end()) {
            ZJC_DEBUG("fts weedout: %s, tx count: %u, max_tx_count: %u",
                      common::Encode::HexEncode(sec_ptr_->GetAddress((*members)[(*iter)->index]->pubkey)).c_str(),
                      statistic_item.tx_count((*iter)->index), max_tx_count);

            continue;
        }

        elect_nodes[(*iter)->index] = *iter;
    }

    {
        std::string debugStr;
        int cout = 0;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            if (elect_nodes[i] != nullptr) {
                cout++;
                debugStr += common::Encode::HexEncode(sec_ptr_->GetAddress((*members)[i]->pubkey)) + " ";
            } else {
                debugStr += "null ";
            }
        }
        ZJC_DEBUG("LLLLL after weedOut count:%d , %s", cout, debugStr.c_str());
    }

    return kConsensusSuccess;
}

int ElectTxItem::GetJoinElectNodesCredit(
        uint32_t index,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        std::vector<NodeDetailPtr> &elect_nodes_to_choose,
        std::vector<NodeDetailPtr> &elect_nodes) {
    if (elect_nodes_to_choose.empty()) {
        return kConsensusSuccess;
    }

    std::set<uint32_t> weedout_nodes;
    FtsGetNodes(elect_nodes_to_choose, false, 1, weedout_nodes);
    for (auto iter = elect_nodes_to_choose.begin(); iter != elect_nodes_to_choose.end(); ++iter) {
        if (weedout_nodes.find((*iter)->index) == weedout_nodes.end()) {
            continue;
        }

        elect_nodes[index] = *iter;
        ZJC_DEBUG("success add join elect node: %s",
                  common::Encode::HexEncode((*iter)->pubkey).c_str());
        assert(!(*iter)->pubkey.empty());
        break;
    }

    return kConsensusSuccess;
}

void ElectTxItem::FtsGetNodes(
        std::vector<NodeDetailPtr> &elect_nodes,
        bool weed_out,
        uint32_t count,
        std::set<uint32_t> &res_nodes) {
    uint64_t max_fts_val = 0;
    SmoothFtsValue(elect_nodes, &max_fts_val);
    {
        std::string ids;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            ids += common::Encode::HexEncode(sec_ptr_->GetAddress(elect_nodes[i]->pubkey)) + ":" +
                   std::to_string(elect_nodes[i]->fts_value) + ",";
        }

        ZJC_DEBUG("fts value: %s", ids.c_str());
    }
    uint32_t try_times = 0;
    std::set<int32_t> tmp_res_nodes;
    while (tmp_res_nodes.size() < count) {
        common::FtsTree fts_tree;
        int32_t idx = 0;
        for (auto iter = elect_nodes.begin(); iter != elect_nodes.end(); ++iter, ++idx) {
            if (tmp_res_nodes.find(idx) != tmp_res_nodes.end()) {
                continue;
            }

            auto fts_val = (*iter)->fts_value;
            if (weed_out) {
                fts_val = max_fts_val - fts_val;
            }

            fts_tree.AppendFtsNode((*iter)->fts_value, idx);
        }

        fts_tree.CreateFtsTree();
        auto &g2 = *g2_;
        int32_t data = fts_tree.GetOneNode(g2);
        if (data == -1) {
            ++try_times;
            if (try_times > 5) {
                ELECT_ERROR("fts get elect nodes failed! tmp_res_nodes size: %d", tmp_res_nodes.size());
                return;
            }
            continue;
        }

        try_times = 0;
        tmp_res_nodes.insert(data);
    }

    for (auto iter = tmp_res_nodes.begin(); iter != tmp_res_nodes.end(); ++iter) {
        res_nodes.insert(elect_nodes[*iter]->index);
    }
}

void ElectTxItem::SmoothFtsValue(
        std::vector<NodeDetailPtr> &elect_nodes,
        uint64_t *max_fts_val) {
    {
        std::string ids;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ":" + std::to_string(elect_nodes[i]->stoke) + ",";
        }

        ZJC_DEBUG("before sort: %s", ids.c_str());
    }
    std::stable_sort(elect_nodes.begin(), elect_nodes.end(), ElectNodeBalanceCompare);
    {
        std::string ids;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ":" + std::to_string(elect_nodes[i]->stoke) + ",";
        }

        ZJC_DEBUG("before sort 0: %s", ids.c_str());
    }
    elect_nodes[0]->stoke_diff = 0;
    for (uint32_t i = 1; i < elect_nodes.size(); ++i) {
        elect_nodes[i]->stoke_diff = elect_nodes[i]->stoke - elect_nodes[i - 1]->stoke;
    }
    {
        std::string ids;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ":" + std::to_string(elect_nodes[i]->stoke_diff) + ",";
        }

        ZJC_DEBUG("after sort: %s", ids.c_str());
    }
    std::stable_sort(elect_nodes.begin(), elect_nodes.end(), ElectNodeBalanceDiffCompare);
    {
        std::string ids;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ":" + std::to_string(elect_nodes[i]->stoke_diff) + ",";
        }

        ZJC_DEBUG("after sort 1: %s", ids.c_str());
    }
    uint64_t diff_2b3 = elect_nodes[elect_nodes.size() * 2 / 3]->stoke_diff;
    std::stable_sort(elect_nodes.begin(), elect_nodes.end(), ElectNodeBalanceCompare);
    {
        std::string ids;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ":" + std::to_string(elect_nodes[i]->stoke) + ",";
        }

        ZJC_DEBUG("after sort 2: %s", ids.c_str());
    }
    int32_t min_balance = (std::numeric_limits<int32_t>::max)();
    int32_t blance_diff = 0;
    std::vector<int32_t> blance_weight;
    {
        blance_weight.resize(elect_nodes.size(), 0);
        blance_weight[0] = 100;
        int32_t max_balance = 0;
        auto &g2 = *g2_;
        for (uint32_t i = 1; i < elect_nodes.size(); ++i) {
            uint64_t fts_val_diff = elect_nodes[i]->stoke - elect_nodes[i - 1]->stoke;
            if (fts_val_diff == 0) {
                blance_weight[i] = blance_weight[i - 1];
            } else {
                if (fts_val_diff < diff_2b3) {
                    auto rand_val = fts_val_diff + g2() % (diff_2b3 - fts_val_diff);
                    blance_weight[i] = blance_weight[i - 1] + (20 * rand_val) / diff_2b3;
                } else {
                    auto rand_val = diff_2b3 + g2() % (fts_val_diff + 1 - diff_2b3);
                    blance_weight[i] = blance_weight[i - 1] + (20 * rand_val) / fts_val_diff;
                }
            }

            if (min_balance > blance_weight[i]) {
                min_balance = blance_weight[i];
            }

            if (max_balance < blance_weight[i]) {
                max_balance = blance_weight[i];
            }
        }

        // at least [100, 1000] for fts
        blance_diff = max_balance - min_balance;
        if (max_balance - min_balance < 1000) {
            auto old_balance_diff = max_balance - min_balance;
            max_balance = min_balance + 1000;
            blance_diff = max_balance - min_balance;
            if (old_balance_diff > 0) {
                int32_t balance_index = blance_diff / old_balance_diff;
                for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                    blance_weight[i] = min_balance + balance_index * (blance_weight[i] - min_balance);
                }
            }
        }
    }

    std::vector<int32_t> credit_weight;
    {
        credit_weight.resize(elect_nodes.size(), 0);
        int32_t min_credit = (std::numeric_limits<int32_t>::max)();
        int32_t max_credit = (std::numeric_limits<int32_t>::min)();
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            credit_weight[i] = elect_nodes[i]->credit;
            if (min_credit > credit_weight[i]) {
                min_credit = credit_weight[i];
            }

            if (max_credit < credit_weight[i]) {
                max_credit = credit_weight[i];
            }
        }

        int32_t credit_diff = max_credit - min_credit;
        if (credit_diff > 0) {
            int32_t credit_index = blance_diff / credit_diff;
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                credit_weight[i] = min_balance + credit_index * (credit_weight[i] - min_credit);
            }
        }
    }

    std::vector<int32_t> ip_weight;
    {
        ip_weight.resize(elect_nodes.size(), 0);
        int32_t min_ip_weight = (std::numeric_limits<int32_t>::max)();
        int32_t max_ip_weight = (std::numeric_limits<int32_t>::min)();
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            int32_t prefix_len = 0;
            auto count = 0;
            ip_weight[i] = elect_nodes[i]->area_weight;
            if (ip_weight[i] > max_ip_weight) {
                max_ip_weight = ip_weight[i];
            }

            if (ip_weight[i] < min_ip_weight) {
                min_ip_weight = ip_weight[i];
            }
        }

        int32_t weight_diff = max_ip_weight - min_ip_weight;
        if (weight_diff > 0) {
            int32_t weight_index = blance_diff / weight_diff;
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                ip_weight[i] = min_balance + weight_index * (ip_weight[i] - min_ip_weight);
            }
        }
    }

      std::vector<int32_t> gap_weight;
    {
        gap_weight.resize(elect_nodes.size(), 0);
        int32_t min_gap_weight = (std::numeric_limits<int32_t>::max)();
        int32_t max_gap_weight = (std::numeric_limits<int32_t>::min)();
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            int32_t prefix_len = 0;
            auto count = 0;
            gap_weight[i] = elect_nodes[i]->consensus_gap;
            if (gap_weight[i] > max_gap_weight) {
                max_gap_weight = gap_weight[i];
            }

            if (gap_weight[i] < min_gap_weight) {
                min_gap_weight = gap_weight[i];
            }
        }

        int32_t weight_diff = max_gap_weight - min_gap_weight;
        if (weight_diff > 0) {
            int32_t weight_index = blance_diff / weight_diff;
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                gap_weight[i] = min_balance + weight_index * (gap_weight[i] - min_gap_weight);
            }
        }
    }

    std::vector<int32_t> epoch_weight;
    {
        epoch_weight.resize(elect_nodes.size(), 0);
        int32_t min_epoch_weight = (std::numeric_limits<int32_t>::max)();
        int32_t max_epoch_weight = (std::numeric_limits<int32_t>::min)();
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            int32_t prefix_len = 0;
            auto count = 0;
            epoch_weight[i] = elect_nodes[i]->tx_count;
            if (epoch_weight[i] > max_epoch_weight) {
                max_epoch_weight = epoch_weight[i];
            }

            if (epoch_weight[i] < min_epoch_weight) {
                min_epoch_weight = epoch_weight[i];
            }
        }

        int32_t weight_diff = max_epoch_weight - min_epoch_weight;
        if (weight_diff > 0) {
            int32_t weight_index = blance_diff / weight_diff;
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                epoch_weight[i] = min_balance + weight_index * (epoch_weight[i] - min_epoch_weight);
            }
        }
    }

    std::string fts_val_str;
    for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
        elect_nodes[i]->fts_value = (2 * ip_weight[i] +
                                     2 * credit_weight[i] +
                                     2 * blance_weight[i] +
                                     2 * epoch_weight[i]) +
                                     2 * gap_weight[i] /
                                    10;
        fts_val_str += std::to_string(ip_weight[i]) + "," +
                       std::to_string(credit_weight[i]) + "," +
                       std::to_string(blance_weight[i]) + "," +
                       std::to_string(epoch_weight[i]) + "," +
                       std::to_string(gap_weight[i]) + "," +
                       std::to_string(elect_nodes[i]->fts_value) + " --- ";
        if (*max_fts_val < elect_nodes[i]->fts_value) {
            *max_fts_val = elect_nodes[i]->fts_value;
        }
    }

    ZJC_DEBUG("fts value final: %s", fts_val_str.c_str());
}

}; // namespace consensus

}; // namespace shardora
