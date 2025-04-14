#include <consensus/hotstuff/types.h>
#include <consensus/hotstuff/view_block_chain.h>
#include <gtest/gtest.h>
#include <libff/algebra/curves/alt_bn128/alt_bn128_g1.hpp>
#include <memory>
#include <protos/block.pb.h>

namespace shardora {

namespace hotstuff {

namespace test {

std::shared_ptr<ViewBlock> GenViewBlock(const HashStr &parent_hash, const View &view);
std::shared_ptr<QC> GenQC(const View &view, const HashStr &view_block_hash);
std::shared_ptr<block::protobuf::Block> GenBlock();
uint32_t GenLeaderIdx();
static std::shared_ptr<db::Db> db_ptr = nullptr;


std::shared_ptr<ViewBlock> GenViewBlock(const HashStr& parent_hash, const View& view) {
    auto vb = std::make_shared<ViewBlock>(parent_hash,
        GenQC(view, parent_hash),
        GenBlock(),
        view,
        GenLeaderIdx());
    vb->hash = vb->DoHash();
    return vb;
}

uint32_t GenLeaderIdx() {
    return 0;
}

std::shared_ptr<QC> GenQC(const View& view, const HashStr& view_block_hash) {
    auto sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::random_element());
    return std::make_shared<QC>(sign, view, view_block_hash);
}

std::shared_ptr<block::protobuf::Block> GenBlock() {
    auto block = std::make_shared<block::protobuf::Block>();
    return block;
}

class TestViewBlockChain : public testing::Test {
protected:
    void SetUp() {
        libff::alt_bn128_pp::init_public_params();
        genesis_ = GenViewBlock("", 1);
        db_ptr = std::make_shared<db::Db>();
        chain_ = std::make_shared<ViewBlockChain>(db_ptr);
        chain_->Store(genesis_);
    }

    void TearDown() {}

    std::shared_ptr<ViewBlockChain> chain_;
    std::shared_ptr<ViewBlock> genesis_;

    static void AssertEq(const std::shared_ptr<ViewBlock>& expect, const std::shared_ptr<ViewBlock>& actual) {
        EXPECT_TRUE(actual != nullptr);
        EXPECT_EQ(expect->hash, actual->hash);
        EXPECT_EQ(expect->parent_hash, actual->parent_hash);
        EXPECT_EQ(expect->view, actual->view);
    }

