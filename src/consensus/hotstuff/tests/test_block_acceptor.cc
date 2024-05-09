#include <common/time_utils.h>
#include <consensus/hotstuff/utils.h>
#include <gtest/gtest.h>
#include <consensus/hotstuff/block_acceptor.h>
#include <protos/address.pb.h>
#include <protos/block.pb.h>
#include <protos/pools.pb.h>
#include <security/ecdsa/ecdsa.h>
#include <vss/vss_manager.h>

namespace shardora {

namespace contract {
class ContractManager;
}

namespace pools {
class TxPoolManager;
}

namespace consensus {
class ContractGasPrepayment;
}

namespace block {
class BlockManager;
class AccountManager;
}

namespace hotstuff {

namespace test {

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
static std::shared_ptr<BlockAcceptor> block_acceptor_ = nullptr;

class TestBlockAcceptor : public testing::Test {
protected:
    std::shared_ptr<block::protobuf::Block> prev_block_ = nullptr;
    
    static void SetUpTestCase() {
        security_ = std::make_shared<security::Ecdsa>();
        security_->SetPrivateKey(common::Encode::HexDecode(sk_));
        account_mgr_ = std::make_shared<block::AccountManager>();
        system("rm -rf ./core.* ./db_acceptor");
        db_ = std::make_shared<db::Db>();
        db_->Init("./db_acceptor");
        
        kv_sync_ = std::make_shared<sync::KeyValueSync>();
        pools_mgr_ = std::make_shared<pools::TxPoolManager>(
                security_, db_, kv_sync_, account_mgr_);        
        block_mgr_ = std::make_shared<block::BlockManager>(net_handler_);
        
        elect_info_ = std::make_shared<ElectInfo>(security_, nullptr);
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
                tm_block_mgr_,
                [](std::shared_ptr<block::protobuf::Block>& block, db::DbWriteBatch& db_batch) {
                    return;
                });

        // 创建一个账户
        auto account_info = CreateAddress();
        prefix_db_ = std::make_shared<protos::PrefixDb>(db_);
        prefix_db_->AddAddressInfo(account_info->addr(), *account_info);
        account_mgr_->Init(db_, pools_mgr_);

        auto address_info = account_mgr_->GetAccountInfo(account_info->addr());
    }

    static void TearDownTestCase() {
        system("rm -rf ./core.* ./db_acceptor");
    }

    static std::shared_ptr<address::protobuf::AddressInfo> CreateAddress() {
        auto account_info = std::make_shared<address::protobuf::AddressInfo>();
        account_info->set_pubkey(security_->GetPublicKeyUnCompressed());
        account_info->set_pool_index(POOL);
        account_info->set_addr(security_->GetAddress(security_->GetPublicKeyUnCompressed()));
        account_info->set_type(address::protobuf::kNormal);
        account_info->set_sharding_id(3);
        account_info->set_latest_height(0);
        account_info->set_balance(100000000lu);
        return account_info;
    }

    void SetUp() {
        // 交易池放入一个交易
        prev_block_ = CreateBlock(POOL, 10, "prev_block_hash");
        pools_mgr_->UpdateLatestInfo(prev_block_, db_batch);        
    }

    void TearDown() {
    }

    static std::shared_ptr<pools::protobuf::TxMessage> CreateTxMessage() {
        // 创建交易 TxMessage
        std::string random_prefix = common::Random::RandomString(33);
        uint32_t* test_arr = (uint32_t*)random_prefix.data();
        auto tx_info = std::make_shared<pools::protobuf::TxMessage>();
        tx_info->set_step(pools::protobuf::kNormalFrom);
        tx_info->set_pubkey(security_->GetPublicKeyUnCompressed());
        tx_info->set_to("27d4c39244f26c157b5a87898569ef4ce5807413");
        auto gid = std::string((char*)test_arr, 32);
        tx_info->set_gid(gid);
        tx_info->set_gas_limit(10000llu);
        tx_info->set_amount(1);
        tx_info->set_gas_price(common::kBuildinTransactionGasPrice);
        return tx_info;
    }

    static std::shared_ptr<block::protobuf::Block> CreateBlock(uint32_t pool_idx, uint64_t height, std::string prehash) {
        auto block = std::make_shared<block::protobuf::Block>();
        block->set_pool_index(pool_idx);
        block->set_height(height);
        block->set_prehash(prehash);
        block->set_hash(GetBlockHash(*block));
        block->set_timestamp(common::TimeUtils::TimestampUs());
        return block;
    }
};

// block_info 交易池错误
TEST_F(TestBlockAcceptor, Accept_NotSamePool) {
    auto block_info = std::make_shared<IBlockAcceptor::blockInfo>();
    block_info->view = View(10);
    block_info->block = CreateBlock(POOL+1, 10, prev_block_->hash());
    block_info->tx_type = pools::protobuf::kNormalFrom;
    block_info->txs.push_back(CreateTxMessage());

    Status s = block_acceptor_->Accept(block_info, false);
    EXPECT_TRUE(s == Status::kError);
}

// 允许 block_info 没有打包任何交易
TEST_F(TestBlockAcceptor, Accept_NoWrappedTxs) {
    auto block_info = std::make_shared<IBlockAcceptor::blockInfo>();
    block_info->view = View(10);
    block_info->block = CreateBlock(POOL, 10, prev_block_->hash());
    block_info->tx_type = pools::protobuf::kNormalFrom;

    Status s = block_acceptor_->Accept(block_info, false);
    EXPECT_TRUE(s == Status::kSuccess);
}

// 不接受旧的 block
TEST_F(TestBlockAcceptor, Accept_InvalidBlock_OldHeightBlock) {
    EXPECT_EQ(10, pools_mgr_->latest_height(POOL));
        
    auto block_info = std::make_shared<IBlockAcceptor::blockInfo>();
    block_info->view = View(10);
    block_info->block = CreateBlock(POOL, prev_block_->height(), prev_block_->hash());
    block_info->tx_type = pools::protobuf::kNormalFrom;
    block_info->txs.push_back(CreateTxMessage());

    Status s = block_acceptor_->Accept(block_info, false);
    EXPECT_TRUE(s == Status::kAcceptorBlockInvalid);
}

// 允许本交易池中没有 From 交易，收到后同步
TEST_F(TestBlockAcceptor, Accept_InvalidTxs_NormalFromTx) {
    EXPECT_EQ(10, pools_mgr_->latest_height(POOL));
    
        
    auto block_info = std::make_shared<IBlockAcceptor::blockInfo>();
    block_info->view = View(10);
    block_info->block = CreateBlock(POOL, prev_block_->height()+1, prev_block_->hash());
    block_info->tx_type = pools::protobuf::kNormalFrom;
    block_info->txs.push_back(CreateTxMessage());

    EXPECT_EQ(0, block_info->block->tx_list_size());

    Status s = block_acceptor_->Accept(block_info, false);

    
    EXPECT_EQ(s, Status::kSuccess);
    EXPECT_EQ(1, block_info->block->tx_list_size());
    EXPECT_EQ(block_info->txs[0]->amount(), block_info->block->tx_list(0).amount());
}

} // namespace test

} // namespace hotstuff

} // namespace shardora

