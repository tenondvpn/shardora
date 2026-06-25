#include "consensus/zbft/elect_tx_item.h"

#include "common/fts_tree.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "elect_tx_item.h"
#include "protos/get_proto_hash.h"
#include <bls/bls_utils.h>
#include <google/protobuf/util/json_util.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>
#include <algorithm>

namespace shardora {

namespace consensus {

// Define the area penalty coefficient default value (can be tuned)
const double ElectTxItem::kAreaPenaltyCoefficient = 1.0;

inline bool ElectNodePosCompare(const NodeDetailPtr& left, const NodeDetailPtr& right) {
    return left->stoke < right->stoke;
}

inline bool ElectNodePosDiffCompare(
        const NodeDetailPtr& left,
        const NodeDetailPtr& right) {
    return left->stoke_diff < right->stoke_diff;
}

int ElectTxItem::TxToBlockTx(
        const pools::protobuf::TxMessage& tx_info,
        block::protobuf::BlockTx* block_tx) {
    SHARDORA_DEBUG("pools statistic tag tx consensus coming: %s, nonce: %lu, val: %s", 
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
        uint32_t tx_index,
        view_block::protobuf::ViewBlockItem& view_block,
        shardoravm::ShardorahainHost& pre_shardora_host,
        hotstuff::BalanceAndNonceMap& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    view_block_chain_ = pre_shardora_host.view_block_chain_;
    g2_ = std::make_shared<std::mt19937_64>(vss_mgr_->EpochRandom());
    shardoravm::ShardorahainHost shardora_host;
    shardora_host.view_block_chain_ = pre_shardora_host.view_block_chain_;
    shardora_host.tx_context_ = pre_shardora_host.tx_context_;
    shardora_host.pre_shardora_host_ = &pre_shardora_host;
    InitHost(shardora_host, block_tx, block_tx.gas_limit(), block_tx.gas_price(), view_block);
    auto& unique_hash = tx_info->key();
    if (!elect_statistic_.ParseFromString(tx_info->value())) {
        SHARDORA_DEBUG("elect tx parse elect info failed!");
        return consensus::kConsensusError;
    }

    elect_block_ = elect_statistic_.mutable_elect_block();
    SHARDORA_DEBUG("get sharding statistic info sharding: %u, statistic_height: %lu, "
        "new node size: %u, %s, unique_hash: %s",
        elect_statistic_.sharding_id(), 
        elect_statistic_.statistic_height(), 
        elect_statistic_.join_elect_nodes_size(),
        ProtobufToJson(elect_statistic_).c_str(),
        common::Encode::HexEncode(unique_hash).c_str());
    uint64_t to_balance = 0;
    uint64_t to_nonce = 0;
    GetTempAccountBalance(shardora_host, block_tx.to(), acc_balance_map, &to_balance, &to_nonce);
    auto str_key = block_tx.to() + unique_hash;
    std::string val;
    if (shardora_host.GetKeyValue(block_tx.to(), unique_hash, &val) == shardoravm::kShardoravmSuccess) {
        SHARDORA_DEBUG("unique hash has consensus: %s", common::Encode::HexEncode(unique_hash).c_str());
        return consensus::kConsensusError;
    }

    block_tx.set_unique_hash(unique_hash);
    auto res = processElect(shardora_host, view_block, block_tx);
    if (res != consensus::kConsensusSuccess) {
        return kConsensusError;
    }

    shardora_host.SaveKeyValue(block_tx.to(), unique_hash, "1");
    SHARDORA_WARN("success call elect block pool: %d, view: %lu, to_nonce: %lu. tx nonce: %lu", 
        view_block.qc().pool_index(), view_block.qc().view(), to_nonce, block_tx.nonce());
    acc_balance_map[block_tx.to()]->set_balance(to_balance);
    acc_balance_map[block_tx.to()]->set_nonce(block_tx.nonce());
    acc_balance_map[block_tx.to()]->set_latest_height(view_block.block_info().height());
    acc_balance_map[block_tx.to()]->set_tx_index(tx_index);
    SHARDORA_DEBUG("success add addr: %s, value: %s, uqniue hash: %s", 
        common::Encode::HexEncode(block_tx.to()).c_str(), 
        ProtobufToJson(*(acc_balance_map[block_tx.to()])).c_str(),
        common::Encode::HexEncode(unique_hash).c_str());
    
    // *view_block.mutable_block_info()->mutable_elect_statistic() = elect_statistic_;
    *view_block.mutable_block_info()->mutable_elect_block() = *elect_block_;
    view_block.mutable_block_info()->add_unique_hashs(block_tx.unique_hash());
    block::protobuf::TxHashStatus tx_hash_status;
    tx_hash_status.set_status(block_tx.status());
    auto status_val = tx_hash_status.SerializeAsString();
    shardora_host.SaveKeyValue("tx", block_tx.tx_hash(), status_val);
    shardora_host.MergeToPrev();
    return consensus::kConsensusSuccess;
}

int ElectTxItem::processElect(
        shardoravm::ShardorahainHost& shardora_host,
        view_block::protobuf::ViewBlockItem& view_block,
        shardora::block::protobuf::BlockTx &block_tx) {
    auto& block = *view_block.mutable_block_info();
    const pools::protobuf::PoolStatisticItem *statistic = nullptr;
    shardora::common::MembersPtr members = nullptr;
    int retVal = getMaxElectHeightInfo(statistic, members);
    if ( retVal != kConsensusSuccess) {
        SHARDORA_DEBUG("getMaxElectHeightInfo failed ret val: %d", retVal);
        // //assert(false);
        return retVal;
    }

    elect_members_ = members;
    for (auto iter = members->begin(); iter != members->end(); ++iter) {
        added_nodes_.insert((*iter)->pubkey);
        SHARDORA_DEBUG("success add now elect member: %s, %s",
            common::Encode::HexEncode(sec_ptr_->GetAddressWithPublicKey((*iter)->pubkey)).c_str(),
            common::Encode::HexEncode((*iter)->pubkey).c_str());
    }

    uint32_t min_area_weight = common::kInvalidUint32;
    uint32_t min_tx_count = common::kInvalidUint32;
    std::vector<NodeDetailPtr> elect_nodes(members->size(), nullptr);
    int res = CheckWeedout(members, *statistic, &min_area_weight, &min_tx_count, elect_nodes);
    if (res != kConsensusSuccess) {
        //assert(false);
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

    //assert(expect_leader_count > 0);
    std::set<uint32_t> leader_nodes;
#ifndef NDEBUG
    {
        std::string ids;
        for (uint32_t i = 0; i < src_elect_nodes_to_choose.size(); ++i) {
            ids += common::Encode::HexEncode(src_elect_nodes_to_choose[i]->pubkey) + ":" +
                std::to_string(src_elect_nodes_to_choose[i]->index) + ",";
        }

        SHARDORA_DEBUG("befor get leader: %s", ids.c_str());
    }
#endif

    FtsGetNodes(src_elect_nodes_to_choose, false, expect_leader_count, leader_nodes);
    SHARDORA_DEBUG("net: %u, elect use height to random order: %lu, leader size: %d, "
        "nodes count: %u, leader size: %d, random_str: %s, leader index: %d",
        elect_statistic_.sharding_id(), vss_mgr_->EpochRandom(), expect_leader_count,
        elect_nodes.size(), leader_nodes.size(), random_str.c_str(), *leader_nodes.begin());
    if (leader_nodes.size() != (uint32_t)expect_leader_count) {
        SHARDORA_ERROR("choose leader failed: %u", elect_statistic_.sharding_id());
        return kConsensusError;
    }

    int32_t mode_idx = 0;
    for (auto iter = src_elect_nodes_to_choose.begin(); iter != src_elect_nodes_to_choose.end(); ++iter) {
        if (leader_nodes.find((*iter)->index) != leader_nodes.end()) {
            (*iter)->leader_mod_index = mode_idx++;
        }
    }

#ifndef NDEBUG
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

        SHARDORA_DEBUG("LLLLLL before CreateNewElect: count %d, %s", count, ids.c_str());
    }
#endif

    CreateNewElect(
        shardora_host,
        block,
        elect_nodes,
        gas_for_root,
        block_tx);

#ifndef NDEBUG
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

        SHARDORA_DEBUG("LLLLL after CreateNewElect: count: %d ,%s", count, ids.c_str());
    }
#endif
    // Persist per-election log for analysis (one file per round)
    try {
        std::filesystem::path log_dir = std::filesystem::path("elect_logs");
        std::error_code ec;
        std::filesystem::create_directories(log_dir, ec);
        if (ec) {
            SHARDORA_ERROR("create elect_logs dir failed: %s", ec.message().c_str());
        } else {
            auto now = std::chrono::system_clock::now();
            auto now_ts = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
            std::ostringstream fname;
            fname << "elect_" << elect_statistic_.sharding_id() << "_" << now_ts << "_" << elect_statistic_.statistic_height() << ".json";
            auto filepath = log_dir / fname.str();

            std::ofstream ofs(filepath.string(), std::ios::out | std::ios::trunc);
            if (ofs) {
                ofs << "{\n";
                ofs << "  \"timestamp\": " << now_ts << ",\n";
                ofs << "  \"shard\": " << elect_statistic_.sharding_id() << ",\n";
                ofs << "  \"statistic_height\": " << elect_statistic_.statistic_height() << ",\n";

                ofs << "  \"fts_params\": {\n";
                ofs << "    \"weedout_div_rate\": " << kFtsWeedoutDividRate << ",\n";
                ofs << "    \"new_elect_join_rate\": " << kFtsNewElectJoinRate << ",\n";
                ofs << "    \"min_double_node_count\": " << kFtsMinDoubleNodeCount << ",\n";
                ofs << "    \"area_penalty_coefficient\": " << ElectTxItem::kAreaPenaltyCoefficient << "\n";
                ofs << "  },\n";

                std::string proto_json = ProtobufToJson(elect_statistic_);
                ofs << "  \"elect_statistic_proto\": ";
                ofs << proto_json << ",\n";

                ofs << "  \"leaders\": [";
                bool first = true;
                for (auto idx : leader_nodes) {
                    if (!first) ofs << ", ";
                    first = false;
                    ofs << idx;
                }
                ofs << "],\n";

                ofs << "  \"nodes\": [\n";
                bool first_node = true;
                for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                    if (elect_nodes[i] == nullptr) continue;
                    if (!first_node) ofs << ",\n";
                    first_node = false;
                    auto &n = elect_nodes[i];
                    ofs << "    {\n";
                    ofs << "      \"index\": " << n->index << ",\n";
                    ofs << "      \"pubkey\": \"" << common::Encode::HexEncode(n->pubkey) << "\",\n";
                    ofs << "      \"fts_value\": " << n->fts_value << ",\n";
                    ofs << "      \"stoke\": " << n->stoke << ",\n";
                    ofs << "      \"stoke_diff\": " << n->stoke_diff << ",\n";
                    ofs << "      \"tx_count\": " << n->tx_count << ",\n";
                    ofs << "      \"gas_sum\": " << n->gas_sum << ",\n";
                    ofs << "      \"credit\": " << n->credit << ",\n";
                    ofs << "      \"area_weight\": " << n->area_weight << ",\n";
                    ofs << "      \"leader_mod_index\": " << n->leader_mod_index << ",\n";
                    ofs << "      \"mining_token\": " << n->mining_token << ",\n";
                    ofs << "      \"consensus_gap\": " << n->consensus_gap << "\n";
                    ofs << "    }";
                }
                ofs << "\n  ]\n";
                ofs << "}\n";
                ofs.close();
                SHARDORA_DEBUG("wrote elect log: %s", filepath.string().c_str());
            } else {
                SHARDORA_ERROR("open elect log file failed: %s", filepath.string().c_str());
            }
        }
    } catch (const std::exception &e) {
        SHARDORA_ERROR("exception while writing elect log: %s", e.what());
    }

