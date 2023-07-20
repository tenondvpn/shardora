#pragma once

#include <evmc/evmc.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>

#include "block/account_manager.h"
#include "bls/bls_manager.h"
#include "common/bitmap.h"
#include "common/node_members.h"
#include "consensus/consensus_utils.h"
#include "consensus/zbft/waiting_txs.h"
#include "consensus/zbft/zbft_utils.h"
#include "pools/tx_pool_manager.h"
#include "protos/zbft.pb.h"
#include "protos/block.pb.h"
#include "protos/pools.pb.h"
#include "security/ecdsa/public_key.h"
#include "timeblock/time_block_manager.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

namespace zjchain {

namespace consensus {

class WaitingTxsPools;
class Zbft : public std::enable_shared_from_this<Zbft> {
public:
    Zbft(
        std::shared_ptr<block::AccountManager>& account_mgr,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<bls::BlsManager>& bls_mgr,
        std::shared_ptr<WaitingTxsItem>& tx_ptr,
        std::shared_ptr<consensus::WaitingTxsPools>& pools_mgr,
        std::shared_ptr<timeblock::TimeBlockManager>& tm_block_mgr);
    virtual ~Zbft();
    virtual void DoTransactionAndCreateTxBlock(block::protobuf::Block& zjc_block);
    int Init(
        int32_t leader_idx,
        int32_t leader_count,
        uint64_t elect_height,
        const common::MembersPtr& members_ptr,
        const libff::alt_bn128_G2& common_pk,
        const libff::alt_bn128_Fr& local_sec_key);
    int Prepare(bool leader, zbft::protobuf::ZbftMessage* bft_msg);
    int LeaderCreatePrepare(zbft::protobuf::ZbftMessage* bft_msg);
    int BackupCheckPrepare(
        zbft::protobuf::ZbftMessage* bft_msg,
        int32_t* invalid_tx_idx);
    int DoTransaction(zbft::protobuf::TxBft& ltx_msg);
    int LeaderCallTransaction(zbft::protobuf::ZbftMessage* bft_msg);
    int LeaderPrecommitOk(
        const zbft::protobuf::TxBft& tx_prepare,
        uint32_t index,
        const libff::alt_bn128_G1& backup_sign,
        const std::string& id);
    int LeaderCommitOk(
        uint32_t index,
        const libff::alt_bn128_G1& backup_sign,
        const std::string& id);
    bool BackupCheckLeaderValid(const zbft::protobuf::ZbftMessage* bft_msg);
    void Destroy();
    void AfterNetwork();
    void LeaderResetPrepareBitmap(const std::string& prepare_hash);
    void LeaderResetPrepareBitmap(std::shared_ptr<LeaderPrepareItem>& prepare_item);

    uint32_t pool_index() const {
        return txs_ptr_->pool_index;
    }

    void set_gid(const std::string& gid) {
        gid_ = gid;
    }

    const std::string& gid() {
        return gid_;
    }

    void set_network_id(uint32_t net_id) {
        network_id_ = net_id;
    }

    uint32_t network_id() {
        return network_id_;
    }

    void set_randm_num(uint64_t rnum) {
        rand_num_ = rnum;
    }

    uint64_t rand_num() {
        return rand_num_;
    }

    uint32_t min_aggree_member_count() {
        return min_aggree_member_count_;
    }

    uint32_t min_oppose_member_count() {
        return min_oppose_member_count_;
    }

    uint32_t commit_aggree_count() {
        return commit_aggree_set_.size();
    }

    void set_member_count(uint32_t mem_cnt) {
        member_count_ = mem_cnt;
        min_aggree_member_count_ = member_count_ * 2 / 3;
        if ((member_count_ * 2) % 3 > 0) {
            min_aggree_member_count_ += 1;
        }

        min_oppose_member_count_ = member_count_ / 3;
//         if (member_count_ % 3 > 0) {
            min_oppose_member_count_ += 1;
//         }

//         ZJC_DEBUG("consensus member count: %d/%d, oppose count: %d",
//             min_aggree_member_count_, member_count_, min_oppose_member_count_);
    }

    void set_precommit_bitmap(const std::vector<uint64_t>& bitmap_data) {
        assert(bitmap_data.size() == common::kEachShardMaxNodeCount / 64);
        precommit_bitmap_ = common::Bitmap(bitmap_data);
    }

    const common::Bitmap& precommit_bitmap() const {
        return precommit_bitmap_;
    }

    void set_consensus_status(uint32_t status) {
        consensus_status_ = status;
    }

    uint32_t consensus_status() {
        return consensus_status_;
    }

    uint32_t member_count() {
        return member_count_;
    }

    uint32_t min_agree_member_count() {
        return min_aggree_member_count_;
    }

    const std::string& local_prepare_hash() const {
        return prepare_block_->hash();
    }

