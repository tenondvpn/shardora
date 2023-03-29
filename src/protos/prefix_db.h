#pragma once

#include <evmc/evmc.hpp>
#include <libff/algebra/curves/alt_bn128/alt_bn128_init.hpp>
#include <libbls/tools/utils.h>

#include "common/encode.h"
#include "common/hash.h"
#include "common/string_utils.h"
#include "common/tick.h"
#include "common/time_utils.h"
#include "db/db.h"
#include "protos/address.pb.h"
#include "protos/block.pb.h"
#include "protos/bls.pb.h"
#include "protos/elect.pb.h"
#include "protos/sync.pb.h"
#include "protos/timeblock.pb.h"
#include "security/security.h"

namespace zjchain {

namespace protos {

typedef std::shared_ptr<address::protobuf::AddressInfo> AddressInfoPtr;
static const std::string kFieldBytesCode = "__bytes_code";
static const std::string kGenesisElectPrikeyEncryptKey = common::Encode::HexDecode(
    "17dfdd4d49509691361225e9059934675dea440d123aa8514441aa6788354016");

static const std::string kRootCreateNewAccountAttrKey("kRootNewAddr_netid");

// transaction contract attr keys
static const std::string kContractAddress = "__caddress";
static const std::string kContractBytesCode = "__cbytescode";
static const std::string kContractSourceCode = "__csourcecode";
static const std::string kContractBytesStartCode = "60806040";
static const std::string kContractName = "__ctname";
static const std::string kContractDesc = "__ctdesc";
static const std::string kContractCreatedBytesCode = "__cbytescodecreated";
static const std::string kContractInputCode = "__cinput";
static const std::string kContractCallerbalance = "__ccontractcallerbalance";
static const std::string kContractCallerChangeAmount = "__ccontractcallerchangeamount";
static const std::string kContractCallerGasUsed = "__ccontractcallergasused";
static const std::string kStatisticAttr = "__statisticattr";
static const std::string kNormalTos = "__normal_tos";
static const std::string kLocalNormalTos = "__local_tos";
static const std::string kConsensusLocalNormalTos = "__consensus_local_tos";

static const std::string kAddressPrefix = "a\x01";
static const std::string kBlsVerifyPrefex = "b\x01";
static const std::string kBlsSwapKeyPrefex = "c\x01";
static const std::string kAddressStorageKeyPrefex = "d\x01";
static const std::string kBlsSecKeyPrefex = "e\x01";
static const std::string kShardingIdPrefex = "f\x01";
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

static const std::string kTemporaryKeyPrefix = "t\x01";

class PrefixDb {
public:
    PrefixDb(const std::shared_ptr<db::Db>& db_ptr) : db_(db_ptr) {
        gid_set_[0].reserve(202400);
        gid_set_[1].reserve(202400);
        db_batch_tick_.CutOff(
            5000000lu,
            std::bind(&PrefixDb::DumpGidToDb, this, std::placeholders::_1));
    }

    ~PrefixDb() {}

    void Destroy() {
        for (int32_t i = 0; i < 2; ++i) {
            if (!gid_set_[i].empty()) {
                db_->Put(db_batch[i]);
                db_batch[i].Clear();
                gid_set_[i].clear();
            }
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

    std::shared_ptr<address::protobuf::AddressInfo> GetAddressInfo(const std::string& addr) {
        std::string val;
        auto st = db_->Get(kAddressPrefix + addr, &val);
        if (!st.ok()) {
            return nullptr;
        }

        auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
        if (!addr_info->ParseFromString(val)) {
            return nullptr;
        }

        return addr_info;
    }

    void AddBlsVerifyG2(
            uint32_t local_member_idx,
            const bls::protobuf::VerifyVecItem& verify_item,
            uint64_t height,
            uint32_t local_idx,
            uint32_t peer_idx) {
        std::string key;
        key.reserve(32);
        key.append(kBlsVerifyPrefex);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&height, sizeof(height));
        key.append((char*)&local_idx, sizeof(local_idx));
        key.append((char*)&peer_idx, sizeof(peer_idx));
        ZJC_DEBUG("save g2: %s", common::Encode::HexEncode(key).c_str());
        std::string val = verify_item.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    bool GetBlsVerifyG2(
            uint32_t local_member_idx,
            uint64_t height,
            uint32_t local_idx,
            uint32_t peer_idx,
            bls::protobuf::VerifyVecItem* verify_item) {
        std::string key;
        key.reserve(32);
        key.append(kBlsVerifyPrefex);
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append((char*)&height, sizeof(height));
        key.append((char*)&local_idx, sizeof(local_idx));
        key.append((char*)&peer_idx, sizeof(peer_idx));
        std::string val;
        auto st = db_->Get(key, &val);
        ZJC_DEBUG("get g2: %s, %d", common::Encode::HexEncode(key).c_str(), st.ok());
        if (!st.ok()) {
            return false;
        }

        if (!verify_item->ParseFromString(val)) {
            assert(false);
            return false;
        }

        return true;
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

    evmc::bytes32 GetAddressStorage(
            const evmc::address& addr,
            const evmc::bytes32& k) {
        std::string key;
        key.reserve(128);
        key.append(kAddressStorageKeyPrefex);
        key.append((char*)addr.bytes, sizeof(addr.bytes));
        key.append((char*)k.bytes, sizeof(k.bytes));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return {};
        }

        evmc::bytes32 tmp_val{};
        uint32_t offset = 0;
        uint32_t length = sizeof(tmp_val.bytes);
        if (val.size() < sizeof(tmp_val.bytes)) {
            offset = sizeof(tmp_val.bytes) - val.size();
            length = val.size();
        }

        memcpy(tmp_val.bytes + offset, val.c_str(), length);
        return tmp_val;
    }

    bool GetAddressStorage(
            const evmc::address& addr,
            const std::string& k,
            std::string* val) {
        std::string key;
        key.reserve(128);
        key.append(kAddressStorageKeyPrefex);
        key.append((char*)addr.bytes, sizeof(addr.bytes));
        key.append(k);
        auto st = db_->Get(key, val);
        if (!st.ok()) {
            return false;
        }

        return true;
    }

    uint32_t GetShardingId() {
        std::string key(kShardingIdPrefex + "sharding");
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return common::kInvalidUint32;
        }

        uint32_t sharding_id = common::kInvalidUint32;
        if (!common::StringUtil::ToUint32(val, &sharding_id)) {
            return common::kInvalidUint32;
        }

        return sharding_id;
    }

    void SaveShardingId(uint32_t sharding_id) {
        std::string key(kShardingIdPrefex + "sharding");
        std::string val = std::to_string(sharding_id);
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
    }

    uint32_t GetWaitingId() {
        std::string key(kShardingIdPrefex + "waiting");
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return common::kInvalidUint32;
        }

        uint32_t sharding_id = common::kInvalidUint32;
        if (!common::StringUtil::ToUint32(val, &sharding_id)) {
            return common::kInvalidUint32;
        }

        return sharding_id;
    }

