#pragma once

#include "block/account_manager.h"
#include "consensus/zbft/tx_item_base.h"
#include "security/security.h"

namespace zjchain {

namespace consensus {

class ElectTxItem : public TxItemBase {
public:
    ElectTxItem(
        const transport::MessagePtr& msg,
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& sec_ptr,
        std::shared_ptr<protos::PrefixDb>& prefix_db,
        std::shared_ptr<elect::ElectManager>& elect_mgr,
        uint64_t vss_random)
        : TxItemBase(msg, account_mgr, sec_ptr),
        prefix_db_(prefix_db),
        elect_mgr_(elect_mgr),
        vss_random_(vss_random) {}
    virtual ~ElectTxItem() {}
    virtual int HandleTx(
            uint8_t thread_idx,
            const block::protobuf::Block& block,
            std::shared_ptr<db::DbWriteBatch>& db_batch,
            std::unordered_map<std::string, int64_t>& acc_balance_map,
            block::protobuf::BlockTx& block_tx) {
        for (int32_t i = 0; i < block_tx.storages_size(); ++i) {
            if (block_tx.storages(i).key() == protos::kShardStatistic) {
                pools::protobuf::ElectStatistic elect_statistic;
                std::string val;
                if (!prefix_db_->GetTemporaryKv(block_tx.storages(i).val_hash(), &val)) {
                    assert(false);
                    return kConsensusError;
                }

                if (!elect_statistic.ParseFromString(val)) {
                    assert(false);
                    return kConsensusError;
                }

                uint64_t now_elect_height = elect_mgr_->latest_height(elect_statistic.sharding_id());
                pools::protobuf::PoolStatisticItem* statistic = nullptr;
                for (int32_t i = 0; i < elect_statistic.statistics_size(); ++i) {
                    if (elect_statistic.statistics(i).elect_height() == now_elect_height) {
                        statistic = &elect_statistic.statistics(i);
                        break;
                    }
                }

                if (statistic == nullptr) {
                    return kConsensusError;
                }

                auto members = elect_mgr_->GetNetworkMembersWithHeight(
                    now_elect_height,
                    elect_statistic.sharding_id(),
                    nullptr,
                    nullptr);
                if (members == nullptr) {
                    ZJC_WARN("get members failed, elect height: %lu, net: %u",
                        latest_elect_height_, shard_netid);
                    assert(false);
                    return kConsensusError;
                }

                if (members->size() != statistic->tx_count_size() ||
                        members->size() != statistic->stokes_size() ||
                        members->size() != statistic->area_point_size()) {
                    assert(false);
                    return kConsensusError;
                }

                uint32_t min_area_weight = common::kInvalidUint32;
                uint32_t min_tx_count = common::kInvalidUint32;
                int res = CheckWeedout(members, *statistic, &min_area_weight, &min_tx_count);
                if (res != kConsensusSuccess) {
                    return res;
                }

                min_area_weight += 1;
                min_tx_count += 1;
                res = GetJoinElectNodesCredit(elect_statistic, thread_idx, min_area_weight, min_tx_count);
                if (res != kConsensusSuccess) {
                    return res;
                }
            }
        }
        return kConsensusSuccess;
    }

private:
    struct ElectNodeInfo {
        uint64_t fts_value;
        uint64_t stoke;
        uint64_t stoke_diff;
        uint64_t tx_count;
        int32_t credit;
        int32_t area_weight;
        std::string id;
    };

    typedef std::shared_ptr<ElectNodeInfo> NodeDetailPtr;

    int CheckWeedout(
            common::MembersPtr& members,
            const pools::protobuf::PoolStatisticItem& statistic_item,
            uint32_t* min_area_weight,
            uint32_t* min_tx_count) {
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
        std::sort(
            member_tx_count.begin(),
            member_tx_count.end(), [](const TxItem& l, const TxItem& r) {
            return l.second > r.second; });
        std::set<uint32_t> invalid_nodes;
        for (int32_t i = 0; i < direct_weed_out_count; ++i) {
            if (member_tx_count[i].second < direct_weedout_tx_count) {
                invalid_nodes.insert(member_tx_count[i].first);
            }
        }

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
                uint32_t dis = (point0.x - point1.x) * (point0.x - point1.x) +
                    (point0.y - point1.y) * (point0.y - point1.y);
                if (min_dis > dis) {
                    min_dis = dis;
                }
            }

            auto account_info = account_mgr_->GetAccountInfo(thread_idx, (*members)[member_idx]->id);
            if (account_info == nullptr) {
                assert(false);
                return kConsensusError;
            }

            auto node_info = std::make_shared<ElectNodeInfo>();
            node_info->area_weight = min_dis;
            node_info->tx_count = statistic_item.tx_count(member_idx);
            node_info->stoke = statistic_item.stokes(member_idx);
            node_info->credit = account_info->credit();
            node_info->id = (*members)[member_idx]->id;
            if (*min_tx_count > node_info->tx_count) {
                *min_tx_count = node_info->tx_count;
            }