    void set_prepare_hash(const std::string& prepare_hash) {
        prepare_hash_ = prepare_hash;
        bls_mgr_->GetLibffHash(prepare_hash, &g1_prepare_hash_);
    }

    const std::string& prepare_hash() const {
        return prepare_hash_;
    }

    void set_precoimmit_hash() {
        if (prepare_block_ == nullptr) {
            return;
        }

        prepare_block_->set_is_commited_block(true);
        auto precommit_hash = GetBlockHash(*prepare_block_);
        prepare_block_->set_hash(precommit_hash);
        bls_mgr_->GetLibffHash(precommit_hash, &g1_precommit_hash_);
        ZJC_DEBUG("reset block hash: %s", common::Encode::HexEncode(precommit_hash).c_str());
    }

    uint32_t leader_index() const {
        return leader_index_;
    }

    bool set_bls_precommit_agg_sign(const libff::alt_bn128_G1& agg_sign, const std::string& sign_hash);
    bool verify_bls_precommit_agg_sign(const libff::alt_bn128_G1& agg_sign, const std::string& sign_hash);

    const std::shared_ptr<libff::alt_bn128_G1>& bls_precommit_agg_sign() const {
        assert(bls_precommit_agg_sign_ != nullptr);
        return bls_precommit_agg_sign_;
    }

    bool set_bls_commit_agg_sign(const libff::alt_bn128_G1& agg_sign);

    const std::shared_ptr<libff::alt_bn128_G1>& bls_commit_agg_sign() const {
        assert(bls_commit_agg_sign_ != nullptr);
        return bls_commit_agg_sign_;
    }

    std::shared_ptr<block::protobuf::Block>& prepare_block() {
        return prepare_block_;
    }

    void set_prepare_block(std::shared_ptr<block::protobuf::Block> prepare_block) {
        prepare_block_ = prepare_block;
        if (prepare_block_ != nullptr) {
            auto& precommit_hash = prepare_block_->hash();
            ZJC_DEBUG("set block hash: %s, gid: %s",
                common::Encode::HexEncode(precommit_hash).c_str(),
                common::Encode::HexEncode(gid()).c_str());
            bls_mgr_->GetLibffHash(precommit_hash, &g1_precommit_hash_);
            CreateCommitVerifyHash();
            ZJC_DEBUG("reset block hash: %s", common::Encode::HexEncode(precommit_hash).c_str());
        }
    }

    bool aggree() {
        return aggree_;
    }

    bool is_commited_block() {
        if (prepare_block_ == nullptr) {
            return false;
        }

        return prepare_block_->is_commited_block();
    }

    void not_aggree() {
        aggree_ = false;
    }

    void AddBftEpoch() {
        ++bft_epoch_;
    }

    uint32_t GetEpoch() {
        return bft_epoch_;
    }

    void SetEpoch(uint32_t epoch) {
        bft_epoch_ = epoch;
    }

    common::BftMemberPtr& leader_mem_ptr() {
        return leader_mem_ptr_;
    }

    common::MembersPtr& members_ptr() {
        return members_ptr_;
    }

    void add_prepair_failed_node_index(uint32_t index) {
        prepare_enc_failed_nodes_.insert(index);
    }

    uint64_t elect_height() {
        return elect_height_;
    }

    bool this_node_is_leader() {
        return this_node_is_leader_;
    }

    uint32_t add_prepare_verify_failed_count() {
        return ++prepare_verify_failed_count_;
    }

    const libff::alt_bn128_Fr& local_sec_key() const {
        return local_sec_key_;
    }

    uint32_t local_member_index() {
        return local_member_index_;
    }

    int AddPrepareOpposeNode(const std::string& id) {
        if (leader_handled_precommit_) {
            return kConsensusHandled;
        }

        precommit_oppose_set_.insert(id);
        if (precommit_oppose_set_.size() >= min_oppose_member_count_) {
            leader_handled_precommit_ = true;
            return kConsensusOppose;
        }

        return kConsensusWaitingBackup;
    }

    int AddPrecommitOpposeNode(const std::string& id) {
        if (leader_handled_commit_) {
            return kConsensusHandled;
        }

        commit_oppose_set_.insert(id);
        if (commit_oppose_set_.size() >= min_oppose_member_count_) {
            leader_handled_commit_ = true;
            ZJC_DEBUG("gid precommit oppose: %s", common::Encode::HexEncode(gid()).c_str());
            return kConsensusOppose;
        }

        return kConsensusWaitingBackup;
    }

    int32_t AddVssRandomOppose(uint32_t index, uint64_t random) {
        auto iter = vss_random_map_.find(random);
        if (iter == vss_random_map_.end()) {
            vss_random_map_[random] = std::set<uint32_t>();
            vss_random_map_[random].insert(index);
            return 1;
        } else {
            iter->second.insert(index);
            return iter->second.size();
        }
    }

