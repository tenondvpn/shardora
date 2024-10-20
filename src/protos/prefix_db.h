#pragma once

#include <evmc/evmc.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
#include <libbls/tools/utils.h>
#include <protos/view_block.pb.h>

#include "common/encode.h"
#include "common/global_info.h"
#include "common/hash.h"
#include "common/string_utils.h"
#include "common/tick.h"
#include "common/time_utils.h"
#include "db/db.h"
#include "protos/address.pb.h"
#include "protos/block.pb.h"
#include "protos/bls.pb.h"
#include "protos/c2c.pb.h"
#include "protos/elect.pb.h"
#include "protos/init.pb.h"
#include "protos/sync.pb.h"
#include "protos/timeblock.pb.h"
#include "protos/tx_storage_key.h"
#include "security/security.h"
#include <zjcvm/zjcvm_utils.h>
#include "consensus/hotstuff/types.h"

namespace shardora {

namespace protos {

typedef std::shared_ptr<address::protobuf::AddressInfo> AddressInfoPtr;
static const std::string kFieldBytesCode = "__bytes_code";
static const std::string kGenesisElectPrikeyEncryptKey = common::Encode::HexDecode(
    "17dfdd4d49509691361225e9059934675dea440d123aa8514441aa6788354016");

// transaction contract attr keys

static const std::string kAddressPrefix = "a\x01";
static const std::string kBlsVerifyPrefex = "b\x01";
static const std::string kBlsSwapKeyPrefex = "c\x01";
static const std::string kAddressStorageKeyPrefex = "d\x01";
static const std::string kBlsSecKeyPrefex = "e\x01";
static const std::string kAddressPubkeyPrefex = "f\x01";
static const std::string kBlsPrivateKeyPrefix = "g\x01";
static const std::string kLatestElectBlockPrefix = "h\x01";
static const std::string kLatestTimeBlockPrefix = "i\x01";
static const std::string kBlockHeightPrefix = "j\x01";
static const std::string kBlockPrefix = "k\x01";
static const std::string kToTxsHeightsPrefix = "l\x01";
static const std::string kLatestToTxsHeightsPrefix = "m\x01";
static const std::string kLatestPoolPrefix = "n\x01";
static const std::string kHeightTreePrefix = "o\x01";
static const std::string kGidPrefix = "p\x01";
static const std::string kContractGasPrepaymentPrefix = "q\x01";
static const std::string kBlsInfoPrefix = "r\x01";
static const std::string kBlsVerifyValuePrefix = "s\x01";
static const std::string kTemporaryKeyPrefix = "t\x01";
static const std::string kPresetPolynomialPrefix = "u\x01";
static const std::string kPresetVerifyValuePrefix = "v\x01";
static const std::string kStatisticHeightsPrefix = "w\x01";
static const std::string kConsensusedStatisticPrefix = "x\x01";
static const std::string kRootStatisticedPrefix = "y\x01";
static const std::string kElectNodesStokePrefix = "z\x01";
static const std::string kSaveLatestElectHeightPrefix = "aa\x01";
static const std::string kSaveChoosedJoinShardPrefix = "ab\x01";
static const std::string kGenesisTimeblockPrefix = "ac\x01";
static const std::string kLocalPolynomialPrefix = "ad\x01";
static const std::string kLocalVerifiedG2Prefix = "ae\x01";
static const std::string kLocalTempPolynomialPrefix = "af\x01";
static const std::string kLocalTempCommonPublicKeyPrefix = "ag\x01";
static const std::string kNodeVerificationVectorPrefix = "ah\x01";
static const std::string kNodeLocalElectPosPrefix = "ai\x01";
static const std::string kCrossCheckHeightPrefix = "aj\x01";
static const std::string kElectHeightWithBlsCommonPkPrefix = "ak\x01";
static const std::string kBftInvalidHeightHashs = "al\x01";
static const std::string kTempBftInvalidHeightHashs = "am\x01";
static const std::string kViewBlockInfoPrefix = "an\x01";
static const std::string kAggBlsPrivateKeyPrefix = "ao\x01";

class PrefixDb {
public:
    PrefixDb(const std::shared_ptr<db::Db>& db_ptr) : db_(db_ptr) {
    }

    ~PrefixDb() {
        Destroy();
    }

    void InitGidManager() {
        db_batch_tick_.CutOff(
            5000000lu,
            std::bind(&PrefixDb::DumpGidToDb, this));
    }

    void Destroy() {
        for (int32_t i = 0; i < common::kMaxThreadCount; ++i) {
            db_->Put(db_batch_[i]);
        }
    }

    void AddAddressInfo(
            const std::string& addr,
            const address::protobuf::AddressInfo& addr_info) {
        AddAddressInfo(addr, addr_info.SerializeAsString());
    }

    void AddAddressInfo(
            const std::string& addr,
            const address::protobuf::AddressInfo& addr_info,
            db::DbWriteBatch& db_batch) {
        db_batch.Put(kAddressPrefix + addr, addr_info.SerializeAsString());
        ZJC_DEBUG("success add addr: %s", common::Encode::HexEncode(kAddressPrefix + addr).c_str());
    }

