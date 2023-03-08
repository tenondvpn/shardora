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
#include "elect/member_manager.h"
#include "pools/tx_pool_manager.h"
#include "protos/hotstuff.pb.h"
#include "protos/block.pb.h"
#include "protos/pools.pb.h"
#include "security/ecdsa/public_key.h"
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
        std::shared_ptr<consensus::WaitingTxsPools>& pools_mgr);
    virtual ~Zbft();
    virtual void DoTransactionAndCreateTxBlock(block::protobuf::Block& zjc_block);
    int Init(
        uint64_t elect_height,
        common::BftMemberPtr& leader_mem_ptr,
        common::MembersPtr& members_ptr,
        libff::alt_bn128_G2& common_pk,
        libff::alt_bn128_Fr& local_sec_key);
    int Prepare(bool leader, hotstuff::protobuf::ZbftMessage* bft_msg);
    int LeaderCreatePrepare(hotstuff::protobuf::ZbftMessage* bft_msg);
    int BackupCheckPrepare(
        hotstuff::protobuf::ZbftMessage* bft_msg,
        int32_t* invalid_tx_idx);
//     std::shared_ptr<hotstuff::protobuf::HotstuffLeaderPrepare> CreatePrepareTxInfo(
//         std::shared_ptr<block::protobuf::Block>& block_ptr,
//         hotstuff::protobuf::LeaderTxPrepare& ltx_prepare);
    int DoTransaction(hotstuff::protobuf::LeaderTxPrepare& ltx_msg);
    void LeaderCallTransaction(hotstuff::protobuf::ZbftMessage* bft_msg);
    int LeaderPrecommitOk(
        const hotstuff::protobuf::LeaderTxPrepare& tx_prepare,
        uint32_t index,
        const libff::alt_bn128_G1& backup_sign,
        const std::string& id);
    int LeaderCommitOk(
        uint32_t index,
        const libff::alt_bn128_G1& backup_sign,
        const std::string& id);
    int CheckTimeout();
    bool BackupCheckLeaderValid(const hotstuff::protobuf::ZbftMessage* bft_msg);
    int InitZjcTvmContext();
    void Destroy();
    void AfterNetwork();

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
        if (member_count_ % 3 > 0) {
            min_oppose_member_count_ += 1;
        }

        ZJC_DEBUG("consensus member count: %d/%d, oppose count: %d",
            min_aggree_member_count_, member_count_, min_oppose_member_count_);
    }

    void set_precommit_bitmap(const std::vector<uint64_t>& bitmap_data) {
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

    void reset_timeout() {
        timeout_ = (std::chrono::steady_clock::now() +
                std::chrono::microseconds(kBftTimeout));
    }

    uint32_t member_count() {
        return member_count_;
    }

    uint32_t min_agree_member_count() {
        return min_aggree_member_count_;
    }

    const std::string& local_prepare_hash() const {
        return prepare_hash_;
    }

    void set_prepare_hash(const std::string& prepare_hash) {
        prepare_hash_ = prepare_hash;
        bls_mgr_->GetLibffHash(prepare_hash_, &g1_prepare_hash_);
    }

    void set_precoimmit_hash(const std::string& precommit_hash) {
        precommit_hash_ = precommit_hash;
        bls_mgr_->GetLibffHash(precommit_hash_, &g1_precommit_hash_);
    }

    uint32_t leader_index() const {
        return leader_index_;
    }

    void set_prepare_bitmap(const std::vector<uint64_t>& bitmap_data) {
        prepare_bitmap_ = common::Bitmap(bitmap_data);
    }

    const common::Bitmap& prepare_bitmap() const {
        return prepare_bitmap_;
    }

    const std::string& precommit_hash() const {
        return precommit_hash_;
    }

    bool set_bls_precommit_agg_sign(const libff::alt_bn128_G1& agg_sign, const std::string& sign_hash);

    const std::shared_ptr<libff::alt_bn128_G1>& bls_precommit_agg_sign() const {
        assert(bls_precommit_agg_sign_ != nullptr);
        return bls_precommit_agg_sign_;
    }

    void set_bls_commit_agg_sign(const libff::alt_bn128_G1& agg_sign) {
        bls_commit_agg_sign_ = std::make_shared<libff::alt_bn128_G1>(agg_sign);
    }

    const std::shared_ptr<libff::alt_bn128_G1>& bls_commit_agg_sign() const {
        assert(bls_commit_agg_sign_ != nullptr);
        return bls_commit_agg_sign_;
    }

    void init_prepare_timeout() {
        prepare_timeout_ = (std::chrono::steady_clock::now() +
                std::chrono::microseconds(kBftLeaderPrepareWaitPeriod));
    }

    void init_precommit_timeout() {
        precommit_timeout_ = (std::chrono::steady_clock::now() +
                std::chrono::microseconds(kBftLeaderPrepareWaitPeriod));
    }

    std::shared_ptr<block::protobuf::Block>& prpare_block() {
        return prpare_block_;
    }

    bool aggree() {
        return aggree_;
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

    std::shared_ptr<elect::MemberManager>& mem_manager_ptr() {
        return mem_manager_ptr_;
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
        precommit_oppose_set_.insert(id);
        if (precommit_oppose_set_.size() >= min_oppose_member_count_) {
            leader_handled_precommit_ = true;
            return kConsensusOppose;
        }

        return kConsensusWaitingBackup;
    }

    int AddPrecommitOpposeNode(const std::string& id) {
        commit_oppose_set_.insert(id);
        if (commit_oppose_set_.size() >= min_oppose_member_count_) {
            leader_handled_precommit_ = true;
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

    int32_t SetPrepareBlock(
            const std::string& id,
            uint32_t index,
            const std::string& prepare_hash,
            uint64_t height,
            const libff::alt_bn128_G1& sign) {
        auto iter = prepare_block_map_.find(prepare_hash);
        if (iter == prepare_block_map_.end()) {
            auto item = std::make_shared<LeaderPrepareItem>();
            item->backup_sign.push_back(sign);
            item->height = height;
            item->precommit_aggree_set_.insert(id);
            item->prepare_bitmap_.Set(index);
            item->backup_precommit_signs_[index] = sign;
            item->height_count_map[height] = 1;
            prepare_block_map_[prepare_hash] = item;
            return 1;
        } else {
            iter->second->backup_sign.push_back(sign);
            iter->second->precommit_aggree_set_.insert(id);
            iter->second->prepare_bitmap_.Set(index);
            iter->second->backup_precommit_signs_[index] = sign;
            auto hiter = iter->second->height_count_map.find(height);
            if (hiter == iter->second->height_count_map.end()) {
                iter->second->height_count_map[height] = 1;
            } else {
                ++hiter->second;
            }

            return iter->second->backup_sign.size();
        }
    }

    int LeaderCreatePreCommitAggChallenge(const std::string& hash);
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

    void set_prev_bft_ptr(std::shared_ptr<Zbft> zbft_ptr) {
        pipeline_prev_zbft_ptr_ = zbft_ptr;
    }

    const std::shared_ptr<Zbft>& pipeline_prev_zbft_ptr() const {
        return pipeline_prev_zbft_ptr_;
    }

protected:
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<security::Security> security_ptr_ = nullptr;
    common::MembersPtr members_ptr_{ nullptr };
    std::shared_ptr<elect::MemberManager> mem_manager_ptr_{ nullptr };
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
    std::chrono::steady_clock::time_point timeout_;
    std::string prepare_hash_;
    std::chrono::steady_clock::time_point prepare_timeout_;
    std::chrono::steady_clock::time_point precommit_timeout_;
    std::shared_ptr<block::protobuf::Block> prpare_block_{ nullptr };
    std::unordered_set<std::string> precommit_oppose_set_;
    std::unordered_set<std::string> commit_aggree_set_;
    std::unordered_set<std::string> commit_oppose_set_;
    bool aggree_{ true };
    uint32_t bft_epoch_{ 0 };
    common::BftMemberPtr leader_mem_ptr_{ nullptr };
    std::set<uint32_t> prepare_enc_failed_nodes_;
    bool this_node_is_leader_{ false };
    uint64_t elect_height_{ 0 };
    libff::alt_bn128_G1 backup_commit_signs_[common::kEachShardMaxNodeCount];
    std::shared_ptr<libff::alt_bn128_G1> bls_precommit_agg_sign_{ nullptr };
    std::shared_ptr<libff::alt_bn128_G1> bls_commit_agg_sign_{ nullptr };
    std::string precommit_hash_;
    std::string commit_hash_;
    uint32_t prepare_verify_failed_count_{ 0 };
    libff::alt_bn128_Fr local_sec_key_{ libff::alt_bn128_Fr::zero() };
    libff::alt_bn128_G2 common_pk_{ libff::alt_bn128_G2::zero() };
    uint32_t local_member_index_ = -1;
    zjcvm::ZjchainHost zjc_host_;
    std::map<uint64_t, std::set<uint32_t>> vss_random_map_;
    int32_t handle_last_error_code_{ 0 };
    std::string handle_last_error_msg_;
    std::unordered_map<std::string, std::shared_ptr<LeaderPrepareItem>> prepare_block_map_;
    common::Bitmap prepare_bitmap_;
    std::string leader_tbft_prepare_hash_;
    std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
    std::shared_ptr<WaitingTxsItem> txs_ptr_ = nullptr;
    std::shared_ptr<consensus::WaitingTxsPools> pools_mgr_ = nullptr;
    std::string precommit_bls_agg_verify_hash_;
    std::string commit_bls_agg_verify_hash_;
    libff::alt_bn128_G1 g1_prepare_hash_;
    libff::alt_bn128_G1 g1_precommit_hash_;
    std::shared_ptr<Zbft> pipeline_prev_zbft_ptr_ = nullptr;

public:
    inline void set_test_times(uint32_t index) {
#ifdef ZJC_UNITTEST
        times_[index] = common::TimeUtils::TimestampUs();
#endif
    }
#ifdef ZJC_UNITTEST
    uint64_t times_[64] = { 0 };
#endif

    DISALLOW_COPY_AND_ASSIGN(Zbft);
};

typedef std::shared_ptr<Zbft> ZbftPtr;

};  // namespace consensus

};  // namespace zjchain