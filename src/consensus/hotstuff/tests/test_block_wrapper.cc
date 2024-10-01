#include <common/time_utils.h>
#include <gtest/gtest.h>
#include <consensus/hotstuff/block_wrapper.h>
#include <protos/hotstuff.pb.h>
#include <security/ecdsa/ecdsa.h>

namespace shardora {
namespace hotstuff {
namespace test {

extern std::shared_ptr<ViewBlock> GenViewBlock(const HashStr &parent_hash, const View &view);
extern std::shared_ptr<QC> GenQC(const View &view, const HashStr &view_block_hash);
extern std::shared_ptr<block::protobuf::Block> GenBlock();
extern uint32_t GenLeaderIdx();

static const uint32_t NET = 3;
static const uint32_t POOL = 0;
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
static std::shared_ptr<protos::PrefixDb> prefix_db_ = nullptr;
static db::DbWriteBatch db_batch;
static std::shared_ptr<IBlockWrapper> block_wrapper_ = nullptr;

class TestBlockWrapper : public testing::Test {
protected:
    static void SetUpTestCase() {
        common::GlobalInfo::Instance()->set_network_id(NET);
        security_ = std::make_shared<security::Ecdsa>();
        security_->SetPrivateKey(common::Encode::HexDecode(sk_));        
        system("rm -rf ./core.* ./db_wrapper");
        db_ = std::make_shared<db::Db>();
        db_->Init("./db_wrapper");
        
        account_mgr_ = std::make_shared<block::AccountManager>();
        kv_sync_ = std::make_shared<sync::KeyValueSync>();
        pools_mgr_ = std::make_shared<pools::TxPoolManager>(
                security_, db_, kv_sync_, account_mgr_);
        block_mgr_ = std::make_shared<block::BlockManager>(net_handler_);
        elect_info_ = std::make_shared<ElectInfo>(security_, nullptr);
        block_wrapper_ = std::make_shared<BlockWrapper>(
                POOL, pools_mgr_, tm_block_mgr_, block_mgr_, elect_info_);

        account_mgr_->Init(db_, pools_mgr_);

        auto member = std::make_shared<common::BftMember>(1, "1", "pk1", 1, 0);
        auto members = std::make_shared<common::Members>();
        members->push_back(member);
        auto common_pk = libff::alt_bn128_G2::one();
        auto sk = libff::alt_bn128_Fr::one();
        elect_info_->OnNewElectBlock(NET, 1, members, common_pk, sk);
    }

    static void TearDownTestCase() {}
};

TEST_F(TestBlockWrapper, Wrap) {
    uint64_t prev_height = 1;
    HashStr prev_hash = "prev hash";
    auto block = std::make_shared<block::protobuf::Block>();
    block->set_hash(prev_hash);
    block->set_height(prev_height);
    block->set_timestamp(common::TimeUtils::TimestampMs());

    auto sign = std::make_shared<libff::alt_bn128_G1>(libff::alt_bn128_G1::one());
    auto prev_view_block = std::make_shared<ViewBlock>(
            "parent_vb_hash",
            std::make_shared<QC>(sign, prev_height-1, "prev prev hash"),
            block,
            prev_height,
            GenLeaderIdx());

    auto new_block = std::make_shared<block::protobuf::Block>();
    auto tx_propose = std::make_shared<hotstuff::protobuf::TxPropose>();
    Status s = block_wrapper_->Wrap(
            prev_view_block->block, GenLeaderIdx(), new_block, tx_propose, false);
    
    EXPECT_EQ(Status::kSuccess, s);
    EXPECT_EQ(prev_view_block->block->height()+1, new_block->height());
    EXPECT_EQ(prev_view_block->block->hash(), new_block->prehash());
    EXPECT_TRUE(tx_propose->txs().empty());
}
            
}
}
}
