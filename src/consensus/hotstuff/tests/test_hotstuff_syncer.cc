#include <consensus/hotstuff/hotstuff_manager.h>
#include <consensus/hotstuff/hotstuff_syncer.h>
#include <dht/dht_key.h>
#include <gtest/gtest.h>
#include <memory>
#include <security/ecdsa/ecdsa.h>
#include <transport/transport_utils.h>
#include <vss/vss_manager.h>

namespace shardora {

namespace hotstuff {

namespace test {

extern std::shared_ptr<ViewBlock> GenViewBlock(const HashStr &parent_hash, const View &view);
extern std::shared_ptr<QC> GenQC(const View &view, const HashStr &view_block_hash);
extern std::shared_ptr<block::protobuf::Block> GenBlock();
extern uint32_t GenLeaderIdx();
static const uint32_t NET_ID = 3;
static const uint32_t POOL = 63;

static const std::string sk_ =
    "b5039128131f96f6164a33bc7fbc48c2f5cf425e8476b1c4d0f4d186fbd0d708";

static transport::MultiThreadHandler net_handler_;
static std::shared_ptr<security::Security> security_ = nullptr;
static std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
static std::shared_ptr<hotstuff::ElectInfo> elect_info_ = nullptr;
static std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
static std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
static std::shared_ptr<db::Db> db_ = nullptr;
static std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
static std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
static std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
static std::shared_ptr<consensus::ContractGasPrepayment> gas_prepayment_ = nullptr;
static std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
static std::shared_ptr<BlockAcceptor> block_acceptor_ = nullptr;
static std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
static std::shared_ptr<elect::ElectManager> elect_mgr_ = nullptr;
static std::shared_ptr<bls::BlsManager> bls_mgr_ = nullptr;
static db::DbWriteBatch db_batch;
static std::shared_ptr<HotstuffSyncer> syncer_ = nullptr;
static std::shared_ptr<consensus::HotstuffManager> hotstuff_mgr_ = nullptr;


class TestViewBlockChainSyncer : public testing::Test {
protected:
    static void SetUpTestCase() {
        security_ = std::make_shared<security::Ecdsa>();
        security_->SetPrivateKey(common::Encode::HexDecode(sk_));
        
        account_mgr_ = std::make_shared<block::AccountManager>();
        system("rm -rf ./core.* ./db_syncer");
        db_ = std::make_shared<db::Db>();
        db_->Init("./db_syncer");
        
        kv_sync_ = std::make_shared<sync::KeyValueSync>();
        pools_mgr_ = std::make_shared<pools::TxPoolManager>(
                security_, db_, kv_sync_, account_mgr_);        
        block_mgr_ = std::make_shared<block::BlockManager>(net_handler_);
        
        vss_mgr_ = std::make_shared<vss::VssManager>(security_);
        contract_mgr_ = std::make_shared<contract::ContractManager>();
        gas_prepayment_ = std::make_shared<consensus::ContractGasPrepayment>(db_);
        tm_block_mgr_ = std::make_shared<timeblock::TimeBlockManager>();
        bls_mgr_ = std::make_shared<bls::BlsManager>(security_, db_);
        elect_mgr_ = std::make_shared<elect::ElectManager>(
                vss_mgr_, account_mgr_, block_mgr_, security_, bls_mgr_, db_,
                nullptr);
        elect_info_ = std::make_shared<ElectInfo>(security_, nullptr);
        
        kv_sync_->Init(block_mgr_, db_, nullptr);
        contract_mgr_->Init(security_);
        tm_block_mgr_->Init(vss_mgr_,account_mgr_);

        hotstuff_mgr_ = std::make_shared<consensus::HotstuffManager>();
        hotstuff_mgr_->Init(
                contract_mgr_,
                gas_prepayment_,
                vss_mgr_,
                account_mgr_,
                block_mgr_,
                elect_mgr_,
                pools_mgr_,
                security_,
                tm_block_mgr_,
                bls_mgr_,
                db_,
                [](std::shared_ptr<block::protobuf::Block>& block, db::DbWriteBatch& db_batch){});
    
        syncer_ = std::make_shared<HotstuffSyncer>(hotstuff_mgr_, db_, kv_sync_);
        syncer_->SetOnRecvViewBlockFn(StoreViewBlock);
    }