    SHARDORA_DEBUG("consensus elect tx success: %u, proto: %s",
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
    // Get related members based on the latest election block height
    members = elect_mgr_->GetNetworkMembersWithHeight(
        now_elect_height,
        elect_statistic_.sharding_id(),
        nullptr,
        nullptr);
    if (members == nullptr) {
        SHARDORA_WARN("get members failed, elect height: %lu, net: %u",
            now_elect_height, elect_statistic_.sharding_id());
        // //assert(false);
        return kConsensusError;
    }

    if (max_elect_height < now_elect_height) {
        auto older_members = elect_mgr_->GetNetworkMembersWithHeight(
            max_elect_height,
            elect_statistic_.sharding_id(),
            nullptr,
            nullptr);
        if (older_members == nullptr) {
            SHARDORA_WARN("get members failed, elect height: %lu, net: %u",
                max_elect_height, elect_statistic_.sharding_id());
            // //assert(false);
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

    if (max_elect_height != now_elect_height) {
        SHARDORA_DEBUG("old elect coming max_elect_height: %lu, now_elect_height: %lu",
            max_elect_height, now_elect_height);
        return kConsensusError;
    }

    SHARDORA_DEBUG("success check old elect coming max_elect_height: %lu, now_elect_height: %lu",
        max_elect_height, now_elect_height);
    int32_t member_count = members->size();
    if (member_count != statistic->tx_count_size() ||
            member_count != statistic->stokes_size() ||
            member_count != statistic->area_point_size()) {
        SHARDORA_DEBUG("now_elect_height: %lu, member size error: %u, %u, %u, %u",
            now_elect_height, members->size(), statistic->tx_count_size(),
            statistic->stokes_size(), statistic->area_point_size());
        //assert(false);
        return kConsensusError;
    }
    return kConsensusSuccess;
}

void ElectTxItem::JoinNewNodes2ElectNodes(
        shardora::common::MembersPtr &members,
        std::vector<shardora::consensus::NodeDetailPtr> &elect_nodes,
        uint32_t min_area_weight,
        uint32_t min_tx_count) {
    // Calculate the number of newly added nodes
    // If the maximum number of nodes is not reached, the newly added nodes are 10% of the current number of nodes
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

    SHARDORA_DEBUG("add new node count: %u", join_count);
    for (uint32_t i = 0; i < join_count; ++i) {
        elect_nodes.push_back(nullptr);
    }
    // First fill in the new nodes with fixed positions, then fill in the random nodes
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

        SHARDORA_DEBUG("LLLLLL after join elect: count:%d, %s", count, ids.c_str());
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

        SHARDORA_DEBUG("elect add new node: %u, index: %d, hold pos: %d",
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
            //assert(false);
            return;
        }

        if (elect_nodes[i] != nullptr) {
            SHARDORA_DEBUG("LLLLL elect add new node: %s",
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
        SHARDORA_DEBUG("join new node: %s, des shard: %u, statistic shrad: %u",
                  common::Encode::HexEncode(elect_statistic_.join_elect_nodes(i).pubkey()).c_str(),
                  elect_statistic_.join_elect_nodes(i).shard(),
                  elect_statistic_.sharding_id());

        auto iter = added_nodes_.find(elect_statistic_.join_elect_nodes(i).pubkey());
        if (iter != added_nodes_.end()) {
            SHARDORA_DEBUG("join new node failed: %s, already in committee", common::Encode::HexEncode(elect_statistic_.join_elect_nodes(i).pubkey()).c_str());
            continue;
        }

        if (elect_statistic_.join_elect_nodes(i).shard() != elect_statistic_.sharding_id()) {
            SHARDORA_DEBUG("join new node failed: %s, not in this sharding", common::Encode::HexEncode(elect_statistic_.join_elect_nodes(i).pubkey()).c_str());
            continue;
        }

        if (index != common::kInvalidUint32) {
            if (elect_statistic_.join_elect_nodes(i).elect_pos() != (int32_t)index) {
                SHARDORA_DEBUG("join new node failed: %s, not in this index, new node index :%d, need index:%d",
                    common::Encode::HexEncode(elect_statistic_.join_elect_nodes(i).pubkey()).c_str(),
                    elect_statistic_.join_elect_nodes(i).elect_pos(),
                    index);
                continue;
            }
        }

        auto id = sec_ptr_->GetAddressWithPublicKey(elect_statistic_.join_elect_nodes(i).pubkey());
        protos::AddressInfoPtr account_info = view_block_chain_->ChainGetAccountInfo(id);
        if (account_info == nullptr) {
            //assert(false);
            return;
        }

        auto node_info = std::make_shared<ElectNodeInfo>();
        node_info->area_weight = min_area_weight;
        node_info->stoke = elect_statistic_.join_elect_nodes(i).stoke();
        node_info->tx_count = min_tx_count;
        node_info->credit = elect_statistic_.join_elect_nodes(i).credit();
        node_info->pubkey = elect_statistic_.join_elect_nodes(i).pubkey();
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
    uint64_t gas_burned = 0;

    // Burn disabled by configuration: do not burn gas, add it to incentive pool.
    // Previously we called ApplyBurnMechanism(all_gas_amount, &gas_for_mining, &gas_burned)
    // which would split some gas to be burned. Now gas_burned stays 0 and the full
    // amount remains available for incentives (subject to root allocation below).
    
    // root shard use statistic gas amount.
    if (statistic_sharding_id == network::kRootCongressNetworkId) {
        // For root shard, apply burn to the full amount
        // gas_for_mining already set by ApplyBurnMechanism
    } else {
        // For other shards, allocate some to root
        uint64_t gas_for_root_before_burn = all_gas_amount / network_count_;
        *gas_for_root = gas_for_root_before_burn;
        gas_for_mining = all_gas_amount - gas_for_root_before_burn - gas_burned;
    }
    
    ELECT_INFO("MiningToken: shard=%u, total_gas=%lu, burned=%lu, for_mining=%lu, for_root=%lu",
               statistic_sharding_id, all_gas_amount, gas_burned, gas_for_mining,
               (gas_for_root ? *gas_for_root : 0));

    auto now_ming_count = GetMiningMaxCount(max_tx_count);
    uint64_t tmp_all_gas_amount = 0;
    if (!stop_mining_) {
        for (uint32_t i = 0; i < valid_nodes.size(); ++i) {
            auto id = sec_ptr_->GetAddressWithPublicKey(valid_nodes[i]->pubkey);
            protos::AddressInfoPtr account_info = view_block_chain_->ChainGetAccountInfo(id);
            if (account_info == nullptr) {
                SHARDORA_DEBUG("get account info failed: %s",
                          common::Encode::HexEncode(id).c_str());
                //assert(false);
                continue;
            }

            auto tx_count = valid_nodes[i]->tx_count;
            if (tx_count == 0) {
                tx_count = 1;
            }

            auto mining_token = now_ming_count * tx_count / max_tx_count;
            valid_nodes[i]->mining_token = mining_token;
            //            auto gas_token = tx_count * gas_for_mining / all_tx_count;
            //          Only the leader node will get all the gas
            auto gas_token = valid_nodes[i]->gas_sum;
            if (i + 1 == valid_nodes.size()) {
                //assert(gas_for_mining >= tmp_all_gas_amount);
                gas_token = gas_for_mining - tmp_all_gas_amount;
            }

            valid_nodes[i]->mining_token += gas_token;
            tmp_all_gas_amount += gas_token;
            SHARDORA_DEBUG("elect mining %s, mining: %lu, gas mining: %lu, all gas: %lu, src: %lu",
                      common::Encode::HexEncode(id).c_str(),
                      mining_token, gas_token, tmp_all_gas_amount, gas_for_mining);
        }
    }

    //assert(tmp_all_gas_amount == gas_for_mining);
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
        //assert(false);
        return;
    }

    *block_item.mutable_prev_elect_block() = prev_block_item.elect_block();
    SHARDORA_DEBUG("success set prev elect block info: %s",
        ProtobufToJson(prev_block_item.elect_block()).c_str());
}

// ============================================================================
// Dynamic Sharding Reward System Implementation
// ============================================================================

uint64_t ElectTxItem::GetCurrentEpochNumber() {
    if (first_timeblock_timestamp_ == 0) {
        ELECT_ERROR("first_timeblock_timestamp_ is 0, cannot calculate epoch number");
        return 0;
    }
    
    uint64_t current_timestamp = common::TimeUtils::TimestampSeconds();
    if (current_timestamp < first_timeblock_timestamp_) {
        ELECT_ERROR("current_timestamp %lu < first_timeblock_timestamp_ %lu",
                    current_timestamp, first_timeblock_timestamp_);
        return 0;
    }
    
    uint64_t elapsed_seconds = current_timestamp - first_timeblock_timestamp_;
    uint64_t epoch_number = elapsed_seconds / common::kTimeBlockCreatePeriodSeconds;
    
    ELECT_DEBUG("GetCurrentEpochNumber: current_ts=%lu, first_ts=%lu, elapsed=%lu, epoch=%lu",
                current_timestamp, first_timeblock_timestamp_, elapsed_seconds, epoch_number);
    
    return epoch_number;
}

uint64_t ElectTxItem::CalculateBaseReward(uint64_t epoch_number) {
    // Calculate halving count (every 4 years)
    uint32_t halving_count = static_cast<uint32_t>(epoch_number / common::kHalvingPeriodEpochs);
    
    // Prevent overflow by limiting halving count
    if (halving_count > common::kMaxHalvingCount) {
        halving_count = common::kMaxHalvingCount;
    }
    
    // Calculate base reward with halving: initial_reward / (2^halving_count)
    uint64_t base_reward = common::kInitialTotalReward;
    for (uint32_t i = 0; i < halving_count; ++i) {
        base_reward /= 2;
        if (base_reward < common::kMinBlockReward) {
            base_reward = common::kMinBlockReward;
            break;
        }
    }
    
    ELECT_DEBUG("CalculateBaseReward: epoch=%lu, halving_count=%u, base_reward=%lu",
                epoch_number, halving_count, base_reward);
    
    return base_reward;
}

uint32_t ElectTxItem::GetShardGeneration(uint32_t shard_id) {
    // Find which generation this shard belongs to
    for (uint32_t i = 0; i < common::kShardGenerationCount; ++i) {
        if (shard_id >= common::kShardGenerations[i].start_shard_id &&
            shard_id <= common::kShardGenerations[i].end_shard_id) {
            return common::kShardGenerations[i].generation;
        }
    }
    
    // If not found, return last generation (should not happen)
    ELECT_WARN("Shard %u not found in generation table, using last generation", shard_id);
    return common::kShardGenerations[common::kShardGenerationCount - 1].generation;
}

uint32_t ElectTxItem::GetActiveShardCount() {
    // Get the maximum consensus sharding ID from network
    // This should be provided by the network layer
    // For now, we use a simple heuristic based on elect_statistic_
    
    uint32_t max_shard_id = network_count_;  // Use network_count_ as proxy
    
    // Count how many shards are active based on generation table
    uint32_t active_count = 0;
    for (uint32_t i = 0; i < common::kShardGenerationCount; ++i) {
        if (max_shard_id >= common::kShardGenerations[i].start_shard_id) {
            // All shards in this generation are active
            active_count += common::kShardGenerations[i].shard_count;
        } else {
            break;
        }
    }
    
    // Ensure at least initial shard count
    if (active_count < common::kInitialShardCount) {
        active_count = common::kInitialShardCount;
    }
    
    ELECT_DEBUG("GetActiveShardCount: max_shard_id=%u, active_count=%u",
                max_shard_id, active_count);
    
    return active_count;
}

double ElectTxItem::CalculateTotalWeight(uint32_t active_shard_count) {
    double total_weight = 0.0;
    uint32_t counted_shards = 0;
    
    // Sum up weights for all active generations
    for (uint32_t i = 0; i < common::kShardGenerationCount; ++i) {
        const auto& gen = common::kShardGenerations[i];
        
        if (counted_shards >= active_shard_count) {
            break;
        }
        
        // Calculate how many shards from this generation are active
        uint32_t shards_in_gen = gen.shard_count;
        if (counted_shards + shards_in_gen > active_shard_count) {
            shards_in_gen = active_shard_count - counted_shards;
        }
        
        total_weight += gen.weight * shards_in_gen;
        counted_shards += shards_in_gen;
        
        ELECT_DEBUG("Gen %u: weight=%.6f, shards=%u, contribution=%.6f",
                    gen.generation, gen.weight, shards_in_gen, gen.weight * shards_in_gen);
    }
    
    ELECT_DEBUG("CalculateTotalWeight: active_shards=%u, total_weight=%.6f",
                active_shard_count, total_weight);
    
    return total_weight;
}

double ElectTxItem::CalculateEarlyBonus(uint32_t active_shard_count) {
    // Apply 10% bonus when network has fewer than max shards
    double bonus = (active_shard_count < common::kMaxShardCount) 
                   ? common::kEarlyBonusMultiplier 
                   : 1.0;
    
    ELECT_DEBUG("CalculateEarlyBonus: active_shards=%u, bonus=%.2f",
                active_shard_count, bonus);
    
    return bonus;
}

uint64_t ElectTxItem::CalculateShardReward(
        uint32_t shard_id,
        uint64_t total_base_reward,
        uint32_t active_shard_count) {
    
    // Get shard generation and weight
    uint32_t generation = GetShardGeneration(shard_id);
    double weight = common::kShardGenerations[generation].weight;
    
    // Calculate total weight across all active shards
    double total_weight = CalculateTotalWeight(active_shard_count);
    
    if (total_weight <= 0.0) {
        ELECT_ERROR("Total weight is zero or negative: %.6f", total_weight);
        return common::kMinBlockReward;
    }
    
    // Calculate this shard's reward based on its weight
    uint64_t shard_reward = static_cast<uint64_t>(
        (total_base_reward * weight) / total_weight);
    
    // Ensure minimum reward
    if (shard_reward < common::kMinBlockReward) {
        shard_reward = common::kMinBlockReward;
    }
    
    ELECT_INFO("CalculateShardReward: shard=%u, gen=%u, weight=%.6f, "
               "total_base=%lu, total_weight=%.6f, shard_reward=%lu",
               shard_id, generation, weight, total_base_reward, 
               total_weight, shard_reward);
    
    return shard_reward;
}

uint64_t ElectTxItem::CalculateTxBonus(uint64_t shard_reward, uint64_t max_tx_count) {
    if (max_tx_count == 0) {
        return 0;
    }
    
    // Normalize transaction count to 0-1 range using log2
    // log2(1) = 0, log2(1048576) ≈ 20, so we divide by 20 to normalize
    double tx_count_normalized = log2(static_cast<double>(max_tx_count + 1)) / 20.0;
    if (tx_count_normalized > 1.0) {
        tx_count_normalized = 1.0;
    }
    
    // Calculate transaction bonus (20% of shard reward)
    uint64_t tx_bonus = static_cast<uint64_t>(
        shard_reward * tx_count_normalized * common::kTxBonusMultiplier);
    
    ELECT_DEBUG("CalculateTxBonus: shard_reward=%lu, max_tx_count=%lu, "
                "normalized=%.4f, tx_bonus=%lu",
                shard_reward, max_tx_count, tx_count_normalized, tx_bonus);
    
    return tx_bonus;
}

uint64_t ElectTxItem::CalculateTotalEpochReward(uint32_t shard_id, uint64_t max_tx_count) {
    // 1. Get current epoch number
    uint64_t epoch_number = GetCurrentEpochNumber();
    
    // 2. Calculate base reward (with halving every 4 years)
    uint64_t base_reward = CalculateBaseReward(epoch_number);
    
    // 3. Get current active shard count
    uint32_t active_shards = GetActiveShardCount();
    
    // 4. Apply early bonus (10% when shards < 1024)
    double early_bonus = CalculateEarlyBonus(active_shards);
    uint64_t total_base_reward = static_cast<uint64_t>(base_reward * early_bonus);
    
    // 5. Calculate this shard's base reward
    uint64_t shard_reward = CalculateShardReward(shard_id, total_base_reward, active_shards);
    
    // 6. Add transaction bonus
    uint64_t tx_bonus = CalculateTxBonus(shard_reward, max_tx_count);
    
    uint64_t total_reward = shard_reward + tx_bonus;
    
    ELECT_INFO("CalculateTotalEpochReward: shard=%u, epoch=%lu, base=%lu, "
               "early_bonus=%.2f, active_shards=%u, shard_reward=%lu, "
               "tx_bonus=%lu, total=%lu",
               shard_id, epoch_number, base_reward, early_bonus, active_shards,
               shard_reward, tx_bonus, total_reward);
    
    return total_reward;
}

void ElectTxItem::ApplyBurnMechanism(
        uint64_t total_gas,
        uint64_t* gas_to_distribute,
        uint64_t* gas_to_burn) {
    if (gas_to_distribute == nullptr || gas_to_burn == nullptr) {
        ELECT_ERROR("ApplyBurnMechanism: null pointer parameters");
        return;
    }
    
    // Calculate gas to burn (EIP-1559 style)
    *gas_to_burn = static_cast<uint64_t>(total_gas * common::kBurnRatio);
    *gas_to_distribute = total_gas - *gas_to_burn;
    
    ELECT_INFO("ApplyBurnMechanism: total_gas=%lu, burned=%lu, distributed=%lu, burn_ratio=%.2f",
               total_gas, *gas_to_burn, *gas_to_distribute, common::kBurnRatio);
}

// ============================================================================
// Original Mining Functions (Modified to use dynamic sharding reward system)
// ============================================================================

uint64_t ElectTxItem::GetMiningMaxCount(uint64_t max_tx_count) {
    // Get the shard ID from elect_statistic_
    uint32_t shard_id = elect_statistic_.sharding_id();
    
    // Use new dynamic sharding reward system
    uint64_t total_epoch_reward = CalculateTotalEpochReward(shard_id, max_tx_count);
    
    ELECT_DEBUG("GetMiningMaxCount: shard=%u, max_tx_count=%lu, total_epoch_reward=%lu",
                shard_id, max_tx_count, total_epoch_reward);
    
    return total_epoch_reward;
}

int ElectTxItem::CreateNewElect(
        shardoravm::ShardorahainHost& shardora_host,
        block::protobuf::Block &block,
        const std::vector<NodeDetailPtr> &elect_nodes,
        uint64_t gas_for_root,
        block::protobuf::BlockTx &block_tx) {
    auto& elect_block = *elect_block_;
    for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
        if (elect_nodes[i] == nullptr) {
            if (i >= elect_members_->size()) {
                break;
            }

            auto in = elect_block.add_in();
            in->set_pubkey((*elect_members_)[i]->pubkey);
            in->set_pool_idx_mod_num(-1);
            in->set_mining_amount(0);
            SHARDORA_DEBUG("elect_nodes[i] == nullptr: %s, i: %d, id: %s, member size: %d, "
                "pool_idx_mod_num: %d, mining_amount: %lu",
                common::Encode::HexEncode((*elect_members_)[i]->pubkey).c_str(), 
                i,
                common::Encode::HexEncode((*elect_members_)[i]->id).c_str(),
                elect_members_->size(),
                -1,
                0);
        } else {
            auto in = elect_block.add_in();
            in->set_pubkey(elect_nodes[i]->pubkey);
            in->set_pool_idx_mod_num(elect_nodes[i]->leader_mod_index);
            in->set_mining_amount(elect_nodes[i]->mining_token);
            in->set_fts_value(elect_nodes[i]->fts_value);
            SHARDORA_DEBUG("elect_nodes[i] == nullptr: %s, i: %d, id: %s, member size: %d, "
                "pool_idx_mod_num: %d, mining_amount: %lu",
                common::Encode::HexEncode((*elect_members_)[i]->pubkey).c_str(), 
                i,
                common::Encode::HexEncode((*elect_members_)[i]->id).c_str(),
                elect_members_->size(),
                elect_nodes[i]->leader_mod_index,
                elect_nodes[i]->mining_token);
        }
    }

    elect_block.set_gas_for_root(gas_for_root);
    elect_block.set_shard_network_id(elect_statistic_.sharding_id());
    elect_block.set_elect_height(block.height());
    elect_block.set_all_gas_amount(elect_statistic_.gas_amount());
    if (elect_block.has_prev_members()) {
        SHARDORA_WARN("success add bls consensus info: %u, %lu",
                  elect_statistic_.sharding_id(),
                  elect_block.prev_members().prev_elect_height());
        SetPrevElectInfo(elect_block, block);
    } else {
        SHARDORA_WARN("no prev members, maybe first elect: %u, %lu",
                  elect_statistic_.sharding_id(),
                  elect_block.elect_height());
    }
    
    return kConsensusSuccess;
}
/**
 * @param members
 * @param statistic_item Statistics from consensus nodes
 * @param min_area_weight return Minimum logical distance between nodes
 * @param min_tx_count Return result Minimum transaction count
 * @param elect_nodes Return result Nodes with less than max_tx_count/2 are eliminated, and the original node position remains unchanged
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
        SHARDORA_DEBUG("LLLLL before WeedOut count %d : %s", statistic_item.tx_count_size(), dugstr.c_str());
    }

    uint32_t weed_out_count = statistic_item.tx_count_size() * kFtsWeedoutDividRate / 100; // 10% of the old committee will be eliminated
    uint32_t direct_weed_out_count = weed_out_count / 2;

    // Calculate maximum transaction volume Sort by transaction volume in descending order
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

    // First elimination
    // Eliminate at most direct_weedout_count nodes due to low transaction volume
    for (uint32_t i = 0; i < direct_weed_out_count; ++i) {
        if (member_tx_count[i].second < direct_weedout_tx_count) {
            invalid_nodes.insert(member_tx_count[i].first);
            SHARDORA_DEBUG("direct weedout: %s, tx count: %u, max_tx_count: %u",
                      common::Encode::HexEncode(sec_ptr_->GetAddressWithPublicKey((*members)[member_tx_count[i].first]->pubkey)).c_str(),
                      statistic_item.tx_count(member_tx_count[i].first), max_tx_count);
        }
    }

    // Calculate area dispersion metrics for all nodes first
    std::vector<std::vector<uint32_t>> all_distances(statistic_item.tx_count_size());
    for (int32_t member_idx = 0; member_idx < statistic_item.tx_count_size(); ++member_idx) {
        if (invalid_nodes.find(member_idx) != invalid_nodes.end()) {
            continue;
        }
        
        // Calculate distances to all other valid nodes
        std::vector<uint32_t> distances;
        for (int32_t idx = 0; idx < statistic_item.tx_count_size(); ++idx) {
            if (member_idx == idx || invalid_nodes.find(idx) != invalid_nodes.end()) {
                continue;
            }
            
            auto &point0 = statistic_item.area_point(member_idx);
            auto &point1 = statistic_item.area_point(idx);
            uint32_t dis = (point0.x() - point1.x()) * (point0.x() - point1.x()) +
                           (point0.y() - point1.y()) * (point0.y() - point1.y());
            distances.push_back(dis);
        }
        
        all_distances[member_idx] = distances;
    }

    std::vector<NodeDetailPtr> elect_nodes_to_choose;
    for (int32_t member_idx = 0; member_idx < statistic_item.tx_count_size(); ++member_idx) {
        if (invalid_nodes.find(member_idx) != invalid_nodes.end()) {
            continue;
        }

        // Calculate area_weight using improved algorithm:
        // area_weight = avg_distance + (std_dev * 0.5) + (median_distance * 0.3)
        // This considers average proximity, variance, and median to better assess dispersion
        uint32_t area_weight = 0;
        
        if (!all_distances[member_idx].empty()) {
            auto &distances = all_distances[member_idx];
            std::sort(distances.begin(), distances.end());
            
            // Calculate average distance
            uint64_t sum_distance = 0;
            for (auto d : distances) {
                sum_distance += d;
            }
            double avg_distance = static_cast<double>(sum_distance) / distances.size();
            
            // Calculate median distance
            uint32_t median_distance = distances[distances.size() / 2];
            
            // Calculate standard deviation
            double sq_sum = 0.0;
            for (auto d : distances) {
                double diff = d - avg_distance;
                sq_sum += diff * diff;
            }
            double std_dev = std::sqrt(sq_sum / distances.size());
            
            // Composite area weight: prioritize average proximity with variance adjustment
            // Higher value = better dispersion (greater average distance from neighbors)
            double composite_weight = avg_distance + (std_dev * 0.5) + (median_distance * 0.3);
            area_weight = static_cast<uint32_t>(composite_weight);
            
            SHARDORA_DEBUG("Node %d: avg=%.1f, median=%u, std_dev=%.1f, composite_weight=%u",
                      member_idx, avg_distance, median_distance, std_dev, area_weight);
        } else {
            // Fallback if no valid neighbors (should not happen)
            area_weight = common::kInvalidUint32;
        }
        
        // Build node information and update global minimum node distance
        protos::AddressInfoPtr account_info = view_block_chain_->ChainGetAccountInfo((*members)[member_idx]->id);
        if (account_info == nullptr) {
            SHARDORA_ERROR("get account info failed: %s",
                      common::Encode::HexEncode((*members)[member_idx]->id).c_str());
            //assert(false);
            return kConsensusError;
        }

        auto node_info = std::make_shared<ElectNodeInfo>();
        node_info->gas_sum = statistic_item.gas_sum(member_idx);
        node_info->area_weight = area_weight;
        node_info->tx_count = statistic_item.tx_count(member_idx);
        node_info->stoke = statistic_item.stokes(member_idx);
        node_info->credit = statistic_item.credit(member_idx);
        node_info->index = member_idx;
        node_info->pubkey = (*members)[member_idx]->pubkey;
        node_info->consensus_gap = statistic_item.consensus_gap(member_idx); 
        if (*min_area_weight > area_weight && area_weight != common::kInvalidUint32) {
            *min_area_weight = area_weight;
        }

        elect_nodes_to_choose.push_back(node_info);
    }

    if (elect_nodes_to_choose.empty()) {
        SHARDORA_WARN("elect sharding nodes empty.");
        return kConsensusError;
    }

    std::set<uint32_t> weedout_nodes;
    FtsGetNodes(elect_nodes_to_choose, true, weed_out_count - invalid_nodes.size(), weedout_nodes);
    for (auto iter = elect_nodes_to_choose.begin(); iter != elect_nodes_to_choose.end(); ++iter) {
        if (weedout_nodes.find((*iter)->index) != weedout_nodes.end()) {
            SHARDORA_DEBUG("fts weedout: %s, tx count: %u, max_tx_count: %u",
                      common::Encode::HexEncode(sec_ptr_->GetAddressWithPublicKey((*members)[(*iter)->index]->pubkey)).c_str(),
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
                debugStr += common::Encode::HexEncode(sec_ptr_->GetAddressWithPublicKey((*members)[i]->pubkey)) + " ";
            } else {
                debugStr += "null ";
            }
        }
        SHARDORA_DEBUG("LLLLL after weedOut count:%d , %s", cout, debugStr.c_str());
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
        SHARDORA_DEBUG("success add join elect node: %s",
                  common::Encode::HexEncode((*iter)->pubkey).c_str());
        //assert(!(*iter)->pubkey.empty());
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
            ids += common::Encode::HexEncode(sec_ptr_->GetAddressWithPublicKey(elect_nodes[i]->pubkey)) + ":" +
                   std::to_string(elect_nodes[i]->fts_value) + ",";
        }

        SHARDORA_DEBUG("fts value: %s", ids.c_str());
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

        SHARDORA_DEBUG("before sort: %s", ids.c_str());
    }
    std::stable_sort(elect_nodes.begin(), elect_nodes.end(), ElectNodePosCompare);
    {
        std::string ids;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ":" + std::to_string(elect_nodes[i]->stoke) + ",";
        }

        SHARDORA_DEBUG("before sort 0: %s", ids.c_str());
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

        SHARDORA_DEBUG("after sort: %s", ids.c_str());
    }
    std::stable_sort(elect_nodes.begin(), elect_nodes.end(), ElectNodePosDiffCompare);
    {
        std::string ids;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ":" + std::to_string(elect_nodes[i]->stoke_diff) + ",";
        }

        SHARDORA_DEBUG("after sort 1: %s", ids.c_str());
    }
    uint64_t diff_2b3 = elect_nodes[elect_nodes.size() * 2 / 3]->stoke_diff;
    std::stable_sort(elect_nodes.begin(), elect_nodes.end(), ElectNodePosCompare);
    {
        std::string ids;
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ":" + std::to_string(elect_nodes[i]->stoke) + ",";
        }

        SHARDORA_DEBUG("after sort 2: %s", ids.c_str());
    }
    // Normalize PoS weight to [100, 10000]
    std::vector<int32_t> pos_weight;
    {
        pos_weight.resize(elect_nodes.size(), 0);
        pos_weight[0] = 100;
        int32_t min_pos = 100;
        int32_t max_pos = 100;
        auto &g2 = *g2_;
        
        // Calculate raw pos weights based on stoke
        for (uint32_t i = 1; i < elect_nodes.size(); ++i) {
            uint64_t fts_val_diff = elect_nodes[i]->stoke - elect_nodes[i - 1]->stoke;
            if (fts_val_diff == 0) {
                pos_weight[i] = pos_weight[i - 1];
            } else {
                if (fts_val_diff < diff_2b3) {
                    auto rand_val = fts_val_diff + g2() % (diff_2b3 - fts_val_diff);
                    pos_weight[i] = pos_weight[i - 1] + (20 * rand_val) / diff_2b3;
                } else {
                    auto rand_val = diff_2b3 + g2() % (fts_val_diff + 1 - diff_2b3);
                    pos_weight[i] = pos_weight[i - 1] + (20 * rand_val) / fts_val_diff;
                }
            }

            if (min_pos > pos_weight[i]) {
                min_pos = pos_weight[i];
            }

            if (max_pos < pos_weight[i]) {
                max_pos = pos_weight[i];
            }
        }

        // Normalize to [100, 10000]
        if (max_pos > min_pos) {
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                pos_weight[i] = 100 + (pos_weight[i] - min_pos) * 9900 / (max_pos - min_pos);
            }
        } else {
            // All nodes have same stoke
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                pos_weight[i] = 100;
            }
        }
    }

    // Normalize credit weight to [100, 10000]
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

        // Normalize to [100, 10000]
        if (max_credit > min_credit) {
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                credit_weight[i] = 100 + (credit_weight[i] - min_credit) * 9900 / (max_credit - min_credit);
            }
        } else {
            // All nodes have same credit
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                credit_weight[i] = 100;
            }
        }
    }

    // Normalize area weight to [100, 10000]
    std::vector<int32_t> area_weight_smooth;
    {
        area_weight_smooth.resize(elect_nodes.size(), 0);
        int32_t min_area_weight_smooth = (std::numeric_limits<int32_t>::max)();
        int32_t max_area_weight_smooth = (std::numeric_limits<int32_t>::min)();
        
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            // Convert area_weight to normalized weight
            // Larger area_weight = better dispersion (higher value)
            // Apply area penalty coefficient: larger coefficient reduces effective weight
            int32_t normalized_area = static_cast<int32_t>(
                static_cast<double>(elect_nodes[i]->area_weight) / ElectTxItem::kAreaPenaltyCoefficient);
            area_weight_smooth[i] = normalized_area;
            
            if (area_weight_smooth[i] > max_area_weight_smooth) {
                max_area_weight_smooth = area_weight_smooth[i];
            }
            
            if (area_weight_smooth[i] < min_area_weight_smooth) {
                min_area_weight_smooth = area_weight_smooth[i];
            }
        }
        
        // Normalize to [100, 10000]
        if (max_area_weight_smooth > min_area_weight_smooth) {
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                area_weight_smooth[i] = 100 + (area_weight_smooth[i] - min_area_weight_smooth) * 9900 / 
                    (max_area_weight_smooth - min_area_weight_smooth);
            }
        } else {
            // All nodes have same area weight
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                area_weight_smooth[i] = 100;
            }
        }
    }

    // Normalize gap weight to [100, 10000]
    // IMPORTANT: consensus_gap represents how many times a node has been elected.
    // Higher consensus_gap = longer tenure = should be penalized (lower score).
    // We invert the normalization: max_gap → 100, min_gap → 10000
    std::vector<int32_t> gap_weight;
    {
        gap_weight.resize(elect_nodes.size(), 0);
        int32_t min_gap_weight = (std::numeric_limits<int32_t>::max)();
        int32_t max_gap_weight = (std::numeric_limits<int32_t>::min)();
        
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            gap_weight[i] = elect_nodes[i]->consensus_gap;
            
            if (gap_weight[i] > max_gap_weight) {
                max_gap_weight = gap_weight[i];
            }

            if (gap_weight[i] < min_gap_weight) {
                min_gap_weight = gap_weight[i];
            }
        }

        // Normalize to [100, 10000] with INVERSION (penalty for long tenure)
        // Higher consensus_gap → lower gap_weight → lower FTS → lower election probability
        if (max_gap_weight > min_gap_weight) {
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                // Inverted formula: max_gap gets 100, min_gap gets 10000
                gap_weight[i] = 10000 - (gap_weight[i] - min_gap_weight) * 9900 / (max_gap_weight - min_gap_weight);
            }
        } else {
            // All nodes have same gap weight
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                gap_weight[i] = 5050;  // Middle value when all equal
            }
        }
    }

    // Normalize epoch weight to [100, 10000]
    std::vector<int32_t> epoch_weight;
    {
        epoch_weight.resize(elect_nodes.size(), 0);
        int32_t min_epoch_weight = (std::numeric_limits<int32_t>::max)();
        int32_t max_epoch_weight = (std::numeric_limits<int32_t>::min)();
        
        for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
            epoch_weight[i] = elect_nodes[i]->tx_count;
            
            if (epoch_weight[i] > max_epoch_weight) {
                max_epoch_weight = epoch_weight[i];
            }

            if (epoch_weight[i] < min_epoch_weight) {
                min_epoch_weight = epoch_weight[i];
            }
        }

        // Normalize to [100, 10000]
        if (max_epoch_weight > min_epoch_weight) {
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                epoch_weight[i] = 100 + (epoch_weight[i] - min_epoch_weight) * 9900 / (max_epoch_weight - min_epoch_weight);
            }
        } else {
            // All nodes have same epoch weight
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                epoch_weight[i] = 100;
            }
        }
    }

    std::string fts_val_str;
    for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
        // FTS calculation with 5 factors (removed ip_weight)
        elect_nodes[i]->fts_value = (2 * credit_weight[i] +
                                     2 * pos_weight[i] +
                                     2 * epoch_weight[i] +
                                     2 * area_weight_smooth[i] +
                                     2 * gap_weight[i]);
        fts_val_str += std::to_string(credit_weight[i]) + "," +
                       std::to_string(pos_weight[i]) + "," +
                       std::to_string(epoch_weight[i]) + "," +
                       std::to_string(area_weight_smooth[i]) + "," +
                       std::to_string(gap_weight[i]) + "," +
                       std::to_string(elect_nodes[i]->fts_value) + " --- ";
        if (*max_fts_val < elect_nodes[i]->fts_value) {
            *max_fts_val = elect_nodes[i]->fts_value;
        }
    }

    SHARDORA_DEBUG("fts value final: %s", fts_val_str.c_str());
}

}; // namespace consensus

}; // namespace shardora