    int32_t handle_last_error_code() {
        return handle_last_error_code_;
    }

    std::string handle_last_error_msg() {
        return handle_last_error_msg_;
    }

    void SetHandlerError(int32_t error_code, const std::string& error_msg) {
        handle_last_error_code_ = error_code;
        handle_last_error_msg_ = error_msg;
    }

    bool PrepareHashNotConsensus() {
        if (consensus_prepare_all_count_  > min_oppose_member_count_ + consensus_prepare_max_count_) {
            return true;
        }

        return false;
    }

    int32_t SetPrepareBlock(
            const std::string& id,
            uint32_t index,
            const std::string& prepare_hash,
            uint64_t height,
            const libff::alt_bn128_G1& sign) {
        auto iter = prepare_block_map_.find(prepare_hash);
        if (iter == prepare_block_map_.end()) {
            auto item = std::make_shared<LeaderPrepareItem>();
            item->height = height;
            item->precommit_aggree_set_.insert(id);
            item->prepare_bitmap_.Set(index);
            item->backup_precommit_signs_[index] = sign;
            item->height_count_map[height] = 1;
            prepare_block_map_[prepare_hash] = item;
            if (consensus_prepare_max_count_ == 0) {
                consensus_prepare_max_count_ = 1;
            }

            ++consensus_prepare_all_count_;
            return 1;
        } else {
            iter->second->precommit_aggree_set_.insert(id);
            iter->second->prepare_bitmap_.Set(index);
            iter->second->backup_precommit_signs_[index] = sign;
            auto hiter = iter->second->height_count_map.find(height);
            if (hiter == iter->second->height_count_map.end()) {
                iter->second->height_count_map[height] = 1;
            } else {
                ++hiter->second;
            }

            if (iter->second->precommit_aggree_set_.size() > consensus_prepare_max_count_) {
                consensus_prepare_max_count_ = iter->second->precommit_aggree_set_.size();
            }

            ++consensus_prepare_all_count_;
            return iter->second->precommit_aggree_set_.size();
        }
    }

    int LeaderPrecommitAggSign(const std::string& hash);
    int LeaderCreateCommitAggSign();
    void RechallengePrecommitClear();
    uint8_t thread_index() const {
        return txs_ptr_->thread_index;
    }

    const std::string& precommit_bls_agg_verify_hash() const {
        return precommit_bls_agg_verify_hash_;
    }

    const std::string& commit_bls_agg_verify_hash() const{
        return commit_bls_agg_verify_hash_;
    }

    const libff::alt_bn128_G1& g1_prepare_hash() const {
        return g1_prepare_hash_;
    }

    const libff::alt_bn128_G1& g1_precommit_hash() const {
        return g1_precommit_hash_;
    }

    void CreatePrecommitVerifyHash();
    void CreateCommitVerifyHash();
    std::shared_ptr<WaitingTxsItem>& txs_ptr() {
        return txs_ptr_;
    }

    bool is_synced_block() const {
        return is_synced_block_;
    }

    const std::vector<uint32_t>& valid_index() const {
        return valid_index_;
    }

    uint64_t height() const {
        if (prepare_block_ == nullptr) {
            return common::kInvalidUint64;
        }

        return prepare_block_->height();
    }

    void reset_timeout() {
        timeout_ = common::TimeUtils::TimestampUs();
    }

    bool timeout(uint64_t now_tm) const {
        if (timeout_ + kBftTimeout < now_tm) {
            return true;
        }

        return false;
    }

    bool precommited_timeout(uint64_t now_tm) const {
        if (timeout_ + kRemovePrecommitedBftTimeUs < now_tm) {
            return true;
        }

        return false;
    }

    void set_leader_pre_height(uint64_t height) {
        leader_pre_height_ = height;
    }

    void set_leader_pre_hash(const std::string& hash) {
        leader_pre_hash_ = hash;
    }

    std::shared_ptr<db::DbWriteBatch> db_batch() {
        return db_batch_;
    }

    bool should_timer_to_restart() const {
        return should_timer_to_restart_;
    }

    void set_should_timer_to_restart(bool tag) {
        should_timer_to_restart_ = true;
    }

    int32_t pool_mod_num() const {
        return pool_index_mod_num_;
    }

    const std::string& leader_waiting_prepare_hash() const {
        return leader_waiting_prepare_hash_;
    }

    transport::MessagePtr reconsensus_msg_ptr() {
        return reconsensus_msg_ptr_;
    }

    void set_reconsensus_msg_ptr(transport::MessagePtr msg_ptr) {
        reconsensus_msg_ptr_ = msg_ptr;
    }

    transport::MessagePtr prepare_msg_ptr() {
        return prepare_msg_ptr_;
    }