            if (*min_area_weight > min_dis) {
                *min_area_weight = min_dis;
            }

            SetMinMaxInfo(node_info);
            elect_nodes_.push_back(node_info);
        }

        return kConsensusSuccess;
    }

    void SetMinMaxInfo(const ElectNodeInfo& node_info) {
        if (max_stoke_ < node_info->stoke) {
            max_stoke_ = node_info->stoke;
        }

        if (min_stoke_ > node_info->stoke) {
            min_stoke_ = node_info->stoke;
        }

        if (max_area_weight_ < node_info->area_weight) {
            max_area_weight_ = node_info->area_weight;
        }

        if (min_area_weight_ > node_info->area_weight) {
            min_area_weight_ = node_info->area_weight;
        }

        if (max_credit_ < node_info->credit) {
            max_credit_ = node_info->credit;
        }

        if (min_credit_ > node_info->credit) {
            min_credit_ = node_info->credit;
        }

        if (max_tx_count_ < node_info->tx_count) {
            max_tx_count_ = node_info->tx_count;
        }

        if (min_tx_count_ > node_info->tx_count) {
            min_tx_count_ = node_info->tx_count;
        }
    }

    int GetJoinElectNodesCredit(
            const pools::protobuf::ElectStatistic& elect_statistic,
            uint8_t thread_idx,
            uint32_t min_area_weight,
            uint32_t min_tx_count) {
        credit.resize(elect_statistic.join_elect_nodes_size());
        for (int32_t i = 0; i < elect_statistic.join_elect_nodes_size(); ++i) {
            auto account_info = account_mgr_->GetAccountInfo(
                thread_idx,
                elect_statistic.join_elect_nodes(i).id());
            if (account_info == nullptr) {
                assert(false);
                return kConsensusError;
            }

            auto node_info = std::make_shared<ElectNodeInfo>();
            node_info->area_weight = min_dis;
            node_info->stoke = elect_statistic.join_elect_nodes(i).stoke();
            node_info->tx_count = min_tx_count;
            node_info->credit = account_info->credit();
            node_info->id = account_info->addr();
            SetMinMaxInfo(node_info);
            elect_nodes_.push_back(node_info);
        }

        return kConsensusSuccess;
    }

    void FtsGetNodes(
            uint32_t shard_netid,
            bool weed_out,
            uint32_t count,
            std::set<int32_t>& res_nodes) {
        std::mt19937_64 g2(vss_random_);
        SmoothFtsValue(
            shard_netid,
            g2,
            sort_vec);
        std::set<int32_t> tmp_res_nodes;
        uint32_t try_times = 0;
        while (tmp_res_nodes.size() < count) {
            common::FtsTree fts_tree;
            int32_t idx = 0;
            for (auto iter = src_nodes.begin(); iter != src_nodes.end(); ++iter, ++idx) {
                if (tmp_res_nodes.find(idx) != tmp_res_nodes.end()) {
                    continue;
                }

                uint64_t fts_value = (*iter)->fts_value;
                if (weed_out) {
                    fts_value = common::kZjcCoinMaxAmount - fts_value;
                }

                fts_tree.AppendFtsNode(fts_value, idx);
            }

            fts_tree.CreateFtsTree();
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
    //         NodeDetailPtr node_ptr = *((NodeDetailPtr*)data);
            res_nodes.insert(data);
        }
    }

    inline bool ElectNodeBalanceCompare(const NodeDetailPtr& left, const NodeDetailPtr& right) {
        return left->stoke < right->stoke;
    }

    inline static bool ElectNodeBalanceDiffCompare(const NodeDetailPtr& left, const NodeDetailPtr& right) {
        return left->stoke_diff < right->stoke_diff;
    }

