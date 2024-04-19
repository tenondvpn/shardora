#include <gtest/gtest.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <protos/block.pb.h>
#include <protos/pools.pb.h>
#include <security/ecdsa/ecdsa.h>
#include <vss/vss_manager.h>

namespace shardora {

namespace hotstuff {

namespace test {

static const uint32_t POOL = 3;
static const std::string sk = "8bd064dde62eb50cdb1d8ec9c10043d6ac8a57b3c9b7b0178e4c63938d8ab0da";

class TestBlockAcceptor : public testing::Test {
protected:
    void SetUp() {
        security_ = std::make_shared<security::Ecdsa>();
        security_->SetPrivateKey(common::Encode::HexDecode(sk));
        account_mgr_ = std::make_shared<block::AccountManager>();
        std::string db_path = "./db";
        db_ = std::make_shared<db::Db>();
        db_->Init(db_path);
        
        kv_sync_ = std::make_shared<sync::KeyValueSync>();
        pools_mgr_ = std::make_shared<pools::TxPoolManager>(
        security_, db_, kv_sync_, account_mgr_);        
        block_mgr_ = std::make_shared<block::BlockManager>(net_handler_);
        account_mgr_->Init(db_, pools_mgr_);
        elect_info_ = std::make_shared<ElectInfo>(security_);
        vss_mgr_ = std::make_shared<vss::VssManager>(security_);
        contract_mgr_ = std::make_shared<contract::ContractManager>();
        gas_prepayment_ = std::make_shared<consensus::ContractGasPrepayment>(db_);
        tm_block_mgr_ = std::make_shared<timeblock::TimeBlockManager>();
        
        kv_sync_->Init(block_mgr_, db_);
        contract_mgr_->Init(security_);
        tm_block_mgr_->Init(vss_mgr_,account_mgr_);
        
        block_acceptor_ = std::make_shared<BlockAcceptor>(
                POOL,
                security_,
                account_mgr_,
                elect_info_,
                vss_mgr_,
                contract_mgr_,
                db_,
                gas_prepayment_,
                pools_mgr_,
                block_mgr_,
                tm_block_mgr_);
        
    }

    void TearDown() {}

    transport::MultiThreadHandler net_handler_;
    std::shared_ptr<security::Security> security_ = nullptr;
    std::shared_ptr<block::AccountManager> account_mgr_ = nullptr;
    std::shared_ptr<hotstuff::ElectInfo> elect_info_ = nullptr;
    std::shared_ptr<vss::VssManager> vss_mgr_ = nullptr;
    std::shared_ptr<contract::ContractManager> contract_mgr_ = nullptr;
    std::shared_ptr<db::Db> db_ = nullptr;
    std::shared_ptr<pools::TxPoolManager> pools_mgr_ = nullptr;
    std::shared_ptr<sync::KeyValueSync> kv_sync_ = nullptr;
    std::shared_ptr<block::BlockManager> block_mgr_ = nullptr;
    std::shared_ptr<consensus::ContractGasPrepayment> gas_prepayment_ = nullptr;
    std::shared_ptr<timeblock::TimeBlockManager> tm_block_mgr_ = nullptr;
    std::shared_ptr<BlockAcceptor> block_acceptor_ = nullptr;
};

TEST_F(TestBlockAcceptor, Accept_NotSamePool) {
    auto block_info = std::make_shared<IBlockAcceptor::blockInfo>();
    block_info->view = View(10);
    block_info->block = std::make_shared<block::protobuf::Block>();
    block_info->block->set_pool_index(POOL+1);
    block_info->tx_type = pools::protobuf::kNormalFrom;

    Status s = block_acceptor_->Accept(block_info);
    EXPECT_TRUE(s == Status::kError);
}

} // namespace test

} // namespace hotstuff

} // namespace shardora