    static bool ContainBlock(const std::vector<std::shared_ptr<ViewBlock>>& slice, const std::shared_ptr<ViewBlock>& target) {
        for (auto& b : slice) {
            if (b->hash == target->hash) {
                return true;
            }
        }
        return false;
    }
};

TEST_F(TestViewBlockChain, TestStore_Genesis) {
    std::shared_ptr<ViewBlock> actual_genesis = nullptr;
    Status s = chain_->Get(genesis_->hash, actual_genesis);
    EXPECT_TRUE(s == Status::kSuccess);
    AssertEq(genesis_, actual_genesis);
}

TEST_F(TestViewBlockChain, TestStore_Success) {
    Status s = Status::kSuccess;
    
    auto vb = GenViewBlock(genesis_->hash, genesis_->view+1);
    s = chain_->Store(vb);
    EXPECT_TRUE(s == Status::kSuccess);

    std::shared_ptr<ViewBlock> actual_vb = nullptr;
    chain_->Get(vb->hash, actual_vb);
    AssertEq(vb, actual_vb);

    auto vb2 = GenViewBlock(vb->hash, vb->view+1);
    s = chain_->Store(vb2);
    EXPECT_TRUE(s == Status::kSuccess);
    
    std::shared_ptr<ViewBlock> actual_vb2 = nullptr;
    chain_->Get(vb2->hash, actual_vb2);
    AssertEq(vb2, actual_vb2);

    // same block hash
    s = chain_->Store(vb);
    EXPECT_EQ(s, Status::kError);

    // 允许不连续的 view
    auto vb4 = GenViewBlock(vb->hash, vb2->view+10);
    s = chain_->Store(vb4);
    EXPECT_TRUE(s == Status::kSuccess);
    
    std::shared_ptr<ViewBlock> actual_vb4 = nullptr;
    chain_->Get(vb4->hash, actual_vb4);
    AssertEq(vb4, actual_vb4);    
}

TEST_F(TestViewBlockChain, TestStore_Fail) {
    Status s = Status::kSuccess;
    
    auto vb = GenViewBlock(genesis_->hash, genesis_->view+1);
    s = chain_->Store(vb);
    EXPECT_TRUE(s == Status::kSuccess);
    EXPECT_TRUE(chain_->Has(vb->hash));

    // invalid parent hash
    auto vb2 = GenViewBlock("123", vb->view+1);
    s = chain_->Store(vb2);
    EXPECT_TRUE(s != Status::kSuccess);    
    EXPECT_FALSE(chain_->Has(vb2->hash));
}

TEST_F(TestViewBlockChain, TestGet) {}

TEST_F(TestViewBlockChain, TestExtends) {
    auto vb = GenViewBlock(genesis_->hash, genesis_->view+1);
    chain_->Store(vb);
    auto vb2 = GenViewBlock(vb->hash, vb->view+1);
    chain_->Store(vb2);
    auto vb3a = GenViewBlock(vb2->hash, vb2->view+1);
    chain_->Store(vb3a);
    auto vb3b = GenViewBlock(vb2->hash, vb2->view+1);
    chain_->Store(vb3b);
    auto vb4a = GenViewBlock(vb3a->hash, vb3a->view+1);
    chain_->Store(vb4a);
    auto vb4b = GenViewBlock(vb3b->hash, vb3b->view+1);
    chain_->Store(vb4b);

    EXPECT_TRUE(chain_->Extends(vb4a, vb3a));
    EXPECT_TRUE(chain_->Extends(vb4a, vb2));
    EXPECT_TRUE(chain_->Extends(vb4a, vb));
    EXPECT_TRUE(chain_->Extends(vb4a, genesis_));

    EXPECT_TRUE(chain_->Extends(vb4b, vb3b));
    EXPECT_TRUE(chain_->Extends(vb4b, vb2));
    EXPECT_TRUE(chain_->Extends(vb4b, vb));
    EXPECT_TRUE(chain_->Extends(vb4b, genesis_));

    EXPECT_FALSE(chain_->Extends(vb4b, vb3a));
    EXPECT_FALSE(chain_->Extends(vb4a, vb3b));
    EXPECT_TRUE(chain_->Extends(vb4a, vb4a));
}

TEST_F(TestViewBlockChain, TestPruneTo_OnlyForks) {
    auto vb = GenViewBlock(genesis_->hash, genesis_->view+1);
    chain_->Store(vb);
    auto vb2 = GenViewBlock(vb->hash, vb->view+1);
    chain_->Store(vb2);
    auto vb3a = GenViewBlock(vb2->hash, vb2->view+1);
    chain_->Store(vb3a);
    auto vb3b = GenViewBlock(vb2->hash, vb2->view+1);
    chain_->Store(vb3b);
    auto vb4a = GenViewBlock(vb3a->hash, vb3a->view+1);
    chain_->Store(vb4a);
    auto vb4b = GenViewBlock(vb3b->hash, vb3b->view+1);
    chain_->Store(vb4b);
    auto vb5b_x = GenViewBlock(vb4b->hash, vb4b->view+1);
    chain_->Store(vb5b_x);
    auto vb5b_y = GenViewBlock(vb4b->hash, vb4b->view+1);
    chain_->Store(vb5b_y);    

    std::vector<std::shared_ptr<ViewBlock>> forked_blocks;
    // prune vb3a and vb4a
    chain_->PruneTo(vb4b->hash, forked_blocks, false);

    EXPECT_EQ(2, forked_blocks.size());
    EXPECT_TRUE(ContainBlock(forked_blocks, vb3a));
    EXPECT_TRUE(ContainBlock(forked_blocks, vb4a));    

    EXPECT_FALSE(chain_->Has(vb3a->hash));
    EXPECT_FALSE(chain_->Has(vb4a->hash));
    
    // vb5b_x and vb5b_y still exist
    EXPECT_TRUE(chain_->Has(vb5b_x->hash));
    EXPECT_TRUE(chain_->Has(vb5b_y->hash));

    std::vector<std::shared_ptr<ViewBlock>> forked_blocks2; 
    // prune vb5b_y
    chain_->PruneTo(vb5b_x->hash, forked_blocks2, false);
    EXPECT_EQ(1, forked_blocks2.size());
    EXPECT_TRUE(ContainBlock(forked_blocks2, vb5b_y));

    // has vb5b_x
    EXPECT_TRUE(chain_->Has(vb5b_x->hash));
    // no vb5b_y
    EXPECT_FALSE(chain_->Has(vb5b_y->hash));
}

TEST_F(TestViewBlockChain, TestPruneTo_ForksAndHistory) {
    auto vb = GenViewBlock(genesis_->hash, genesis_->view+1);
    chain_->Store(vb);
    auto vb2 = GenViewBlock(vb->hash, vb->view+1);
    chain_->Store(vb2);
    auto vb3a = GenViewBlock(vb2->hash, vb2->view+1);
    chain_->Store(vb3a);
    auto vb3b = GenViewBlock(vb2->hash, vb2->view+1);
    chain_->Store(vb3b);
    auto vb4a = GenViewBlock(vb3a->hash, vb3a->view+1);
    chain_->Store(vb4a);
    auto vb4b = GenViewBlock(vb3b->hash, vb3b->view+1);
    chain_->Store(vb4b);
    auto vb5b_x = GenViewBlock(vb4b->hash, vb4b->view+1);
    chain_->Store(vb5b_x);
    auto vb5b_y = GenViewBlock(vb4b->hash, vb4b->view+1);
    chain_->Store(vb5b_y);    

    std::vector<std::shared_ptr<ViewBlock>> forked_blocks;
    // prune vb3a and vb4a
    chain_->PruneTo(vb4b->hash, forked_blocks, true);

    EXPECT_EQ(2, forked_blocks.size());
    EXPECT_TRUE(ContainBlock(forked_blocks, vb3a));
    EXPECT_TRUE(ContainBlock(forked_blocks, vb4a));

    EXPECT_EQ(3, chain_->Size());

    std::shared_ptr<ViewBlock> actual_vb3b = nullptr;
    chain_->Get(vb3b->hash, actual_vb3b);
    EXPECT_TRUE(actual_vb3b == nullptr);
}

} // namespace test

} // namespace consensus

} // namespace shardora

