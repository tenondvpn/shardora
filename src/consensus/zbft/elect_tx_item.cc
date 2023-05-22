#include "consensus/zbft/elect_tx_item.h"

#include "common/fts_tree.h"
#include "protos/get_proto_hash.h"

namespace zjchain {

namespace consensus {

inline bool ElectNodeBalanceCompare(const NodeDetailPtr& left, const NodeDetailPtr& right) {
    return left->stoke < right->stoke;
}

inline bool ElectNodeBalanceDiffCompare(
        const NodeDetailPtr& left,
        const NodeDetailPtr& right) {
    return left->stoke_diff < right->stoke_diff;
}

int ElectTxItem::HandleTx(
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        zjcvm::ZjchainHost& zjc_host,
        std::unordered_map<std::string, int64_t>& acc_balance_map,
        block::protobuf::BlockTx& block_tx) {
    g2_ = std::make_shared<std::mt19937_64>(vss_mgr_->EpochRandom());
    for (int32_t storage_idx = 0; storage_idx < block_tx.storages_size(); ++storage_idx) {
        if (block_tx.storages(storage_idx).key() == protos::kShardElection) {
            uint64_t* tmp = (uint64_t*)block_tx.storages(storage_idx).val_hash().c_str();
            pools::protobuf::ElectStatistic elect_statistic;
            if (!prefix_db_->GetStatisticedShardingHeight(
                    tmp[0],
                    tmp[1],
                    &elect_statistic)) {
                ZJC_WARN("get statistic elect statistic failed! net: %u, height: %lu",
                    tmp[0],
                    tmp[1]);
                assert(false);
                return kConsensusError;
            }

            ZJC_DEBUG("get sharding statistic sharding id: %u, tm height: %lu, info sharding: %u",
                tmp[0], tmp[1], elect_statistic.sharding_id());
            uint64_t now_elect_height = elect_mgr_->latest_height(elect_statistic.sharding_id());
            const pools::protobuf::PoolStatisticItem* statistic = nullptr;
            uint64_t max_elect_height = 0;
            for (int32_t i = 0; i < elect_statistic.statistics_size(); ++i) {
                ZJC_DEBUG("sharding: %u, get statistic elect height: %lu, now_elect_height: %lu",
                    elect_statistic.sharding_id(),
                    elect_statistic.statistics(i).elect_height(),
                    now_elect_height);
                if (elect_statistic.statistics(i).elect_height() > max_elect_height) {
                    statistic = &elect_statistic.statistics(i);
                    max_elect_height = elect_statistic.statistics(i).elect_height();
                }
            }

            auto members = elect_mgr_->GetNetworkMembersWithHeight(
                now_elect_height,
                elect_statistic.sharding_id(),
                nullptr,
                nullptr);
            if (members == nullptr) {
                ZJC_WARN("get members failed, elect height: %lu, net: %u",
                    now_elect_height, elect_statistic.sharding_id());
                assert(false);
                return kConsensusError;
            }

            elect_members_ = members;
            for (auto iter = members->begin(); iter != members->end(); ++iter) {
                added_nodes_.insert((*iter)->pubkey);
            }

            pools::protobuf::PoolStatisticItem tmp_statistic;
            if (max_elect_height != now_elect_height) {
                ZJC_WARN("old elect coming max_elect_height: %lu, now_elect_height: %lu",
                    max_elect_height, now_elect_height);
                assert(false);
                return kConsensusError;
            }

            if (members->size() != statistic->tx_count_size() ||
                    members->size() != statistic->stokes_size() ||
                    members->size() != statistic->area_point_size()) {
                ZJC_DEBUG("now_elect_height: %lu, member size error: %u, %u, %u, %u",
                    now_elect_height, members->size(), statistic->tx_count_size(),
                    statistic->stokes_size(), statistic->area_point_size());
                assert(false);
                return kConsensusError;
            }

            {
                std::string ids;
                for (uint32_t i = 0; i < statistic->tx_count_size(); ++i) {
                    ids += common::Encode::HexEncode(sec_ptr_->GetAddress((*members)[i]->pubkey)) +
                        ":" + std::to_string(statistic->area_point(i).x()) +
                        ":" + std::to_string(statistic->area_point(i).y()) +
                        ":" + std::to_string(statistic->tx_count(i)) +
                        ":" + std::to_string(statistic->stokes(i)) + ",";
                }

                ZJC_DEBUG("elect info: %s", ids.c_str());
            }

            uint32_t min_area_weight = common::kInvalidUint32;
            uint32_t min_tx_count = common::kInvalidUint32;
            std::vector<NodeDetailPtr> elect_nodes(members->size(), nullptr);
            {
                std::string ids;
                for (uint32_t i = 0; i < members->size(); ++i) {
                    ids += common::Encode::HexEncode((*members)[i]->pubkey) + ",";
                }

                ZJC_DEBUG("init: %s", ids.c_str());
            }
            int res = CheckWeedout(
                thread_idx,
                members,
                *statistic,
                &min_area_weight,
                &min_tx_count,
                elect_nodes);
            if (res != kConsensusSuccess) {
                assert(false);
                return res;
            }
           
            uint64_t gas_for_root = 0llu;
            MiningToken(
                thread_idx,
                elect_statistic.sharding_id(),
                elect_nodes,
                elect_statistic.gas_amount(),
                &gas_for_root);
            min_area_weight += 1;
            min_tx_count += 1;
            uint32_t join_count = members->size() - elect_nodes.size();
            if (members->size() < common::kEachShardMaxNodeCount) {
                join_count += members->size() * kFtsNewElectJoinRate / 100;
            }

            if (join_count + elect_nodes.size() > common::kEachShardMaxNodeCount) {
                join_count = common::kEachShardMaxNodeCount - elect_nodes.size();
            }

            for (uint32_t i = 0; i < join_count; ++i) {
                elect_nodes.push_back(nullptr);
            }

            std::vector<NodeDetailPtr> src_elect_nodes_to_choose;
            for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                if (elect_nodes[i] != nullptr) {
                    src_elect_nodes_to_choose.push_back(elect_nodes[i]);
                }
            }

            ChooseNodeForEachIndex(
                true,
                thread_idx,
                min_area_weight,
                min_tx_count,
                elect_statistic,
                elect_nodes);
            ChooseNodeForEachIndex(
                false,
                thread_idx,
                min_area_weight,
                min_tx_count,
                elect_statistic,
                elect_nodes);
            {
                std::string ids;
                for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                    if (elect_nodes[i] == nullptr) {
                        continue;
                    }

                    ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ",";
                }

                ZJC_DEBUG("join elect: %s", ids.c_str());
            }
            std::string random_str;
            auto& g2 = *g2_;
            auto RandFunc = [&g2, &random_str](int idx) -> int {
                int val = abs(static_cast<int>(g2())) % idx;
                random_str += std::to_string(val) + ",";
                return val;
            };

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
                elect_statistic.sharding_id(), vss_mgr_->EpochRandom(), expect_leader_count, elect_nodes.size(), leader_nodes.size(), random_str.c_str(), *leader_nodes.begin());
            if (leader_nodes.size() != expect_leader_count) {
                ZJC_ERROR("choose leader failed: %u", elect_statistic.sharding_id());
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
                for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                    if (elect_nodes[i] == nullptr) {
                        continue;
                    }

                    ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ",";
                }