    void set_prepare_msg_ptr(transport::MessagePtr msg_ptr) {
        prepare_msg_ptr_ = msg_ptr;
    }

    void ChangeLeader(uint32_t new_leader_idx, uint64_t elect_height) {
        changed_leader_new_index_ = new_leader_idx;
        changed_leader_elect_height_ = elect_height;
    }

    bool IsChangedLeader() const {
        if (changed_leader_new_index_ == common::kInvalidUint32 ||
                local_member_index_ == common::kInvalidUint32) {
            return false;
        }
        return changed_leader_new_index_ == local_member_index_;
    }

    uint64_t changed_leader_elect_height() const {
        return changed_leader_elect_height_;
    }

    uint32_t changed_leader_new_index() const {
        return changed_leader_new_index_;
    }

protected:
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    common::MembersPtr members_ptr_{ nullptr };
    std::string gid_;
    uint32_t network_id_{ 0 };
    uint32_t leader_index_{ 0 };
    uint64_t rand_num_{ 0 };
    bool leader_handled_precommit_{ false };
    bool leader_handled_commit_{ false };
    uint32_t member_count_{ 0 };
    uint32_t min_aggree_member_count_{ 0 };
    uint32_t min_oppose_member_count_{ 0 };
    common::Bitmap precommit_bitmap_{ common::kEachShardMaxNodeCount };
    uint32_t consensus_status_{ kConsensusInit };
    std::shared_ptr<block::protobuf::Block> prepare_block_{ nullptr };
    std::unordered_set<std::string> precommit_oppose_set_;
    std::unordered_set<std::string> commit_aggree_set_;
    std::unordered_set<std::string> commit_oppose_set_;
    bool aggree_{ true };
    uint32_t bft_epoch_{ 0 };
    common::BftMemberPtr leader_mem_ptr_{ nullptr };
    std::set<uint32_t> prepare_enc_failed_nodes_;
    bool this_node_is_leader_{ false };
    uint64_t elect_height_{ 0 };
    libff::alt_bn128_G1 backup_commit_signs_[common::kEachShardMaxNodeCount] = { libff::alt_bn128_G1::zero() };
    std::shared_ptr<libff::alt_bn128_G1> bls_precommit_agg_sign_{ nullptr };
    std::shared_ptr<libff::alt_bn128_G1> bls_commit_agg_sign_{ nullptr };
    uint32_t prepare_verify_failed_count_{ 0 };
    libff::alt_bn128_Fr local_sec_key_{ libff::alt_bn128_Fr::zero() };
    libff::alt_bn128_G2 common_pk_{ libff::alt_bn128_G2::zero() };
    uint32_t local_member_index_ = common::kInvalidUint32;
    std::map<uint64_t, std::set<uint32_t>> vss_random_map_;
    int32_t handle_last_error_code_{ 0 };
    std::string handle_last_error_msg_;
    std::unordered_map<std::string, std::shared_ptr<LeaderPrepareItem>> prepare_block_map_;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<WaitingTxsItem> txs_ptr_ = nullptr;
    std::shared_ptr<consensus::WaitingTxsPools> pools_mgr_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<db::DbWriteBatch> db_batch_ = nullptr;
    std::string precommit_bls_agg_verify_hash_;
    std::string commit_bls_agg_verify_hash_;
    libff::alt_bn128_G1 g1_prepare_hash_;
    libff::alt_bn128_G1 g1_precommit_hash_;
    bool is_synced_block_ = false;
    std::vector<uint32_t> valid_index_;
    uint32_t consensus_prepare_max_count_ = 0;
    uint32_t consensus_prepare_all_count_ = 0;
    uint64_t timeout_ = 0;
    uint64_t leader_pre_height_ = 0;
    std::string leader_pre_hash_;
    zjcvm::ZjchainHost zjc_host;
    bool should_timer_to_restart_ = false;
    int32_t pool_index_mod_num_ = -1;
    std::string leader_waiting_prepare_hash_;
    transport::MessagePtr reconsensus_msg_ptr_ = nullptr;
    transport::MessagePtr prepare_msg_ptr_ = nullptr;
    uint32_t changed_leader_new_index_ = common::kInvalidUint32;
    uint64_t changed_leader_elect_height_ = common::kInvalidUint64;
    std::string prepare_hash_;

    DISALLOW_COPY_AND_ASSIGN(Zbft);
public:
    // for test
    uint64_t times_[128] = { 0 };
    uint32_t times_index_ = 0;
    void ClearTime() {
        times_index_ = 0;
    }

    void PrintTime() {
        for (uint32_t i = 1; i < times_index_; ++i) {
            std::cout << i << " : " << (times_[i] - times_[i - 1]) << std::endl;
        }
    }

};

typedef std::shared_ptr<Zbft> ZbftPtr;

};  // namespace consensus

};  // namespace zjchain