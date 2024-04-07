#include <consensus/hotstuff/view_block_chain_manager.h>
#include <consensus/hotstuff/view_block_chain_syncer.h>
#include <dht/dht_key.h>
#include <gtest/gtest.h>
#include <memory>
#include <transport/transport_utils.h>

namespace shardora {

namespace consensus {

namespace test {

extern std::shared_ptr<ViewBlock> GenViewBlock(const HashStr &parent_hash, const View &view);
extern std::shared_ptr<QC> GenQC(const View &view, const HashStr &view_block_hash);
extern std::shared_ptr<block::protobuf::Block> GenBlock();
extern uint32_t GenLeaderIdx();


class TestViewBlockChainSyncer : public testing::Test {
protected:
    static const uint32_t NET_ID = 3;
    static const uint32_t POOL = 63;
    
    void SetUp() {
        view_block_chain_mgr_ = std::make_shared<ViewBlockChainManager>(GenViewBlock("", 1));
        syncer_ = std::make_shared<ViewBlockChainSyncer>(view_block_chain_mgr_);
        syncer_->SetOnRecvViewBlockFn(StoreViewBlock);
    }

    void TearDown() {
        
    }

    static transport::MessagePtr CreateRequestMsg() {
        transport::protobuf::Header msg;
        msg.set_src_sharding_id(NET_ID);
        dht::DhtKeyManager dht_key(NET_ID);
        msg.set_des_dht_key(dht_key.StrKey());
        msg.set_type(common::kViewBlockMessage);

        auto vb_msg = view_block::protobuf::ViewBlockMessage();
        auto req = vb_msg.mutable_view_block_req();
        req->set_pool_idx(POOL);
        req->set_network_id(NET_ID);

        *msg.mutable_view_block_proto() = vb_msg;

        transport::MessagePtr trans = nullptr;
        trans->header = msg;
        return trans;
    }

    static Status StoreViewBlock(const std::shared_ptr<ViewBlockChain>& chain, const std::shared_ptr<ViewBlock>& view_block) {
        return chain->Store(view_block);
    }

    std::shared_ptr<ViewBlockChainSyncer> syncer_;
    std::shared_ptr<ViewBlockChainManager> view_block_chain_mgr_;
};

TEST_F(TestViewBlockChainSyncer, TestMergeChain_HasCross) {
    // build ori chain
    auto b1 = GenViewBlock("", 2);
    auto b2 = GenViewBlock(b1->hash, b1->view+1);
    auto b3 = GenViewBlock(b2->hash, b2->view+1);
    auto b4 = GenViewBlock(b3->hash, b3->view+1);
    
    auto ori_chain = std::make_shared<ViewBlockChain>(b1);
    ori_chain->Store(b2);
    ori_chain->Store(b3);

    auto sync_chain = std::make_shared<ViewBlockChain>(b2);
    sync_chain->Store(b3);
    sync_chain->Store(b4);

    syncer_->MergeChain(ori_chain, sync_chain);
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
    
    auto ori_chain = std::make_shared<ViewBlockChain>(b1);
    ori_chain->Store(b2);
    ori_chain->Store(b3);

    auto sync_chain = std::make_shared<ViewBlockChain>(b4);
    sync_chain->Store(b5);
    sync_chain->Store(b6);

    syncer_->MergeChain(ori_chain, sync_chain);
    EXPECT_EQ(3, ori_chain->Size());
    EXPECT_EQ(3, sync_chain->Size());

    EXPECT_TRUE(ori_chain->Has(b4->hash));
    EXPECT_TRUE(ori_chain->Has(b5->hash));
    EXPECT_TRUE(ori_chain->Has(b6->hash));
}

TEST_F(TestViewBlockChainSyncer, TestProcessResponse) {}



} // namespace test

} // namespace consensus

} // namespace shardora