                ZJC_DEBUG("before CreateNewElect: %s", ids.c_str());
            }

            CreateNewElect(
                thread_idx,
                block,
                elect_nodes,
                elect_statistic,
                gas_for_root,
                db_batch,
                block_tx);

            {
                std::string ids;
                for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
                    if (elect_nodes[i] == nullptr) {
                        continue;
                    }

                    ids += common::Encode::HexEncode(elect_nodes[i]->pubkey) + ",";
                }

                ZJC_DEBUG("after CreateNewElect: %s", ids.c_str());
            }
            ZJC_DEBUG("consensus elect tx success: %u", elect_statistic.sharding_id());
            return kConsensusSuccess;
        }
    }

    ZJC_DEBUG("consensus elect tx error.");
    return kConsensusError;
}

void ElectTxItem::ChooseNodeForEachIndex(
        bool hold_pos,
        uint8_t thread_idx,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        const pools::protobuf::ElectStatistic& elect_statistic,
        std::vector<NodeDetailPtr>& elect_nodes) {
    for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
        if (elect_nodes[i] != nullptr) {
            continue;
        }

        std::vector<NodeDetailPtr> elect_nodes_to_choose;
        if (hold_pos) {
            GetIndexNodes(
                i,
                thread_idx,
                min_area_weight,
                min_tx_count,
                elect_statistic,
                &elect_nodes_to_choose);
        } else {
            GetIndexNodes(
                common::kInvalidUint32,
                thread_idx,
                min_area_weight,
                min_tx_count,
                elect_statistic,
                &elect_nodes_to_choose);
        }

        if (elect_nodes_to_choose.empty()) {
            continue;
        }

        auto res = GetJoinElectNodesCredit(
            i,
            elect_statistic,
            thread_idx,
            min_area_weight,
            min_tx_count,
            elect_nodes_to_choose,
            elect_nodes);
        if (res != kConsensusSuccess) {
            assert(false);
            return;
        }

        if (elect_nodes[i] != nullptr) {
            added_nodes_.insert(elect_nodes[i]->pubkey);
        }
    }
}