    static void TearDownTestCase() {
        system("rm -rf ./core.* ./db_syncer");
    }

    static transport::MessagePtr CreateRequestMsg() {
        transport::protobuf::Header msg;
        msg.set_src_sharding_id(NET_ID);
        dht::DhtKeyManager dht_key(NET_ID);
        msg.set_des_dht_key(dht_key.StrKey());
        msg.set_type(common::kHotstuffSyncMessage);

        auto vb_msg = view_block::protobuf::ViewBlockSyncMessage();
        auto req = vb_msg.mutable_view_block_req();
        req->set_pool_idx(POOL);
        req->set_network_id(NET_ID);

        *msg.mutable_view_block_proto() = vb_msg;

        transport::MessagePtr trans = nullptr;
        trans->header = msg;
        return trans;
    }

    static Status StoreViewBlock(
            const uint32_t& pool_idx,
            const std::shared_ptr<ViewBlockChain>& chain,
            const std::shared_ptr<ViewBlock>& view_block) {
        return chain->Store(view_block);
    }
};

TEST_F(TestViewBlockChainSyncer, TestMergeChain_HasCross) {
    // build ori chain
    auto b1 = GenViewBlock("", 2);
    auto b2 = GenViewBlock(b1->hash, b1->view+1);
    auto b3 = GenViewBlock(b2->hash, b2->view+1);
    auto b4 = GenViewBlock(b3->hash, b3->view+1);
    auto b5 = GenViewBlock(b4->hash, b4->view+1);
    
    auto ori_chain = std::make_shared<ViewBlockChain>(db_);
    ori_chain->Store(b1);
    ori_chain->Store(b2);
    ori_chain->Store(b3);

    auto sync_chain = std::make_shared<ViewBlockChain>(db_);
    sync_chain->Store(b2);
    sync_chain->Store(b3);
    sync_chain->Store(b4);

    syncer_->MergeChain(POOL, ori_chain, sync_chain);
    EXPECT_EQ(4, ori_chain->Size());
    EXPECT_EQ(3, sync_chain->Size());

    EXPECT_TRUE(ori_chain->Has(b1->hash));
    EXPECT_TRUE(ori_chain->Has(b2->hash));
    EXPECT_TRUE(ori_chain->Has(b3->hash));
    EXPECT_TRUE(ori_chain->Has(b4->hash));
}

TEST_F(TestViewBlockChainSyncer, TestMergeChain_NoCross) {
    // build ori chain
    auto b1 = GenViewBlock("", 1);
    auto b2 = GenViewBlock(b1->hash, b1->view+1);
    auto b3 = GenViewBlock(b2->hash, b2->view+1);
    auto b4 = GenViewBlock(b3->hash, b3->view+1);
    auto b5 = GenViewBlock(b4->hash, b4->view+1);
    auto b6 = GenViewBlock(b5->hash, b5->view+1);
    
    auto ori_chain = std::make_shared<ViewBlockChain>(db_);
    ori_chain->Store(b1);
    ori_chain->Store(b2);
    ori_chain->Store(b3);

    auto sync_chain = std::make_shared<ViewBlockChain>(db_);
    sync_chain->Store(b4);
    sync_chain->Store(b5);
    sync_chain->Store(b6);

    syncer_->MergeChain(POOL, ori_chain, sync_chain);
    EXPECT_EQ(3, ori_chain->Size());
    EXPECT_EQ(3, sync_chain->Size());

    EXPECT_TRUE(ori_chain->Has(b4->hash));
    EXPECT_TRUE(ori_chain->Has(b5->hash));
    EXPECT_TRUE(ori_chain->Has(b6->hash));
}

TEST_F(TestViewBlockChainSyncer, TestProcessResponse) {
    
}



} // namespace test

} // namespace consensus

} // namespace shardora