    void SaveWaitingId(uint32_t sharding_id) {
        std::string key(kShardingIdPrefex + "waiting");
        std::string val = std::to_string(sharding_id);
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
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
            ZJC_ERROR("get bls failed: %lu, %u, %s",
                elect_height,
                sharding_id,
                common::Encode::HexEncode(security_ptr->GetAddress()).c_str());
            return false;
        }

        if (elect_height <= 4) {
            // for genesis block with sure encrypt key
            if (security_ptr->Decrypt(
                    val,
                    kGenesisElectPrikeyEncryptKey,
                    bls_prikey) != security::kSecuritySuccess) {
                return false;
            }
        } else {
            if (security_ptr->Decrypt(
                    val,
                    security_ptr->GetPrikey(),
                    bls_prikey) != security::kSecuritySuccess) {
                return false;
            }
        }
        
        return true;
    }

    void SaveLatestElectBlock(const elect::protobuf::ElectBlock& elect_block) {
        std::string key;
        key.reserve(48);
        key.append(kLatestElectBlockPrefix);
        auto sharding_id = elect_block.shard_network_id();
        key.append((char*)&sharding_id, sizeof(sharding_id));
        auto st = db_->Put(key, elect_block.SerializeAsString());
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }

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

    void SaveLatestTimeBlock(uint64_t block_height, uint64_t timestamp, uint64_t vss_random) {
        std::string key(kLatestTimeBlockPrefix);
        timeblock::protobuf::TimeBlock tmblock;
        if (GetLatestTimeBlock(&tmblock)) {
            if (tmblock.height() >= block_height) {
                return;
            }
        }

        tmblock.set_height(block_height);
        tmblock.set_timestamp(timestamp);
        tmblock.set_vss_random(vss_random);
        auto st = db_->Put(key, tmblock.SerializeAsString());
        if (!st.ok()) {
            ZJC_FATAL("write db failed!");
        }
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

        return true;
    }

    bool SaveBlock(const block::protobuf::Block& block, db::DbWriteBatch& batch) {
        if (BlockExists(block.hash())) {
            return false;
        }

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

    bool GetBlockWithHeight(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            block::protobuf::Block* block) {
        std::string block_hash;
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

    void SaveLatestToTxsHeights(
            const pools::protobuf::ToTxHeights& heights,
            db::DbWriteBatch& batch) {
        std::string key;
        key.reserve(48);
        key.append(kLatestToTxsHeightsPrefix);
        uint32_t sharding_id = heights.sharding_id();
        key.append((char*)&sharding_id, sizeof(sharding_id));
        batch.Put(key, heights.SerializeAsString());
    }

    bool GetLatestToTxsHeights(uint32_t sharding_id, pools::protobuf::ToTxHeights* heights) {
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

    bool GidExists(const std::string& gid) {
        auto now_tm = common::TimeUtils::TimestampUs();
        if (!gid_set_.empty() && prev_gid_tm_us_ + 120000000lu < now_tm) {
            valid_index_ = (valid_index_ + 1) % 2;
            gid_set_[valid_index_].clear();
            prev_gid_tm_us_ = now_tm;
        }

        std::string key = kGidPrefix + gid;
        auto iter0 = gid_set_[0].find(key);
        if (iter0 != gid_set_[0].end()) {
            return true;
        }

        auto iter1 = gid_set_[1].find(key);
        if (iter1 != gid_set_[1].end()) {
            return true;
        }

        if (db_->Exist(key)) {
            return true;
        }

        gid_set_[valid_index_].insert(key);
        db_batch[valid_index_].Put(key, "1");
        return false;
    }

private:
    void DumpGidToDb(uint8_t thread_idx) {
        uint32_t index = (valid_index_ + 1) % 2;
        db_->Put(db_batch[index]);
        db_batch[index].Clear();
        db_batch_tick_.CutOff(
            3000000lu,
            std::bind(&PrefixDb::DumpGidToDb, this, std::placeholders::_1));
    }

    std::shared_ptr<db::Db> db_ = nullptr;
    std::unordered_set<std::string> gid_set_[2];
    db::DbWriteBatch db_batch[2];
    uint32_t valid_index_ = 0;
    uint64_t prev_gid_tm_us_ = 0;
    common::Tick db_batch_tick_;

    DISALLOW_COPY_AND_ASSIGN(PrefixDb);
};

};  // namespace protos

};  // namespace zjchain