void ElectTxItem::GetIndexNodes(
        uint32_t index,
        uint8_t thread_idx,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        const pools::protobuf::ElectStatistic& elect_statistic,
        std::vector<NodeDetailPtr>* elect_nodes_to_choose) {
    for (int32_t i = 0; i < elect_statistic.join_elect_nodes_size(); ++i) {
        ZJC_DEBUG("join new node: %s, des shard: %u, statistic shrad: %u",
            common::Encode::HexEncode(elect_statistic.join_elect_nodes(i).pubkey()).c_str(),
            elect_statistic.join_elect_nodes(i).shard(),
            elect_statistic.sharding_id());
        auto iter = added_nodes_.find(elect_statistic.join_elect_nodes(i).pubkey());
        if (iter != added_nodes_.end()) {
            continue;
        }

        if (elect_statistic.join_elect_nodes(i).shard() != elect_statistic.sharding_id()) {
            continue;
        }

        if (index != common::kInvalidUint32) {
            if (elect_statistic.join_elect_nodes(i).elect_pos() != index) {
                continue;
            }
        }

        auto id = sec_ptr_->GetAddress(elect_statistic.join_elect_nodes(i).pubkey());
        auto account_info = account_mgr_->GetAccountInfo(
            thread_idx,
            id);
        if (account_info == nullptr) {
            assert(false);
            return;
        }

        auto node_info = std::make_shared<ElectNodeInfo>();
        node_info->area_weight = min_area_weight;
        node_info->stoke = elect_statistic.join_elect_nodes(i).stoke();
        node_info->tx_count = min_tx_count;
        node_info->credit = account_info->credit();
        node_info->pubkey = elect_statistic.join_elect_nodes(i).pubkey();
        node_info->index = index;
        elect_nodes_to_choose->push_back(node_info);
    }
}