    void SmoothFtsValue(std::mt19937_64& g2) {
        std::sort(sort_vec.begin(), sort_vec.end(), ElectNodeBalanceCompare);
        sort_vec[0]->stoke = stoke_mgr_->GetAddressStoke(sort_vec[0]->id);
        ELECT_DEBUG("TTTTTTTTT smooth get blance: %s, %lu",
            common::Encode::HexEncode(sort_vec[0]->id).c_str(),
            (uint64_t)sort_vec[0]->stoke);
        for (uint32_t i = 1; i < sort_vec.size(); ++i) {
            sort_vec[i]->stoke = stoke_mgr_->GetAddressStoke(sort_vec[i]->id);
            ELECT_DEBUG("TTTTTTTTT smooth get blance: %s, %lu",
                common::Encode::HexEncode(sort_vec[i]->id).c_str(),
                (uint64_t)sort_vec[i]->stoke);
            sort_vec[i]->stoke_diff = sort_vec[i]->stoke - sort_vec[i - 1]->stoke;
        }

        std::sort(sort_vec.begin(), sort_vec.end(), ElectNodeBalanceDiffCompare);
        uint64_t diff_2b3 = sort_vec[sort_vec.size() * 2 / 3]->stoke_diff;
        std::sort(sort_vec.begin(), sort_vec.end(), ElectNodeBalanceCompare);
        std::vector<int32_t> blance_weight;
        blance_weight.resize(sort_vec.size());
        blance_weight[0] = 100;
        int32_t min_balance = (std::numeric_limits<int32_t>::max)();
        int32_t max_balance = 0;
        for (uint32_t i = 1; i < sort_vec.size(); ++i) {
            uint64_t fts_val_diff = sort_vec[i]->stoke - sort_vec[i - 1]->stoke;
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
        int32_t blance_diff = max_balance - min_balance;
        if (max_balance - min_balance < 1000) {
            auto old_balance_diff = max_balance - min_balance;
            max_balance = min_balance + 1000;
            blance_diff = max_balance - min_balance;
            if (old_balance_diff > 0) {
                int32_t balance_index = blance_diff / old_balance_diff;
                for (uint32_t i = 0; i < sort_vec.size(); ++i) {
                    blance_weight[i] = min_balance + balance_index * (blance_weight[i] - min_balance);
                }
            }
        }

        std::vector<int32_t> credit_weight;
        credit_weight.resize(sort_vec.size());
        int32_t min_credit = (std::numeric_limits<int32_t>::max)();
        int32_t max_credit = (std::numeric_limits<int32_t>::min)();
        for (uint32_t i = 0; i < sort_vec.size(); ++i) {
            int32_t credit = common::kInitNodeCredit;
            node_credit_.GetNodeHistoryCredit(sort_vec[i]->id, &credit);
            credit_weight[i] = credit;
            if (min_credit > credit) {
                min_credit = credit;
            }

            if (max_credit < credit) {
                max_credit = credit;
            }
        }

        int32_t credit_diff = max_credit - min_credit;
        if (credit_diff > 0) {
            int32_t credit_index = blance_diff / credit_diff;
            for (uint32_t i = 0; i < sort_vec.size(); ++i) {
                credit_weight[i] = min_balance + credit_index * (credit_weight[i] - min_credit);
            }
        }

        std::vector<int32_t> ip_weight;
        ip_weight.resize(sort_vec.size());
        auto choosed_ip_weight = 0;
        int32_t min_ip_weight = (std::numeric_limits<int32_t>::max)();
        int32_t max_ip_weight = (std::numeric_limits<int32_t>::min)();
        for (uint32_t i = 0; i < sort_vec.size(); ++i) {
            int32_t prefix_len = 0;
            auto count = 0;
            // choosed_ip_weight.GetIpCount(sort_vec[i]->public_ip, &prefix_len);
            ip_weight[i] = prefix_len;
            if (ip_weight[i] > max_ip_weight) {
                max_ip_weight = ip_weight[i];
            }

            if (ip_weight[i] < min_ip_weight) {
                min_ip_weight = ip_weight[i];
            }
        }

        for (uint32_t i = 0; i < sort_vec.size(); ++i) {
            ip_weight[i] = max_ip_weight - ip_weight[i];
        }

        int32_t weight_diff = max_ip_weight - min_ip_weight;
        if (weight_diff > 0) {
            int32_t weight_index = blance_diff / weight_diff;
            for (uint32_t i = 0; i < sort_vec.size(); ++i) {
                ip_weight[i] = min_balance + weight_index * (ip_weight[i] - min_ip_weight);
            }
        }

        for (uint32_t i = 0; i < sort_vec.size(); ++i) {
            sort_vec[i]->fts_value = (3 * ip_weight[i] + 4 * credit_weight[i] + 3 * blance_weight[i]) / 10;
            ELECT_DEBUG("fts smooth %s: %d, %d, %d, %d",
                common::Encode::HexEncode(sort_vec[i]->id).c_str(),
                sort_vec[i]->fts_value, ip_weight[i], credit_weight[i], blance_weight[i]);
            std::cout << common::Encode::HexEncode(sort_vec[i]->id) << " : "
                << sort_vec[i]->fts_value << ", "
                << ip_weight[i] << ", "
                << credit_weight[i] << ", "
                << blance_weight[i] << std::endl;
        }
    }

    std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
    std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
    uint64_t vss_random_ = 0;
    std::vector<std::shared_ptr<ElectNodeInfo>> elect_nodes_;
    uint64_t max_stoke_ = 0;
    uint64_t min_stoke_ = common::kInvalidUint64;
    uint32_t max_area_weight_ = 0;
    uint32_t min_area_weight_ = common::kInvalidUint32;
    uint32_t max_tx_count_ = 0;
    uint32_t min_tx_count_ = common::kInvalidUint32;
    int32_t max_credit_ = 0;
    int32_t min_credit_ = common::kInvalidInt32;

    DISALLOW_COPY_AND_ASSIGN(ElectTxItem);
};

};  // namespace consensus

};  // namespace zjchain