    void AddAddressInfo(const std::string& addr, const std::string& val) {
        auto st = db_->Put(kAddressPrefix + addr, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    void AddAddressInfo(
            const std::string& addr,
            const std::string& val,
            db::DbWriteBatch& write_batch) {
        write_batch.Put(kAddressPrefix + addr, val);
    }

    void AddNowElectHeight2Plege(const std::string& addr , const uint64_t height , db::DbWriteBatch& db_batch) {
        auto key = common::Encode::HexDecode(addr) + common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000000");
        evmc::bytes32 tmp_val{};
        zjcvm::Uint64ToEvmcBytes32(tmp_val, height);

        auto value =  std::string((char*)tmp_val.bytes, sizeof(tmp_val.bytes));
        SaveTemporaryKv(key, value, db_batch);
    }

    std::shared_ptr<address::protobuf::AddressInfo> GetAddressInfo(const std::string& addr) {
        std::string val;
        auto st = db_->Get(kAddressPrefix + addr, &val);
        if (!st.ok()) {
            ZJC_DEBUG("failed get addr: %s", common::Encode::HexEncode(kAddressPrefix + addr).c_str());
            return nullptr;
        }

        auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
        if (!addr_info->ParseFromString(val)) {
            ZJC_INFO("failed parse addr: %s", common::Encode::HexEncode(kAddressPrefix + addr).c_str());
            return nullptr;
        }

        return addr_info;
    }

    
    void GetAllAddressInfo(std::unordered_map<std::string, protos::AddressInfoPtr>* addr_map_ptr) {
        std::string key;
        key.reserve(128);
        key.append(kAddressPrefix);
        std::map<std::string, std::string> str_addr_map;
        db_->GetAllPrefix(key, str_addr_map);
        auto& addr_map = *addr_map_ptr;
        for (auto iter = str_addr_map.begin(); iter != str_addr_map.end(); ++iter) {
            auto addr_ptr = std::make_shared<address::protobuf::AddressInfo>();
            if (!addr_ptr->ParseFromString(iter->second)) {
                continue;
            }

            addr_map[addr_ptr->addr()] = addr_ptr;
        }
    }

    void SaveSwapKey(
            uint32_t local_member_idx,
            uint64_t height,
            uint32_t local_idx,
            uint32_t peer_idx,
            const std::string& seckey) {
        std::string key;
        key.reserve(32);
        key.append(kBlsSwapKeyPrefex);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&height, sizeof(height));
        key.append((char*)&local_idx, sizeof(local_idx));
        key.append((char*)&peer_idx, sizeof(peer_idx));
        auto st = db_->Put(key, seckey);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool GetSwapKey(
            uint32_t local_member_idx,
            uint64_t height,
            uint32_t local_idx,
            uint32_t peer_idx,
            std::string* seckey) {
        std::string key;
        key.reserve(32);
        key.append(kBlsSwapKeyPrefex);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&height, sizeof(height));
        key.append((char*)&local_idx, sizeof(local_idx));
        key.append((char*)&peer_idx, sizeof(peer_idx));
        auto st = db_->Get(key, seckey);
        if (!st.ok()) {
            return false;
        }

        return true;
    }

    void SaveLatestElectBlock(
            const elect::protobuf::ElectBlock& elect_block,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(48);
        key.append(kLatestElectBlockPrefix);
        auto sharding_id = elect_block.shard_network_id();
        key.append((char*)&sharding_id, sizeof(sharding_id));
        db_batch.Put(key, elect_block.SerializeAsString());

        key.clear();
        key.reserve(64);
        key.append(kSaveLatestElectHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        auto st = db_->Get(key, &val);
        std::set<uint64_t> height_set;
        if (st.ok()) {
            uint64_t* tmp = (uint64_t*)val.c_str();
            uint32_t len = val.size() / 8;
            for (uint32_t i = 0; i < len; ++i) {
                height_set.insert(tmp[i]);
            }
        }

        auto height = elect_block.elect_height();
        height_set.insert(height);
        auto biter = height_set.begin();
        while (height_set.size() > kSaveElectHeightCount) {
            height_set.erase(biter++);
        }

        char data[8 * kSaveElectHeightCount];
        uint64_t* tmp = (uint64_t*)data;
        uint32_t idx = 0;
        for (auto hiter = height_set.begin(); hiter != height_set.end(); ++hiter) {
            tmp[idx++] = *hiter;
        }

        std::string db_val(data, sizeof(data));
        db_batch.Put(key, db_val);
        ZJC_DEBUG("save elect block sharding id: %u, height: %lu",
            sharding_id, elect_block.elect_height());
    }

    bool GetLatestElectBlock(uint32_t sharding_id,
            elect::protobuf::ElectBlock* elect_block) {
        std::string key;
        key.reserve(48);
        key.append(kLatestElectBlockPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        ZJC_DEBUG("get elect block sharding id: %u",
            sharding_id);
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!elect_block->ParseFromString(val)) {
            return false;
        }

        ZJC_DEBUG("success get elect block sharding id: %u, height: %lu",
            sharding_id, elect_block->elect_height());
        return true;
    }

    void SaveLatestTimeBlock(uint64_t block_height, db::DbWriteBatch& db_batch) {
        std::string key(kLatestTimeBlockPrefix);
        timeblock::protobuf::TimeBlock tmblock;
        if (GetLatestTimeBlock(&tmblock)) {
            if (tmblock.height() >= block_height) {
                return;
            }
        }

        tmblock.set_height(block_height);
        db_batch.Put(key, tmblock.SerializeAsString());
        ZJC_DEBUG("dddddd success latest time block: %lu", block_height);
    }

    bool GetLatestTimeBlock(timeblock::protobuf::TimeBlock* tmblock) {
        std::string key(kLatestTimeBlockPrefix);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!tmblock->ParseFromString(val)) {
            return false;
        }

        ZJC_DEBUG("dddddd success get latest time block: %lu", tmblock->height());
        return true;
    }

    void SaveBlockHashWithBlockHeight(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            const std::string& block_hash,
            db::DbWriteBatch& batch) {
        std::string key;
        key.reserve(32);
        key.append(kBlockHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        batch.Put(key, block_hash);
        ZJC_DEBUG("save sync key value %u_%u_%lu, success save block with height: %u, %u, %lu",
            sharding_id, pool_index, height, sharding_id, pool_index, height);
    }

    bool GetBlockHashWithBlockHeight(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            std::string* block_hash) {
        std::string key;
        key.reserve(32);
        key.append(kBlockHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        auto st = db_->Get(key, block_hash);
        if (!st.ok()) {
            return false;
        }

        ZJC_DEBUG("get sync key value %u_%u_%lu, success get block with height: %u, %u, %lu",
            sharding_id, pool_index, height, sharding_id, pool_index, height);
        return true;
    }

    bool SaveBlock(const block::protobuf::Block& block, db::DbWriteBatch& batch) {
        if (BlockExists(block.hash())) {
            return false;
        }

#ifndef ENABLE_HOTSTUFF
        assert(block.has_bls_agg_sign_x() && block.has_bls_agg_sign_y());
#endif
        std::string key;
        key.reserve(48);
        key.append(kBlockPrefix);
        key.append(block.hash());
        SaveBlockHashWithBlockHeight(
            block.network_id(),
            block.pool_index(),
            block.height(),
            block.hash(),
            batch);
        batch.Put(key, block.SerializeAsString());
        if (!block.tx_list().empty() && block.tx_list(0).step() == pools::protobuf::kConsensusRootTimeBlock) {
            ZJC_DEBUG("ddddddd save tm block: %lu", block.height());
        }
        return true;
    }

    bool GetBlock(const std::string& block_hash, block::protobuf::Block* block) {
        std::string key;
        key.reserve(48);
        key.append(kBlockPrefix);
        key.append(block_hash);
        std::string block_str;
        auto st = db_->Get(key, &block_str);
        if (!st.ok()) {
            return false;
        }

        if (!block->ParseFromString(block_str)) {
            return false;
        }
#ifndef ENABLE_HOTSTUFF
        assert(block->has_bls_agg_sign_x() && block->has_bls_agg_sign_y());
#endif
        return true;
    }

    bool GetBlockString(const std::string& block_hash, std::string* block_str) {
        std::string key;
        key.reserve(48);
        key.append(kBlockPrefix);
        key.append(block_hash);
        auto st = db_->Get(key, block_str);
        if (!st.ok()) {
            return false;
        }

        return true;
    }

    bool BlockExists(const std::string& block_hash) {
        std::string key;
        key.reserve(48);
        key.append(kBlockPrefix);
        key.append(block_hash);
        return db_->Exist(key);
    }

    bool BlockExists(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height) {
        std::string key;
        key.reserve(32);
        key.append(kBlockHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        return db_->Exist(key);
    }

    bool GetBlockWithHeight(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            block::protobuf::Block* block) {
        std::string block_hash;
        ZJC_DEBUG("GetBlockWithHeight.");
        if (!GetBlockHashWithBlockHeight(sharding_id, pool_index, height, &block_hash)) {
            return false;
        }

        return GetBlock(block_hash, block);
    }

    bool GetBlockStringWithHeight(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            std::string* block_str) {
        std::string block_hash;
        ZJC_DEBUG("GetBlockStringWithHeight.");
        if (!GetBlockHashWithBlockHeight(sharding_id, pool_index, height, &block_hash)) {
            return false;
        }

        return GetBlockString(block_hash, block_str);
    }

    void SaveToTxsHeights(const pools::protobuf::ToTxHeights& heights) {
        std::string key;
        key.reserve(48);
        key.append(kToTxsHeightsPrefix);
        auto val = heights.SerializeAsString();
        auto hash = common::Hash::keccak256(val);
        key.append(hash);
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool GetToTxsHeights(const std::string& hash, pools::protobuf::ToTxHeights* heights) {
        std::string key;
        key.reserve(48);
        key.append(kToTxsHeightsPrefix);
        key.append(hash);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!heights->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveLatestToTxsHeights(const pools::protobuf::ShardToTxItem& heights) {
        std::string key;
        key.reserve(48);
        key.append(kLatestToTxsHeightsPrefix);
        uint32_t sharding_id = heights.sharding_id();
        key.append((char*)&sharding_id, sizeof(sharding_id));
        auto st = db_->Put(key, heights.SerializeAsString());
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    void SaveLatestToTxsHeights(
            const pools::protobuf::ShardToTxItem& heights,
            db::DbWriteBatch& batch) {
        std::string key;
        key.reserve(48);
        key.append(kLatestToTxsHeightsPrefix);
        uint32_t sharding_id = heights.sharding_id();
        key.append((char*)&sharding_id, sizeof(sharding_id));
        batch.Put(key, heights.SerializeAsString());
    }

    bool GetLatestToTxsHeights(uint32_t sharding_id, pools::protobuf::ShardToTxItem* heights) {
        std::string key;
        key.reserve(48);
        key.append(kLatestToTxsHeightsPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!heights->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveTemporaryKv(
            const std::string& tmp_key,
            const std::string& val) {
        std::string key = kTemporaryKeyPrefix + tmp_key;
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    void SaveTemporaryKv(
            const std::string& tmp_key,
            const std::string& val,
            db::DbWriteBatch& db_batch) {
        std::string key = kTemporaryKeyPrefix + tmp_key;
        db_batch.Put(key, val);
    }

    bool GetTemporaryKv(
            const std::string& tmp_key,
            std::string* val) {
        std::string key = kTemporaryKeyPrefix + tmp_key;
        auto st = db_->Get(key, val);
        return st.ok();
    }

    void SaveLatestPoolInfo(
            uint32_t sharding_id,
            uint32_t pool_index,
            const pools::protobuf::PoolLatestInfo& pool_info,
            db::DbWriteBatch& batch) {
        std::string key;
        key.reserve(48);
        key.append(kLatestPoolPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        batch.Put(key, pool_info.SerializeAsString());
    }

    bool GetLatestPoolInfo(
            uint32_t sharding_id,
            uint32_t pool_index,
            pools::protobuf::PoolLatestInfo* pool_info) {
        std::string key;
        key.reserve(48);
        key.append(kLatestPoolPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!pool_info->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveViewBlockInfo(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t block_height,
            const view_block::protobuf::ViewBlockItem& pb_view_block,
            std::shared_ptr<db::DbWriteBatch>& db_batch) {
        std::string key;
        key.reserve(32);
        key.append(kViewBlockInfoPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&block_height, sizeof(block_height));
        // batch.Put(key, pb_view_block.SerializeAsString());
        db_batch->Put(key, pb_view_block.SerializeAsString());
        ZJC_DEBUG("success save view block %u_%u_%lu",
            sharding_id, pool_index, block_height);
    }

    bool GetViewBlockInfo(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t block_height,
            view_block::protobuf::ViewBlockItem* pb_view_block) {
        std::string key;
        key.reserve(32);
        key.append(kViewBlockInfoPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&block_height, sizeof(block_height));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!pb_view_block->ParseFromString(val)) {
            return false;
        }

        ZJC_DEBUG("success get view block %u_%u_%lu",
            sharding_id, pool_index, block_height);
        return true;
    }

    bool HasViewBlockInfo(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t block_height) {
        std::string key;
        key.reserve(32);
        key.append(kViewBlockInfoPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&block_height, sizeof(block_height));
        return db_->Exist(key);
    }

    void SaveHeightTree(
            uint32_t net_id,
            uint32_t pool_index,
            uint32_t level,
            uint64_t node_index,
            const sync::protobuf::FlushDbItem& flush_db,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kHeightTreePrefix);
        key.append((char*)&net_id, sizeof(net_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&level, sizeof(level));
        key.append((char*)&node_index, sizeof(node_index));
        db_batch.Put(key, flush_db.SerializeAsString());
    }

    bool GetHeightTree(
            uint32_t net_id,
            uint32_t pool_index,
            uint32_t level,
            uint64_t node_index,
            sync::protobuf::FlushDbItem* flush_db) {
        std::string key;
        key.reserve(64);
        key.append(kHeightTreePrefix);
        key.append((char*)&net_id, sizeof(net_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&level, sizeof(level));
        key.append((char*)&node_index, sizeof(node_index));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!flush_db->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    bool JustCheckGidExists(const std::string& gid) {
        std::string key = kGidPrefix + gid;
        if (db_->Exist(key)) {
            return true;
        }

        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        if (db_batch_[thread_idx].Exist(key)) {
            return true;
        }

        return false;
    }

    bool GidExists(const std::string& gid) {
        std::string key = kGidPrefix + gid;
        if (db_->Exist(key)) {
            return true;
        }

        auto thread_idx = common::GlobalInfo::Instance()->get_thread_index();
        if (db_batch_[thread_idx].Exist(key)) {
            return true;
        }

        db_batch_[thread_idx].Put(key, "1");
        return false;
    }

    void SaveContractUserPrepayment(
            const std::string& contract_addr,
            const std::string& user_addr,
            uint64_t height,
            uint64_t prepayment,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kContractGasPrepaymentPrefix);
        key.append(contract_addr);
        key.append(user_addr);
        char data[16];
        uint64_t* tmp = (uint64_t*)data;
        tmp[0] = height;
        tmp[1] = prepayment;
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
        ZJC_DEBUG("success save contract user prepayment: %s, %lu, %lu",
            common::Encode::HexEncode(key).c_str(), height, prepayment);
    }

    bool GetContractUserPrepayment(
            const std::string& contract_addr,
            const std::string& user_addr,
            uint64_t* height,
            uint64_t* prepayment) {
        std::string key;
        key.reserve(128);
        key.append(kContractGasPrepaymentPrefix);
        key.append(contract_addr);
        key.append(user_addr);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get contract user prepayment failed: %s, %lu, %lu",
                common::Encode::HexEncode(key).c_str(), *height, *prepayment);
            return false;
        }

        uint64_t* data = (uint64_t*)val.c_str();
        *height = data[0];
        *prepayment = data[1];
        return true;
    }

    bool ExistsBlsVerifyG2(const std::string& id) {
        std::string key = kBlsVerifyPrefex + id;
        return db_->Exist(key);
    }
    
    void AddBlsVerifyG2(
            const std::string& id,
            const bls::protobuf::VerifyVecBrdReq& verfy_req) {
        std::string key = kBlsVerifyPrefex + id;
        std::string val = verfy_req.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    void AddBlsVerifyG2(
            const std::string& id,
            const bls::protobuf::VerifyVecBrdReq& verfy_req,
            db::DbWriteBatch& db_batch) {
        std::string key = kBlsVerifyPrefex + id;
        std::string val = verfy_req.SerializeAsString();
        db_batch.Put(key, val);
    }

    bool GetBlsVerifyG2(
            const std::string& id,
            bls::protobuf::VerifyVecBrdReq* verfy_req) {
        std::string key = kBlsVerifyPrefex + id;
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!verfy_req->ParseFromString(val)) {
            assert(false);
            return false;
        }

        return true;
    }

    void SaveBlsPrikey(
            uint64_t elect_height,
            uint32_t sharding_id,
            const std::string& node_addr,
            const std::string& bls_prikey) {
        std::string key;
        key.reserve(48);
        key.append(kBlsPrivateKeyPrefix);
        key.append((char*)&elect_height, sizeof(elect_height));
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append(node_addr);
        auto st = db_->Put(key, bls_prikey);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
        
        ZJC_DEBUG("save bls success: %lu, %u, %s", elect_height,
            sharding_id,
            common::Encode::HexEncode(node_addr).c_str());
    }

    bool GetBlsPrikey(
            std::shared_ptr<security::Security>& security_ptr,
            uint64_t elect_height,
            uint32_t sharding_id,
            std::string* bls_prikey) {
        std::string key;
        key.reserve(48);
        key.append(kBlsPrivateKeyPrefix);
        key.append((char*)&elect_height, sizeof(elect_height));
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append(security_ptr->GetAddress());
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get bls failed: %lu, %u, %s",
                elect_height,
                sharding_id,
                common::Encode::HexEncode(security_ptr->GetAddress()).c_str());
            return false;
        }

        if (security_ptr->Decrypt(
                val,
                security_ptr->GetPrikey(),
                bls_prikey) != security::kSecuritySuccess) {
            return false;
        }

        ZJC_DEBUG("save bls success: %lu, %u, %s", elect_height,
            sharding_id,
            common::Encode::HexEncode(security_ptr->GetAddress()).c_str());
        return true;
    }

    // 用于保存 agg bls 的私钥，目前私钥与 elect_height 无关
    void SaveAggBlsPrikey(
            std::shared_ptr<security::Security>& security_ptr,
            const libff::alt_bn128_Fr& bls_prikey) {
        std::string key;
        key.reserve(32);
        key.append(kAggBlsPrivateKeyPrefix);
        key.append(security_ptr->GetAddress());

        std::string enc_data;
        std::string sec_key = libBLS::ThresholdUtils::fieldElementToString(bls_prikey);
        if (security_ptr->Encrypt(
                    sec_key,
                    security_ptr->GetPrikey(),
                    &enc_data) != security::kSecuritySuccess) {
            return;
        }        
        
        auto st = db_->Put(key, enc_data);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool GetAggBlsPrikey(
            std::shared_ptr<security::Security>& security_ptr,
            libff::alt_bn128_Fr* bls_prikey) {
        std::string key;
        key.reserve(32);
        key.append(kAggBlsPrivateKeyPrefix);
        key.append(security_ptr->GetAddress());
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get agg bls failed: %s",
                common::Encode::HexEncode(security_ptr->GetAddress()).c_str());
            return false;
        }

        std::string prikey_str;
        if (security_ptr->Decrypt(
                val,
                security_ptr->GetPrikey(),
                &prikey_str) != security::kSecuritySuccess) {
            return false;
        }

        ZJC_DEBUG("save agg bls success: %s",
            common::Encode::HexEncode(security_ptr->GetAddress()).c_str());

        *bls_prikey = libff::alt_bn128_Fr(prikey_str.c_str());
        return true;
    }    

    void SaveBlsVerifyValue(
            const std::string& id,
            uint32_t idx,
            uint32_t valid_t,
            const libff::alt_bn128_G2& verfiy) {
        std::string key;
        key.reserve(128);
        key.append(kBlsVerifyValuePrefix);
        key.append(id);
        key.append((char*)&idx, sizeof(idx));
        key.append((char*)&valid_t, sizeof(valid_t));
        elect::protobuf::VerifyVecValue verfiy_value;
        verfiy_value.set_x_c0(libBLS::ThresholdUtils::fieldElementToString(verfiy.X.c0));
        verfiy_value.set_x_c1(libBLS::ThresholdUtils::fieldElementToString(verfiy.X.c1));
        verfiy_value.set_y_c0(libBLS::ThresholdUtils::fieldElementToString(verfiy.Y.c0));
        verfiy_value.set_y_c1(libBLS::ThresholdUtils::fieldElementToString(verfiy.Y.c1));
        verfiy_value.set_z_c0(libBLS::ThresholdUtils::fieldElementToString(verfiy.Z.c0));
        verfiy_value.set_z_c1(libBLS::ThresholdUtils::fieldElementToString(verfiy.Z.c1));
        verfiy_value.set_valid_t(valid_t);
        std::string val = verfiy_value.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool GetBlsVerifyValue(
            const std::string& id,
            uint32_t idx,
            uint32_t max_t,
            uint32_t* valid_t,
            libff::alt_bn128_G2* verfiy) {
        for (int32_t i = (int32_t)max_t; i > 0; --i) {
            std::string key;
            key.reserve(128);
            key.append(kBlsVerifyValuePrefix);
            key.append(id);
            key.append((char*)&idx, sizeof(idx));
            key.append((char*)&i, sizeof(i));
            if (!db_->Exist(key)) {
                continue;
            }

            std::string val;
            auto st = db_->Get(key, &val);
            if (!st.ok()) {
                ZJC_ERROR("write db failed!");
                return false;
            }

            elect::protobuf::VerifyVecValue item;
            if (!item.ParseFromString(val)) {
                return false;
            }

            auto x_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c0()).c_str());
            auto x_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.x_c1()).c_str());
            auto x_coord = libff::alt_bn128_Fq2(x_c0, x_c1);
            auto y_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c0()).c_str());
            auto y_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.y_c1()).c_str());
            auto y_coord = libff::alt_bn128_Fq2(y_c0, y_c1);
            auto z_c0 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c0()).c_str());
            auto z_c1 = libff::alt_bn128_Fq(common::Encode::HexEncode(item.z_c1()).c_str());
            auto z_coord = libff::alt_bn128_Fq2(z_c0, z_c1);
            *verfiy = libff::alt_bn128_G2(x_coord, y_coord, z_coord);
            *valid_t = i;
            return true;
        }
        