void ElectTxItem::MiningToken(
        uint8_t thread_idx,
        uint32_t statistic_sharding_id,
        std::vector<NodeDetailPtr>& elect_nodes,
        uint64_t all_gas_amount,
        uint64_t* gas_for_root) {
    uint64_t all_tx_count = 0;
    uint64_t max_tx_count = 0;
    for (int32_t i = 0; i < elect_nodes.size(); ++i) {
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
    }

    if (max_tx_count <= 0) {
        return;
    }

    uint64_t gas_for_mining = all_gas_amount - (all_gas_amount / network_count_);
    // root shard use statistic gas amount.
    if (statistic_sharding_id == network::kRootCongressNetworkId) {
        gas_for_mining = all_gas_amount;
    } else {
        *gas_for_root = all_gas_amount - gas_for_mining;
    }

    auto now_ming_count = GetMiningMaxCount(max_tx_count);
    uint64_t tmp_all_gas_amount = 0;
    if (!stop_mining_) {
        for (int32_t i = 0; i < elect_nodes.size(); ++i) {
            if (elect_nodes[i] == nullptr) {
                continue;
            }

            auto id = sec_ptr_->GetAddress(elect_nodes[i]->pubkey);
            auto account_info = account_mgr_->GetAccountInfo(thread_idx, id);
            if (account_info == nullptr) {
                ZJC_DEBUG("get account info failed: %s",
                    common::Encode::HexEncode(id).c_str());
                assert(false);
                continue;
            }

            auto tx_count = elect_nodes[i]->tx_count;
            if (tx_count == 0) {
                tx_count = 1;
            }

            auto mining_token = now_ming_count * tx_count / max_tx_count;
            elect_nodes[i]->mining_token = mining_token;
            auto gas_token = tx_count * gas_for_mining / all_tx_count;
            if (i == elect_nodes.size() - 1) {
                assert(gas_for_mining >= tmp_all_gas_amount);
                gas_token = gas_for_mining - tmp_all_gas_amount;
            }

            elect_nodes[i]->mining_token += gas_token;
            tmp_all_gas_amount += gas_token;
            ZJC_DEBUG("elect mining %s, mining: %lu, gas mining: %lu, all gas: %lu, src: %lu",
                common::Encode::HexEncode(id).c_str(),
                mining_token, gas_token, tmp_all_gas_amount, gas_for_mining);
        }
    }

    assert(tmp_all_gas_amount == gas_for_mining);
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
        uint8_t thread_idx,
        const block::protobuf::Block& block,
        const std::vector<NodeDetailPtr>& elect_nodes,
        const pools::protobuf::ElectStatistic& elect_statistic,
        uint64_t gas_for_root,
        std::shared_ptr<db::DbWriteBatch>& db_batch,
        block::protobuf::BlockTx& block_tx) {
    elect::protobuf::ElectBlock elect_block;
    for (uint32_t i = 0; i < elect_nodes.size(); ++i) {
        if (elect_nodes[i] == nullptr) {
            if (i >= elect_members_->size()) {
                break;
            }

            auto in = elect_block.add_in();
            in->set_pubkey(elect_members_[i]->pubkey);
            in->set_pool_idx_mod_num(-1);
            in->set_mining_amount(0);
        } else {
            auto in = elect_block.add_in();
            in->set_pubkey(elect_nodes[i]->pubkey);
            in->set_pool_idx_mod_num(elect_nodes[i]->leader_mod_index);
            in->set_mining_amount(elect_nodes[i]->mining_token);
        }
    }

    elect_block.set_gas_for_root(gas_for_root);
    elect_block.set_shard_network_id(elect_statistic.sharding_id());
    elect_block.set_elect_height(block.height());
    elect_block.set_all_gas_amount(elect_statistic.gas_amount());
    if (bls_mgr_->AddBlsConsensusInfo(elect_block) != bls::kBlsSuccess) {
        ZJC_WARN("add prev elect bls consensus info failed sharding id: %u",
            elect_statistic.sharding_id());
    } else {
        ZJC_DEBUG("success add bls consensus info: %u, %lu",
            elect_statistic.sharding_id(),
            elect_block.prev_members().prev_elect_height());
    }

    std::string val = elect_block.SerializeAsString();
    std::string val_hash = protos::GetElectBlockHash(elect_block);
    auto& storage = *block_tx.add_storages();
    storage.set_key(protos::kElectNodeAttrElectBlock);
    storage.set_val_hash(val_hash);
    prefix_db_->SaveTemporaryKv(val_hash, val);
    ZJC_DEBUG("create elect success: %u", elect_statistic.sharding_id());
    return kConsensusSuccess;
}

int ElectTxItem::CheckWeedout(
        uint8_t thread_idx,
        common::MembersPtr& members,
        const pools::protobuf::PoolStatisticItem& statistic_item,
        uint32_t* min_area_weight,
        uint32_t* min_tx_count,
        std::vector<NodeDetailPtr>& elect_nodes) {
    uint32_t weed_out_count = statistic_item.tx_count_size() * kFtsWeedoutDividRate / 100;
    uint32_t direct_weed_out_count = weed_out_count / 2;
    uint32_t max_tx_count = 0;
    typedef std::pair<uint32_t, uint32_t> TxItem;
    std::vector<TxItem> member_tx_count;
    for (int32_t member_idx = 0; member_idx < statistic_item.tx_count_size(); ++member_idx) {
        if (statistic_item.tx_count(member_idx) > max_tx_count) {
            max_tx_count = statistic_item.tx_count(member_idx);
        }

        member_tx_count.push_back(std::make_pair(
            member_idx,
            statistic_item.tx_count(member_idx)));
    }

    uint32_t direct_weedout_tx_count = max_tx_count / 2;
    std::stable_sort(
        member_tx_count.begin(),
        member_tx_count.end(), [](const TxItem& l, const TxItem& r) {
        return l.second > r.second; });
    std::set<uint32_t> invalid_nodes;
    for (int32_t i = 0; i < direct_weed_out_count; ++i) {
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

        uint32_t min_dis = common::kInvalidUint32;
        for (int32_t idx = 0; idx < statistic_item.tx_count_size(); ++idx) {
            if (member_idx == idx) {
                continue;
            }

            auto& point0 = statistic_item.area_point(member_idx);
            auto& point1 = statistic_item.area_point(idx);
            uint32_t dis = (point0.x() - point1.x()) * (point0.x() - point1.x()) +
                (point0.y() - point1.y()) * (point0.y() - point1.y());
            if (min_dis > dis) {
                min_dis = dis;
            }
        }

        auto account_info = account_mgr_->GetAccountInfo(thread_idx, (*members)[member_idx]->id);
        if (account_info == nullptr) {
            ZJC_ERROR("get account info failed: %s",
                common::Encode::HexEncode((*members)[member_idx]->id).c_str());
            assert(false);
            return kConsensusError;
        }

        auto node_info = std::make_shared<ElectNodeInfo>();
        node_info->area_weight = min_dis;
        node_info->tx_count = statistic_item.tx_count(member_idx);
        node_info->stoke = statistic_item.stokes(member_idx);
        node_info->credit = account_info->credit();
        node_info->index = member_idx;
        node_info->pubkey = (*members)[member_idx]->pubkey;
        if (*min_tx_count > node_info->tx_count) {
            *min_tx_count = node_info->tx_count;
        }

        if (*min_area_weight > min_dis) {
            *min_area_weight = min_dis;
        }

        elect_nodes_to_choose.push_back(node_info);
    }

    if (elect_nodes_to_choose.empty()) {
        ZJC_WARN("elect sharding nodes empty.");
        return kConsensusError;
    }

    {
        std::string ids;
        for (uint32_t i = 0; i < elect_nodes_to_choose.size(); ++i) {
            ids += common::Encode::HexEncode(elect_nodes_to_choose[i]->pubkey) + ",";
        }

        ZJC_DEBUG("before weedout: %s, weed_out_count: %u", ids.c_str(), weed_out_count);
    }
    std::set<uint32_t> weedout_nodes;
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

    return kConsensusSuccess;
}

int ElectTxItem::GetJoinElectNodesCredit(
        uint32_t index,
        const pools::protobuf::ElectStatistic& elect_statistic,
        uint8_t thread_idx,
        uint32_t min_area_weight,
        uint32_t min_tx_count,
        const std::vector<NodeDetailPtr>& elect_nodes_to_choose,
        std::vector<NodeDetailPtr>& elect_nodes) {
    if (elect_nodes_to_choose.empty()) {
        return kConsensusSuccess;
    }

    std::set<uint32_t> weedout_nodes;
    FtsGetNodes(elect_nodes_to_choose, false, 1, weedout_nodes);
    for (auto iter = elect_nodes_to_choose.begin(); iter != elect_nodes_to_choose.end(); ++iter) {
        if (weedout_nodes.find((*iter)->index) != weedout_nodes.end()) {
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
        std::vector<NodeDetailPtr>& elect_nodes,
        bool weed_out,
        uint32_t count,
        std::set<uint32_t>& res_nodes) {
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
        auto& g2 = *g2_;
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
        std::vector<NodeDetailPtr>& elect_nodes,
        uint64_t* max_fts_val) {
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
        auto& g2 = *g2_;
        for (uint32_t i = 1; i < elect_nodes.size(); ++i) {
            uint64_t fts_val_diff = elect_nodes[i]->stoke - elect_nodes[i - 1]->stoke;
            if (fts_val_diff == 0) {
                blance_weight[i] = blance_weight[i - 1];
            }
            else {
                if (fts_val_diff < diff_2b3) {
                    auto rand_val = fts_val_diff + g2() % (diff_2b3 - fts_val_diff);
                    blance_weight[i] = blance_weight[i - 1] + (20 * rand_val) / diff_2b3;
                }
                else {
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
        elect_nodes[i]->fts_value = (
            2 * ip_weight[i] +
            4 * credit_weight[i] +
            2 * blance_weight[i] +
            2 * epoch_weight[i]) / 10;
        fts_val_str += std::to_string(ip_weight[i]) + "," +
            std::to_string(credit_weight[i]) + "," +
            std::to_string(blance_weight[i]) + "," +
            std::to_string(epoch_weight[i]) + "," +
            std::to_string(elect_nodes[i]->fts_value) + " --- ";
        if (*max_fts_val < elect_nodes[i]->fts_value) {
            *max_fts_val = elect_nodes[i]->fts_value;
        }
    }

    ZJC_DEBUG("fts value final: %s", fts_val_str.c_str());
}

};  // namespace consensus

};  // namespace zjchain