        return false;
    }

    void SavePresetPolynomial(const bls::protobuf::LocalBlsItem& bls_polynomial) {
        std::string key = kPresetPolynomialPrefix;
        std::string val = bls_polynomial.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool GetPresetPolynomial(bls::protobuf::LocalBlsItem* bls_polynomial) {
        std::string key = kPresetPolynomialPrefix;
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_ERROR("write db failed!");
            return false;
        }

        if (!bls_polynomial->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SavePresetVerifyValue(
            uint32_t idx, uint32_t pos, const bls::protobuf::BlsVerifyValue& verify_val) {
        std::string key;
        key.reserve(64);
        key.append(kPresetPolynomialPrefix);
        key.append((char*)&idx, sizeof(idx));
        key.append((char*)&pos, sizeof(pos));
        std::string val = verify_val.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool ExistsPresetVerifyValue(uint32_t idx, uint32_t pos) {
        std::string key;
        key.reserve(64);
        key.append(kPresetPolynomialPrefix);
        key.append((char*)&idx, sizeof(idx));
        key.append((char*)&pos, sizeof(pos));
        return db_->Exist(key);
    }

    bool GetPresetVerifyValue(
            uint32_t idx,
            uint32_t pos,
            bls::protobuf::BlsVerifyValue* verify_val) {
        std::string key;
        key.reserve(64);
        key.append(kPresetPolynomialPrefix);
        key.append((char*)&idx, sizeof(idx));
        key.append((char*)&pos, sizeof(pos));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_ERROR("write db failed!");
            return false;
        }

        if (!verify_val->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveStatisticLatestHeihgts(
            uint32_t sharding_id,
            const pools::protobuf::StatisticTxItem& to_heights) {
        std::string key;
        key.reserve(64);
        key.append(kPresetPolynomialPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val = to_heights.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool GetStatisticLatestHeihgts(
            uint32_t sharding_id,
            pools::protobuf::StatisticTxItem* to_heights) {
        std::string key;
        key.reserve(64);
        key.append(kPresetPolynomialPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_WARN("get data from db failed!");
            return false;
        }

        if (!to_heights->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveConsensusedStatisticTimeBlockHeight(
            uint32_t sharding_id,
            uint64_t timeblock_height,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kConsensusedStatisticPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val((char*)&timeblock_height, sizeof(timeblock_height));
        db_batch.Put(key, val);
    }

    bool GetConsensusedStatisticTimeBlockHeight(uint32_t sharding_id, uint64_t* timeblock_height) {
        std::string key;
        key.reserve(64);
        key.append(kConsensusedStatisticPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_ERROR("write db failed!");
            return false;
        }

        if (val.size() != sizeof(*timeblock_height)) {
            return false;
        }

        uint64_t* tm_height = (uint64_t*)val.c_str();
        *timeblock_height = tm_height[0];
        return true;
    }

    void SaveStatisticedShardingHeight(
            uint32_t sharding_id,
            uint64_t tm_height,
            const pools::protobuf::ElectStatistic& elect_statistic,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kRootStatisticedPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&tm_height, sizeof(tm_height));
        std::string val = elect_statistic.SerializeAsString();
        db_batch.Put(key, val);
    }

    bool GetStatisticedShardingHeight(
            uint32_t sharding_id,
            uint64_t tm_height,
            pools::protobuf::ElectStatistic* elect_statistic) {
        std::string key;
        key.reserve(64);
        key.append(kRootStatisticedPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&tm_height, sizeof(tm_height));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_WARN("get data from db failed!");
            return false;
        }

        if (!elect_statistic->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    bool ExistsStatisticedShardingHeight(
            uint32_t sharding_id,
            uint64_t tm_height) {
        std::string key;
        key.reserve(64);
        key.append(kRootStatisticedPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&tm_height, sizeof(tm_height));
        return db_->Exist(key);
    }

    void SaveElectNodeStoke(
            const std::string& id,
            uint64_t elect_height,
            uint64_t balance,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kElectNodesStokePrefix);
        key.append(id);
        key.append((char*)&elect_height, sizeof(elect_height));
        char data[8];
        uint64_t* tmp = (uint64_t*)data;
        tmp[0] = balance;
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
    }

    void GetElectNodeMinStoke(uint32_t sharding_id, const std::string& id, uint64_t* stoke) {
        std::string key;
        key.reserve(64);
        key.append(kSaveLatestElectHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        auto st = db_->Get(key, &val);
        std::set<uint64_t> height_set;
        if (st.ok()) {
            uint64_t* tmp = (uint64_t*)val.c_str();
            uint32_t len = val.size() / 8;
            for (uint32_t i = 0; i < len; ++i) {
                height_set.insert(tmp[i]);
            }
        }

        for (auto hiter = height_set.begin(); hiter != height_set.end(); ++hiter) {
            std::string key;
            key.reserve(64);
            key.append(kElectNodesStokePrefix);
            key.append(id);
            uint64_t elect_height = *hiter;
            key.append((char*)&elect_height, sizeof(elect_height));
            std::string val;
            auto st = db_->Get(key, &val);
            if (!st.ok()) {
                continue;
            }

            uint64_t* balance = (uint64_t*)val.c_str();
            if (balance[0] < *stoke || *stoke == 0) {
                *stoke = balance[0];
            }
        }
    }

    void SaveAddressPubkey(const std::string& id, const std::string& pubkey) {
        std::string key;
        key.reserve(64);
        key.append(kAddressPubkeyPrefex);
        key.append(id);
        auto st = db_->Put(key, pubkey);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool GetAddressPubkey(const std::string& id, std::string* pubkey) {
        std::string key;
        key.reserve(64);
        key.append(kAddressPubkeyPrefex);
        key.append(id);
        auto st = db_->Get(key, pubkey);
        if (!st.ok()) {
            ZJC_ERROR("write db failed!");
            return false;
        }

        return true;
    }

    void SaveJoinShard(uint32_t sharding_id, uint32_t des_sharding_id) {
        std::string key;
        key.reserve(64);
        key.append(kSaveChoosedJoinShardPrefix);
        char data[8];
        uint32_t* tmp = (uint32_t*)data;
        tmp[0] = sharding_id;
        tmp[1] = des_sharding_id;
        std::string val(data, sizeof(data));
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool GetJoinShard(uint32_t* sharding_id, uint32_t* des_sharding_id) {
        std::string key;
        key.reserve(64);
        key.append(kSaveChoosedJoinShardPrefix);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        uint32_t* tmp = (uint32_t*)val.c_str();
        *sharding_id = tmp[0];
        *des_sharding_id = tmp[1];
        return true;
    }

    void SaveGenesisTimeblock(uint64_t block_height, uint64_t genesis_tm, db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kGenesisTimeblockPrefix);
        char data[8];
        uint32_t* tmp = (uint32_t*)data;
        tmp[0] = block_height;
        tmp[1] = genesis_tm;
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
    }

    bool GetGenesisTimeblock(uint64_t* block_height, uint64_t* genesis_tm) {
        std::string key;
        key.reserve(64);
        key.append(kGenesisTimeblockPrefix);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_ERROR("get db failed!");
            return false;
        }

        if (val.size() != 16) {
            return false;
        }

        uint32_t* tmp = (uint32_t*)val.c_str();
        *block_height = tmp[0];
        *genesis_tm = tmp[1];
        return true;
    }

    void SaveLocalPolynomial(
            std::shared_ptr<security::Security>& security_ptr,
            const std::string& id,
            const bls::protobuf::LocalPolynomial& local_poly,
            bool for_temp = false) {
        std::string key;
        key.reserve(128);
        if (for_temp) {
            key.append(kLocalTempPolynomialPrefix);
        } else {
            key.append(kLocalPolynomialPrefix);
        }        key.append(id);
        std::string tmp_val = local_poly.SerializeAsString();
        char tmp_data[4];
        uint32_t* tmp = (uint32_t*)tmp_data;
        tmp[0] = tmp_val.size();
        std::string val = std::string(tmp_data, sizeof(tmp_data)) + tmp_val;
        std::string enc_data;
        if (security_ptr->Encrypt(
                val,
                security_ptr->GetPrikey(),
                &enc_data) != security::kSecuritySuccess) {
            ZJC_FATAL("encrypt data failed!");
            return;
        }

        auto st = db_->Put(key, enc_data);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    void SaveLocalPolynomial(
            std::shared_ptr<security::Security>& security_ptr,
            const std::string& id,
            const bls::protobuf::LocalPolynomial& local_poly,
            bool for_temp,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        if (for_temp) {
            key.append(kLocalTempPolynomialPrefix);
        } else {
            key.append(kLocalPolynomialPrefix);
        }        key.append(id);
        std::string tmp_val = local_poly.SerializeAsString();
        char tmp_data[4];
        uint32_t* tmp = (uint32_t*)tmp_data;
        tmp[0] = tmp_val.size();
        std::string val = std::string(tmp_data, sizeof(tmp_data)) + tmp_val;
        std::string enc_data;
        if (security_ptr->Encrypt(
                val,
                security_ptr->GetPrikey(),
                &enc_data) != security::kSecuritySuccess) {
            ZJC_FATAL("encrypt data failed!");
            return;
        }

        db_batch.Put(key, enc_data);
    }

    bool GetLocalPolynomial(
            std::shared_ptr<security::Security>& security_ptr,
            const std::string& id,
            bls::protobuf::LocalPolynomial* local_poly,
            bool for_temp = false) {
        std::string key;
        key.reserve(128);
        if (for_temp) {
            key.append(kLocalTempPolynomialPrefix);
        } else {
            key.append(kLocalPolynomialPrefix);
        }
        key.append(id);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
//             assert(false);
            ZJC_ERROR("get db failed!");
            return false;
        }

        std::string dec_data;
        if (security_ptr->Decrypt(
                val,
                security_ptr->GetPrikey(),
                &dec_data) != security::kSecuritySuccess) {
            assert(false);
            ZJC_ERROR("decrypt db failed!");
            return false;
        }

        uint32_t* tmp = (uint32_t*)dec_data.c_str();
        if (!local_poly->ParseFromArray(dec_data.c_str() + 4, tmp[0])) {
            assert(false);
            ZJC_ERROR("parse db failed!");
            return false;
        }

        return true;
    }

    void SaveVerifiedG2s(
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t valid_t,
            const bls::protobuf::JoinElectBlsInfo& verfy_final_vals,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kLocalVerifiedG2Prefix);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&valid_t, sizeof(valid_t));
        key.append(id);
        std::string val = verfy_final_vals.SerializeAsString();
        db_batch.Put(key, val);
    }

    void SaveVerifiedG2s(
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t valid_t,
            const bls::protobuf::JoinElectBlsInfo& verfy_final_vals) {
        std::string key;
        key.reserve(128);
        key.append(kLocalVerifiedG2Prefix);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&valid_t, sizeof(valid_t));
        key.append(id);
        std::string val = verfy_final_vals.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    void DeleteVerifiedG2s(
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t valid_t) {
        std::string key;
        key.reserve(128);
        key.append(kLocalVerifiedG2Prefix);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&valid_t, sizeof(valid_t));
        key.append(id);
        db_->Delete(key);
    }

    bool GetVerifiedG2s(
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t valid_t,
            bls::protobuf::JoinElectBlsInfo* verfy_final_vals) {
        std::string key;
        key.reserve(128);
        key.append(kLocalVerifiedG2Prefix);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&valid_t, sizeof(valid_t));
        key.append(id);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get data from db failed local_member_idx: %u, valid_t: %u, id: %s",
                local_member_idx, valid_t, common::Encode::HexEncode(id).c_str());
            return false;
        }

        if (!verfy_final_vals->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveNodeVerificationVector(
            const std::string& addr,
            const bls::protobuf::JoinElectInfo& join_info,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kNodeVerificationVectorPrefix);
        key.append(addr);
        std::string val = join_info.SerializeAsString();
        db_batch.Put(key, val);
    }

    bool GetNodeVerificationVector(
            const std::string& addr,
            bls::protobuf::JoinElectInfo* join_info) {
        std::string key;
        key.reserve(128);
        key.append(kNodeVerificationVectorPrefix);
        key.append(addr);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_ERROR("get data from db failed!");
            return false;
        }

        if (!join_info->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveLocalElectPos(const std::string& addr, uint32_t pos, db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kNodeLocalElectPosPrefix);
        key.append(addr);
        char data[4];
        uint32_t* tmp = (uint32_t*)data;
        tmp[0] = pos;
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
    }

    void SaveLocalElectPos(const std::string& addr, uint32_t pos) {
        std::string key;
        key.reserve(128);
        key.append(kNodeLocalElectPosPrefix);
        key.append(addr);
        char data[4];
        uint32_t* tmp = (uint32_t*)data;
        tmp[0] = pos;
        std::string val(data, sizeof(data));
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool GetLocalElectPos(const std::string& addr, uint32_t* pos) {
        std::string key;
        key.reserve(128);
        key.append(kNodeLocalElectPosPrefix);
        key.append(addr);
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_INFO("get db failed!");
            return false;
        }

        uint32_t* tmp = (uint32_t*)val.c_str();
        *pos = tmp[0];
        return true;
    }

    void SaveCheckCrossHeight(
            uint32_t local_shard,
            uint32_t des_shard,
            uint64_t height,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kCrossCheckHeightPrefix);
        key.append((char*)&local_shard, sizeof(local_shard));
        key.append((char*)&des_shard, sizeof(des_shard));
        char data[8];
        uint64_t* tmp = (uint64_t*)data;
        tmp[0] = height;
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
    }

    bool GetCheckCrossHeight(
            uint32_t local_shard,
            uint32_t des_shard,
            uint64_t* height) {
        std::string key;
        key.reserve(128);
        key.append(kCrossCheckHeightPrefix);
        key.append((char*)&local_shard, sizeof(local_shard));
        key.append((char*)&des_shard, sizeof(des_shard));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_INFO("get db failed!");
            return false;
        }

        uint64_t* tmp = (uint64_t*)val.c_str();
        *height = tmp[0];
        return true;
    }

    void SaveElectHeightCommonPk(
            uint32_t des_shard,
            uint64_t height,
            const elect::protobuf::PrevMembers& prv_info,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kElectHeightWithBlsCommonPkPrefix);
        key.append((char*)&des_shard, sizeof(des_shard));
        key.append((char*)&height, sizeof(height));
        std::string val = prv_info.SerializeAsString();
        db_batch.Put(key, val);
        ZJC_DEBUG("save elect height prev info success: %u, %lu", des_shard, height);
        assert(prv_info.has_common_pubkey());
        assert(!prv_info.common_pubkey().x_c0().empty());
    }

    bool GetElectHeightCommonPk(
            uint32_t des_shard,
            uint64_t height,
            elect::protobuf::PrevMembers* prv_info) {
        std::string key;
        key.reserve(128);
        key.append(kElectHeightWithBlsCommonPkPrefix);
        key.append((char*)&des_shard, sizeof(des_shard));
        key.append((char*)&height, sizeof(height));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get elect height prev info failed: %u, %lu", des_shard, height);
            return false;
        }

        if (!prv_info->ParseFromString(val)) {
            ZJC_DEBUG("get elect height prev info failed: %u, %lu", des_shard, height);
            return false;
        }

        ZJC_DEBUG("get elect height prev info success: %u, %lu", des_shard, height);
        assert(prv_info->has_common_pubkey());
        assert(!prv_info->common_pubkey().x_c0().empty());
        return true;
    }

    void SaveHeightInvalidHashs(
            uint32_t shard_id,
            uint32_t pool_index,
            uint64_t height,
            const std::set<std::string>& hashs) {
        std::string key;
        key.reserve(128);
        key.append(kBftInvalidHeightHashs);
        key.append((char*)&shard_id, sizeof(shard_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        std::string val;
        for (auto iter = hashs.begin(); iter != hashs.end(); ++iter) {
            val += *iter;
        }

        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }

        ZJC_DEBUG("save height invalid hashs success: %u, %u, %lu, %s",
            shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
    }

    void SaveHeightInvalidHashs(
            uint32_t shard_id,
            uint32_t pool_index,
            uint64_t height,
            const std::set<std::string>& hashs,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(128);
        key.append(kBftInvalidHeightHashs);
        key.append((char*)&shard_id, sizeof(shard_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        std::string val;
        for (auto iter = hashs.begin(); iter != hashs.end(); ++iter) {
            val += *iter;
        }

        db_batch.Put(key, val);
        ZJC_DEBUG("save height invalid hashs success: %u, %u, %lu, %s",
            shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
    }

    bool GetHeightInvalidHashs(
            uint32_t shard_id,
            uint32_t pool_index,
            uint64_t height,
            std::set<std::string>* hashs) {
        std::string key;
        key.reserve(128);
        key.append(kBftInvalidHeightHashs);
        key.append((char*)&shard_id, sizeof(shard_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get height invalid hashs failed: %u, %u, %lu, %s",
                shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
            return false;
        }

        auto count = val.size() / 32;
        for (uint32_t i = 0; i < count; ++i) {
            std::string tmp_hash(val.c_str() + i * 32, 32);
            hashs->insert(tmp_hash);
        }

        ZJC_DEBUG("get height invalid hashs success: %u, %u, %lu, %s",
            shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
        return true;
    }

    void SaveTempHeightInvalidHashs(
            uint32_t shard_id,
            uint32_t pool_index,
            uint64_t height,
            const std::set<std::string>& hashs) {
        std::string key;
        key.reserve(128);
        key.append(kTempBftInvalidHeightHashs);
        key.append((char*)&shard_id, sizeof(shard_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        std::string val;
        for (auto iter = hashs.begin(); iter != hashs.end(); ++iter) {
            val += *iter;
        }

        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }

        ZJC_DEBUG("save height invalid hashs success: %u, %u, %lu, %s",
            shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
    }

    bool GetTempHeightInvalidHashs(
            uint32_t shard_id,
            uint32_t pool_index,
            uint64_t height,
            std::set<std::string>* hashs) {
        std::string key;
        key.reserve(128);
        key.append(kTempBftInvalidHeightHashs);
        key.append((char*)&shard_id, sizeof(shard_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            ZJC_DEBUG("get height invalid hashs failed: %u, %u, %lu, %s",
                shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
            return false;
        }

        auto count = val.size() / 32;
        for (uint32_t i = 0; i < count; ++i) {
            std::string tmp_hash(val.c_str() + i * 32, 32);
            hashs->insert(tmp_hash);
        }

        ZJC_DEBUG("get height invalid hashs success: %u, %u, %lu, %s",
            shard_id, pool_index, height, common::Encode::HexEncode(val).c_str());
        return true;
    }

private:
    void DumpGidToDb() {
        if (!dumped_gid_) {
            for (uint8_t i = 0; i < common::kMaxThreadCount; ++i) {
                db_->Put(db_batch_[i]);
            }

            dumped_gid_ = true;
        }

        db_batch_tick_.CutOff(
            1000000lu,
            std::bind(&PrefixDb::DumpGidToDb, this));
    }

    static const uint32_t kSaveElectHeightCount = 4u;

    std::shared_ptr<db::Db> db_ = nullptr;
    db::DbWriteBatch db_batch_[common::kMaxThreadCount];
    uint64_t prev_gid_tm_us_ = 0;
    common::Tick db_batch_tick_;
    volatile bool dumped_gid_ = false;

    DISALLOW_COPY_AND_ASSIGN(PrefixDb);
};

};  // namespace protos

};  // namespace shardora
