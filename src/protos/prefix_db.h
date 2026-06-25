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
#include "consensus/hotstuff/types.h"
#include "db/db.h"
#include "network/network_utils.h"
#include "protos/address.pb.h"
#include "protos/block.pb.h"
#include "protos/bls.pb.h"
#include "protos/c2c.pb.h"
#include "protos/elect.pb.h"
#include "protos/init.pb.h"
#include "protos/sync.pb.h"
#include "protos/timeblock.pb.h"
#include "protos/transport.pb.h"
#include "protos/tx_storage_key.h"
#include "protos/ws.pb.h"
#include "protos/view_block.pb.h"
#include "security/security.h"
#include "shardoravm/shardoravm_utils.h"

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
static const std::string kBlsPrivateKeyPrefix = "g\x01";
static const std::string kLatestElectBlockPrefix = "h\x01";
static const std::string kLatestTimeBlockPrefix = "i\x01";
static const std::string kBlockHeightPrefix = "j\x01";
static const std::string kBlockPrefix = "k\x01";
static const std::string kLatestToTxsHeightsPrefix = "m\x01";
static const std::string kLatestPoolPrefix = "n\x01";
static const std::string kHeightTreePrefix = "o\x01";
static const std::string kGidPrefix = "p\x01";
static const std::string kBlsInfoPrefix = "r\x01";
static const std::string kTemporaryKeyPrefix = "t\x01";
static const std::string kPresetVerifyValuePrefix = "v\x01";
static const std::string kStatisticHeightsPrefix = "w\x01";
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
static const std::string kStakeInfoPrefix = "ak\x01";
static const std::string kViewBlockInfoPrefix = "an\x01";

static const std::string kBandwidthPrefix = "ao\x01";
static const std::string kC2cSelloutPrefix = "ap\x01";
static const std::string kC2cSellorderPrefix = "aq\x01";
static const std::string kSaveHavePrevLatestElectHeightPrefix = "ar\x01";
static const std::string kLatestToTxBlock = "as\x01";
static const std::string kLatestPoolStatisticTagPrefix = "at\x01";
static const std::string kViewBlockHashKeyPrefix = "au\x01";
static const std::string kViewBlockParentHashKeyPrefix = "av\x01";
static const std::string kAggBlsPrivateKeyPrefix = "ax\x01";
static const std::string kCommitedGidPrefix = "ay\x01";
static const std::string kHighViewBlockPrefix = "aw\x01";
static const std::string kViewBlockVaildParentHash = "ba\x01";
static const std::string kBlockVaildHeight = "bb\x01";
static const std::string kUserTxPrefix = "bc\x01";
static const std::string kUserTxGidPrefix = "bd\x01";
static const std::string kElectHeightWithElectBlock = "bd\x02";
static const std::string kOverUniqueHash = "be\x02";
static const std::string kLeaderLatestProposeMessage = "bf\x02";

class PrefixDb {
public:
    struct UserTxItem {
        uint64_t height = 0;
        uint32_t tx_index = 0;
        block::protobuf::BlockTx tx;
    };

    PrefixDb(const std::shared_ptr<db::Db>& db_ptr) : db_(db_ptr) {
    }

    ~PrefixDb() {
        Destroy();
    }

    void InitGidManager() {
        // db_batch_tick_.CutOff(
        //     5000000lu,
        //     std::bind(&PrefixDb::DumpGidToDb, this));
    }

    void Destroy() {
    }

    void AddAddressInfo(
            const std::string& addr,
            const address::protobuf::AddressInfo& addr_info,
            db::DbWriteBatch& db_batch) {
        db_batch.Put(kAddressPrefix + addr, addr_info.SerializeAsString());
    }

    void AddNowElectHeight2Plege(const std::string& addr , const uint64_t height , db::DbWriteBatch& db_batch) {
        auto key = common::Encode::HexDecode(addr) + common::Encode::HexDecode("0000000000000000000000000000000000000000000000000000000000000000");
        evmc::bytes32 tmp_val{};
        shardoravm::Uint64ToEvmcBytes32(tmp_val, height);

        auto value =  std::string((char*)tmp_val.bytes, sizeof(tmp_val.bytes));
        SaveTemporaryKv(key, value, db_batch);
    }

    std::shared_ptr<address::protobuf::AddressInfo> GetAddressInfo(const std::string& addr) {
        std::string val;
        auto st = db_->Get(kAddressPrefix + addr, &val);
        if (!st.ok()) {
            SHARDORA_DEBUG("failed get addr: %s", common::Encode::HexEncode(addr).c_str());
            return nullptr;
        }

        auto addr_info = std::make_shared<address::protobuf::AddressInfo>();
        if (!addr_info->ParseFromString(val)) {
            SHARDORA_DEBUG("failed parse addr: %s", common::Encode::HexEncode(addr).c_str());
            return nullptr;
        }

        SHARDORA_DEBUG("success get addr: %s", common::Encode::HexEncode(addr).c_str());
        return addr_info;
    }

    void SaveSwapKey(
            uint32_t sharding_id,
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t peer_idx,
            const std::string& seckey,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(32);
        key.append(kBlsSwapKeyPrefex);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append(id);
        key.append((char*)&peer_idx, sizeof(peer_idx));
        db_batch.Put(key, seckey);
    }

    void SaveSwapKey(
            uint32_t sharding_id,
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t peer_idx,
            const std::string& seckey) {
        std::string key;
        key.reserve(32);
        key.append(kBlsSwapKeyPrefex);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append(id);
        key.append((char*)&peer_idx, sizeof(peer_idx));
        SHARDORA_DEBUG("save ttttt swap key: %u, id: %s, %u",
            local_member_idx, common::Encode::HexEncode(id).c_str(), peer_idx);
        auto st = db_->Put(key, seckey);
        if (!st.ok()) {
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
        }
    }

    bool GetSwapKey(
            uint32_t sharding_id,
            uint32_t local_member_idx,
            const std::string& id,
            uint32_t peer_idx,
            std::string* seckey) {
        std::string key;
        key.reserve(32);
        key.append(kBlsSwapKeyPrefex);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&local_member_idx, sizeof(local_member_idx));
        key.append(id);
        key.append((char*)&peer_idx, sizeof(peer_idx));
        SHARDORA_DEBUG("get ttttt swap key: %u, %s, %u",
            local_member_idx, common::Encode::HexEncode(id).c_str(), peer_idx);
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

        if (elect_block.has_prev_members()) {
            key.clear();
            key.reserve(64);
            key.append(kSaveHavePrevLatestElectHeightPrefix);
            key.append((char*)&sharding_id, sizeof(sharding_id));
            db_batch.Put(key, elect_block.SerializeAsString());
        }

        SHARDORA_DEBUG("save elect block sharding id: %u, height: %lu",
            sharding_id, elect_block.elect_height());
    }

    bool GetHavePrevlatestElectBlock(
            uint32_t sharding_id,
            elect::protobuf::ElectBlock* elect_block) {
        std::string key;
        key.reserve(48);
        key.append(kSaveHavePrevLatestElectHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        SHARDORA_DEBUG("get elect block sharding id: %u",
            sharding_id);
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!elect_block->ParseFromString(val)) {
            return false;
        }

        SHARDORA_DEBUG("success get elect block sharding id: %u, height: %lu",
            sharding_id, elect_block->elect_height());
        return true;
    }

    bool GetLatestElectBlock(uint32_t sharding_id,
            elect::protobuf::ElectBlock* elect_block) {
        std::string key;
        key.reserve(48);
        key.append(kLatestElectBlockPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        SHARDORA_DEBUG("get elect block sharding id: %u",
            sharding_id);
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!elect_block->ParseFromString(val)) {
            return false;
        }

        SHARDORA_DEBUG("success get elect block sharding id: %u, height: %lu",
            sharding_id, elect_block->elect_height());
        return true;
    }

    bool ExistsOverUniqueHash(const std::string& unique_hash) const {
        std::string key = kOverUniqueHash + unique_hash;
        return db_->Exist(key);
    }

    void SaveOverUniqueHash(const std::string& unique_hash , db::DbWriteBatch& db_batch) {
        std::string key = kOverUniqueHash + unique_hash;
        db_batch.Put(key, "1");
    }

    void SaveLatestTimeBlock(const timeblock::protobuf::TimeBlock& tmblock, db::DbWriteBatch& db_batch) {
        std::string key(kLatestTimeBlockPrefix);
        db_batch.Put(key, tmblock.SerializeAsString());
        SHARDORA_DEBUG("dddddd success latest time block: %lu, %s", 
            tmblock.height(), 
            ProtobufToJson(tmblock).c_str());
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

        SHARDORA_DEBUG("dddddd success get latest time block: %lu", tmblock->height());
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
        SHARDORA_DEBUG("save sync key value %u_%u_%lu, success save block with height: %u, %u, %lu",
            sharding_id, pool_index, height, sharding_id, pool_index, height);
    }

    bool GetBlockHashWithBlockHeight(
            uint32_t sharding_id,
            uint32_t pool_index,
            uint64_t height,
            std::string* block_hash) {
        if (sharding_id < network::kRootCongressNetworkId ||
                sharding_id >= network::kConsensusShardEndNetworkId) {
            return false;
        }
        
        std::string key;
        key.reserve(32);
        key.append(kBlockHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        auto st = db_->Get(key, block_hash);
        if (!st.ok()) {
            SHARDORA_DEBUG("failed get sync key value %u_%u_%lu",
                sharding_id, pool_index, height);
            return false;
        }

        SHARDORA_DEBUG("get sync key value %u_%u_%lu, success get block with height: %u, %u, %lu",
            sharding_id, pool_index, height, sharding_id, pool_index, height);
        return true;
    }

    void SaveBlock(const view_block::protobuf::ViewBlockItem& view_block, db::DbWriteBatch& batch) {
        //assert(!view_block.qc().view_block_hash().empty());
        // if (BlockExists(view_block.qc().view_block_hash())) {
        //     auto* block_item = &view_block.block_info();
        //     SHARDORA_DEBUG("view_block.qc().view_block_hash() exists: %s, "
        //         "new block coming sharding id: %u_%d_%lu, view: %u_%u_%lu,"
        //         "tx size: %u, hash: %s, elect height: %lu, tm height: %lu",
        //         common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
        //         view_block.qc().network_id(),
        //         view_block.qc().pool_index(),
        //         block_item->height(),
        //         view_block.qc().network_id(),
        //         view_block.qc().pool_index(),
        //         view_block.qc().view(),
        //         block_item->tx_list_size(),
        //         common::Encode::HexEncode(view_block.qc().view_block_hash()).c_str(),
        //         view_block.qc().elect_height(),
        //         block_item->timeblock_height());
        //     std::string block_hash;
        //     //assert(GetBlockHashWithBlockHeight(
        //         view_block.qc().network_id(),
        //         view_block.qc().pool_index(),
        //         block_item->height(),
        //         &block_hash));
        //     return false;
        // }

        std::string key;
        key.reserve(48);
        key.append(kBlockPrefix);
        key.append(view_block.qc().view_block_hash());
        auto& block = view_block.block_info();
        SaveBlockHashWithBlockHeight(
            view_block.qc().network_id(),
            view_block.qc().pool_index(),
            block.height(),
            view_block.qc().view_block_hash(),
            batch);
        std::string block_str;
        view_block.SerializeToString(&block_str);
        batch.Put(key, block_str);
        std::string view_key;
        view_key.reserve(48);
        view_key.append(kBlockVaildHeight);
        char key_data[16];
        uint32_t *u32_arr = (uint32_t*)key_data;
        u32_arr[0] = view_block.qc().network_id();
        u32_arr[1] = view_block.qc().pool_index();
        uint64_t* u64_arr = (uint64_t*)(key_data + 8);
        u64_arr[0] = view_block.qc().view();
        view_key.append(std::string(key_data, sizeof(key_data)));
        batch.Put(view_key, "1");
    }

    bool BlockHeightExits(uint32_t sharding_id, uint32_t pool_index, uint64_t height) {
        std::string key;
        key.reserve(32);
        key.append(kBlockHeightPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        key.append((char*)&height, sizeof(height));
        return db_->Exist(key);
    }

    bool GetBlock(const std::string& block_hash, view_block::protobuf::ViewBlockItem* block) {
        std::string key;
        key.reserve(48);
        key.append(kBlockPrefix);
        key.append(block_hash);
        std::string block_str;
        auto st = db_->Get(key, &block_str);
        if (!st.ok()) {
            SHARDORA_DEBUG("failed get view block: %s", common::Encode::HexEncode(block_hash).c_str());
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
            view_block::protobuf::ViewBlockItem* block) {
        std::string block_hash;
        SHARDORA_DEBUG("GetBlockWithHeight: %u_%u_%lu", sharding_id, pool_index, height);
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
        SHARDORA_DEBUG("GetBlockStringWithHeight.");
        if (!GetBlockHashWithBlockHeight(sharding_id, pool_index, height, &block_hash)) {
            return false;
        }

        return GetBlockString(block_hash, block_str);
    }

    void SaveLatestToTxsHeights(
            const pools::protobuf::ShardToTxItem& to_heights,
            db::DbWriteBatch& batch) {
        std::string key;
        key.reserve(48);
        key.append(kLatestToTxsHeightsPrefix);
        uint32_t sharding_id = to_heights.sharding_id();
        key.append((char*)&sharding_id, sizeof(sharding_id));
        batch.Put(key, to_heights.SerializeAsString());
    }

    bool GetLatestToTxsHeights(uint32_t sharding_id, pools::protobuf::ShardToTxItem* to_heights) {
        std::string key;
        key.reserve(48);
        key.append(kLatestToTxsHeightsPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }

        if (!to_heights->ParseFromString(val)) {
            return false;
        }

        return true;
    }

    void SaveUserTx(
            const std::string& address,
            uint64_t height,
            uint32_t tx_index,
            const block::protobuf::BlockTx& tx,
            db::DbWriteBatch& db_batch) {
        if (address.size() != common::kUnicastAddressLength) {
            return;
        }

        std::string key;
        key.reserve(kUserTxPrefix.size() + address.size() + sizeof(height) + sizeof(tx_index));
        key.append(kUserTxPrefix);
        key.append(address);
        AppendUint64ForKey(height, &key);
        AppendUint32ForKey(tx_index, &key);
        db_batch.Put(key, tx.SerializeAsString());
    }

    void SaveBlockUserTxs(
            const view_block::protobuf::ViewBlockItem& view_block,
            db::DbWriteBatch& db_batch) {
        const auto& block = view_block.block_info();
        for (int32_t i = 0; i < block.tx_list_size(); ++i) {
            const auto& tx = block.tx_list(i);
            // Contract execute/refund txs are already stored in the full block
            // (SaveBlock). Indexing each one per address duplicates megabytes of
            // writes on large contract blocks and is not read by any in-node path.
            if (tx.step() == pools::protobuf::kContractExcute ||
                    tx.step() == pools::protobuf::kContractRefund) {
                continue;
            }

            if (tx.from().size() == common::kUnicastAddressLength) {
                SaveUserTx(tx.from(), block.height(), static_cast<uint32_t>(i), tx, db_batch);
            }

            if (tx.to().size() == common::kUnicastAddressLength && tx.to() != tx.from()) {
                SaveUserTx(tx.to(), block.height(), static_cast<uint32_t>(i), tx, db_batch);
            }
        }

        if (block.has_local_to()) {
            const auto& local_to = block.local_to();
            const uint32_t base_index = static_cast<uint32_t>(block.tx_list_size());
            for (int32_t i = 0; i < local_to.tos_size(); ++i) {
                const auto& to_item = local_to.tos(i);
                if (to_item.to().size() != common::kUnicastAddressLength) {
                    continue;
                }

                block::protobuf::BlockTx tx;
                tx.set_to(to_item.to());
                tx.set_balance(to_item.balance());
                tx.set_step(pools::protobuf::kConsensusLocalTos);
                tx.set_status(0);
                tx.set_nonce(to_item.nonce());
                SaveUserTx(
                    to_item.to(),
                    block.height(),
                    base_index + static_cast<uint32_t>(i),
                    tx,
                    db_batch);
            }
        }
    }

    bool GetUserTxs(
            const std::string& address,
            uint32_t limit,
            uint32_t offset,
            std::vector<UserTxItem>* txs) {
        if (address.size() != common::kUnicastAddressLength || txs == nullptr) {
            return false;
        }

        std::map<std::string, std::string> res_map;
        db_->GetAllPrefix(kUserTxPrefix + address, res_map);
        if (res_map.empty() || offset >= res_map.size()) {
            return true;
        }

        uint32_t skipped = 0;
        for (auto iter = res_map.rbegin(); iter != res_map.rend(); ++iter) {
            if (skipped++ < offset) {
                continue;
            }

            if (iter->first.size() < kUserTxPrefix.size() + address.size() + sizeof(uint64_t) + sizeof(uint32_t)) {
                continue;
            }

            UserTxItem item;
            const size_t height_pos = kUserTxPrefix.size() + address.size();
            item.height = ReadUint64FromKey(iter->first.data() + height_pos);
            item.tx_index = ReadUint32FromKey(iter->first.data() + height_pos + sizeof(uint64_t));
            if (item.tx.ParseFromString(iter->second)) {
                txs->push_back(item);
            }

            if (txs->size() >= limit) {
                break;
            }
        }

        return true;
    }

    void SaveTemporaryKv(
            const std::string& tmp_key,
            const std::string& val) {
        std::string key = kTemporaryKeyPrefix + tmp_key;
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
        }
    }

    void SaveTemporaryKv(
            const std::string& tmp_key,
            const std::string& val,
            db::DbWriteBatch& db_batch) {
        std::string key = kTemporaryKeyPrefix + tmp_key;
        SHARDORA_DEBUG("save temporary kv: %s, %s", common::Encode::HexEncode(tmp_key).c_str(), common::Encode::HexEncode(val).c_str());
        db_batch.Put(key, val);
    }

    bool GetTemporaryKv(
            const std::string& tmp_key,
            std::string* val) {
        std::string key = kTemporaryKeyPrefix + tmp_key;
        auto st = db_->Get(key, val);
        SHARDORA_DEBUG("get temporary kv: %s, status: %d, value: %s", 
            common::Encode::HexEncode(tmp_key).c_str(), 
            st.ok(), 
            common::Encode::HexEncode(*val).c_str());
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
        SHARDORA_DEBUG("save latest pool info: %s, %u_%u_%lu, synced_height: %lu, hash: %s",
            ProtobufToJson(pool_info).c_str(), 
            sharding_id, 
            pool_index, 
            pool_info.height(), 
            pool_info.synced_height(), 
            common::Encode::HexEncode(pool_info.hash()).c_str());
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

        SHARDORA_DEBUG("get latest pool info: %s, %u_%u_%lu, synced_height: %lu, hash: %s",
            ProtobufToJson(*pool_info).c_str(), 
            sharding_id, 
            pool_index, 
            pool_info->height(), 
            pool_info->synced_height(), 
            common::Encode::HexEncode(pool_info->hash()).c_str());        
        return true;
    }

    void SaveHighViewBlock(
            uint32_t sharding_id,
            uint32_t pool_index,
            const std::string& block_hash,
            db::DbWriteBatch& batch) {
        std::string key;
        key.reserve(48);
        key.append(kHighViewBlockPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        batch.Put(key, block_hash);
        SHARDORA_DEBUG("save high view block: %u_%u, hash: %s",
            sharding_id, pool_index,
            common::Encode::HexEncode(block_hash).c_str());
    }

    bool GetHighViewBlock(
            uint32_t sharding_id,
            uint32_t pool_index,
            view_block::protobuf::ViewBlockItem* block) {
        std::string key;
        key.reserve(48);
        key.append(kHighViewBlockPrefix);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        std::string block_hash;
        auto st = db_->Get(key, &block_hash);
        if (!st.ok() || block_hash.empty()) {
            return false;
        }

        SHARDORA_DEBUG("get high view block: %u_%u, hash: %s",
            sharding_id, pool_index,
            common::Encode::HexEncode(block_hash).c_str());
        return GetBlock(block_hash, block);
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

    bool ExistsBlsVerifyG2(const std::string& id) {
        std::string key = kBlsVerifyPrefex + id;
        return db_->Exist(key);
    }
    
    void AddBlsVerifyG2(
            const std::string& id,
            const bls::protobuf::VerifyVecBrdReq& verfy_req,
            db::DbWriteBatch& db_batch) {
        std::string key = kBlsVerifyPrefex + id;
        std::string val = verfy_req.SerializeAsString();
        db_batch.Put(key, val);
        SHARDORA_DEBUG("%s add bls verify g2: %s", 
            common::Encode::HexEncode(id).c_str(), ProtobufToJson(verfy_req).c_str());
    }

    void AddBlsVerifyG2(
            const std::string& id,
            const bls::protobuf::VerifyVecBrdReq& verfy_req) {
        std::string key = kBlsVerifyPrefex + id;
        std::string val = verfy_req.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
        }

        SHARDORA_DEBUG("%s add bls verify g2: %s", 
            common::Encode::HexEncode(id).c_str(), ProtobufToJson(verfy_req).c_str());
    }

    bool GetBlsVerifyG2(
            const std::string& id,
            bls::protobuf::VerifyVecBrdReq* verfy_req) {
        std::string key = kBlsVerifyPrefex + id;
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            SHARDORA_DEBUG("%s get bls verify g2 failed", common::Encode::HexEncode(id).c_str());
            return false;
        }

        if (!verfy_req->ParseFromString(val)) {
            //assert(false);
            SHARDORA_DEBUG("%s get bls verify g2 failed", common::Encode::HexEncode(id).c_str());
            return false;
        }

        SHARDORA_DEBUG("%s get bls verify g2 success", common::Encode::HexEncode(id).c_str());
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
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
        }
        
        SHARDORA_DEBUG("save bls success: %lu, %u, %s, bls_prikey: %s", elect_height,
            sharding_id,
            common::Encode::HexEncode(node_addr).c_str(),
            common::Encode::HexEncode(bls_prikey).c_str());
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
            SHARDORA_DEBUG("get bls failed: %lu, %u, %s",
                elect_height,
                sharding_id,
                common::Encode::HexEncode(security_ptr->GetAddress()).c_str());
            return false;
        }

        std::string tmp_data;
        if (security_ptr->Decrypt(
                val.substr(4, val.size() - 4),
                security_ptr->GetPrikey(),
                &tmp_data) != security::kSecuritySuccess) {
            return false;
        }

        uint32_t* int_data = (uint32_t*)val.c_str();
        if (tmp_data.size() < int_data[0]) {
            //assert(false);
            return false;
        }

        *bls_prikey = tmp_data.substr(0, int_data[0]);
        SHARDORA_DEBUG("get bls success: %lu, %u, %s, enc bls_item: %s, dec bls item: %s", elect_height,
            sharding_id,
            common::Encode::HexEncode(security_ptr->GetAddress()).c_str(),
            common::Encode::HexEncode(val).c_str(),
            common::Encode::HexEncode(*bls_prikey).c_str());
        return true;
    }

    void SavePresetVerifyValue(
            uint32_t idx, uint32_t pos, const bls::protobuf::BlsVerifyValue& verify_val) {
        std::string key;
        key.reserve(64);
        key.append(kPresetVerifyValuePrefix);
        key.append((char*)&idx, sizeof(idx));
        key.append((char*)&pos, sizeof(pos));
        std::string val = verify_val.SerializeAsString();
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
        }
    }

    bool ExistsPresetVerifyValue(uint32_t idx, uint32_t pos) {
        std::string key;
        key.reserve(64);
        key.append(kPresetVerifyValuePrefix);
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
        key.append(kPresetVerifyValuePrefix);
        key.append((char*)&idx, sizeof(idx));
        key.append((char*)&pos, sizeof(pos));
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            SHARDORA_ERROR("write db failed!");
            return false;
        }

        if (!verify_val->ParseFromString(val)) {
            return false;
        }

        return true;
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
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
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

    // Save stake information for an address (using timestamp)
    void SaveStakeInfo(
            const std::string& address,
            uint64_t total_stake_amount,
            uint64_t stake_timestamp,  // Changed from elect_height to timestamp
            uint64_t stake_block_height) {
        std::string key;
        key.reserve(64);
        key.append(kStakeInfoPrefix);
        key.append(address);
        
        // Pack data: total_stake_amount(8) + stake_timestamp(8) + stake_block_height(8)
        char data[24];
        uint64_t* u64_ptr = (uint64_t*)data;
        u64_ptr[0] = total_stake_amount;
        u64_ptr[1] = stake_timestamp;
        u64_ptr[2] = stake_block_height;
        
        std::string val(data, sizeof(data));
        auto st = db_->Put(key, val);
        if (!st.ok()) {
            SHARDORA_ERROR("Failed to save stake info for address: %s, status: %s",
                common::Encode::HexEncode(address).c_str(), st.ToString().c_str());
        } else {
            SHARDORA_DEBUG("Saved stake info: addr=%s, total_stake=%lu, timestamp=%lu, block_height=%lu",
                common::Encode::HexEncode(address).c_str(),
                total_stake_amount, stake_timestamp, stake_block_height);
        }
    }

    // Save stake information for an address (using timestamp) with db_batch
    void SaveStakeInfo(
            const std::string& address,
            uint64_t total_stake_amount,
            uint64_t stake_timestamp,
            uint64_t stake_block_height,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kStakeInfoPrefix);
        key.append(address);
        
        // Pack data: total_stake_amount(8) + stake_timestamp(8) + stake_block_height(8)
        char data[24];
        uint64_t* u64_ptr = (uint64_t*)data;
        u64_ptr[0] = total_stake_amount;
        u64_ptr[1] = stake_timestamp;
        u64_ptr[2] = stake_block_height;
        
        std::string val(data, sizeof(data));
        db_batch.Put(key, val);
        SHARDORA_DEBUG("Saved stake info to batch: addr=%s, total_stake=%lu, timestamp=%lu, block_height=%lu",
            common::Encode::HexEncode(address).c_str(),
            total_stake_amount, stake_timestamp, stake_block_height);
    }

    // Get stake information for an address (using timestamp)
    bool GetStakeInfo(
            const std::string& address,
            uint64_t* total_stake_amount,
            uint64_t* stake_timestamp) {  // Changed from elect_height to timestamp
        std::string key;
        key.reserve(64);
        key.append(kStakeInfoPrefix);
        key.append(address);
        
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            return false;
        }
        
        if (val.size() != 24) {
            SHARDORA_ERROR("Invalid stake info size: %lu for address: %s",
                val.size(), common::Encode::HexEncode(address).c_str());
            return false;
        }
        
        const uint64_t* u64_ptr = (const uint64_t*)val.c_str();
        *total_stake_amount = u64_ptr[0];
        *stake_timestamp = u64_ptr[1];
        // stake_block_height = u64_ptr[2] (not needed for return)
        
        SHARDORA_DEBUG("Got stake info: addr=%s, total_stake=%lu, timestamp=%lu",
            common::Encode::HexEncode(address).c_str(),
            *total_stake_amount, *stake_timestamp);
        
        return true;
    }

    // Remove stake information for an address
    void RemoveStakeInfo(const std::string& address) {
        std::string key;
        key.reserve(64);
        key.append(kStakeInfoPrefix);
        key.append(address);
        
        auto st = db_->Delete(key);
        if (!st.ok()) {
            SHARDORA_ERROR("Failed to remove stake info for address: %s, status: %s",
                common::Encode::HexEncode(address).c_str(), st.ToString().c_str());
        } else {
            SHARDORA_DEBUG("Removed stake info for address: %s",
                common::Encode::HexEncode(address).c_str());
        }
    }

    // Remove stake information for an address with db_batch
    void RemoveStakeInfo(const std::string& address, db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kStakeInfoPrefix);
        key.append(address);
        
        db_batch.Delete(key);
        SHARDORA_DEBUG("Removed stake info from batch for address: %s",
            common::Encode::HexEncode(address).c_str());
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
            SHARDORA_FATAL("encrypt data failed!");
            return;
        }

        auto st = db_->Put(key, enc_data);
        if (!st.ok()) {
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
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
            SHARDORA_FATAL("encrypt data failed!");
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
//             //assert(false);
            SHARDORA_ERROR("get db failed!");
            return false;
        }

        std::string dec_data;
        if (security_ptr->Decrypt(
                val,
                security_ptr->GetPrikey(),
                &dec_data) != security::kSecuritySuccess) {
            //assert(false);
            SHARDORA_ERROR("decrypt db failed!");
            return false;
        }

        uint32_t* tmp = (uint32_t*)dec_data.c_str();
        if (!local_poly->ParseFromArray(dec_data.c_str() + 4, tmp[0])) {
            //assert(false);
            SHARDORA_ERROR("parse db failed!");
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
        SHARDORA_DEBUG("%s save verified g2s: local_member_idx: %u, valid_t: %u, id: %s, val: %s",
            common::Encode::HexEncode(id).c_str(), local_member_idx, valid_t,
            common::Encode::HexEncode(id).c_str(), ProtobufToJson(verfy_final_vals).c_str());
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
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
        }

        SHARDORA_DEBUG("%s save verified g2s: local_member_idx: %u, valid_t: %u, id: %s, val: %s",
            common::Encode::HexEncode(id).c_str(), local_member_idx, valid_t,
            common::Encode::HexEncode(id).c_str(), ProtobufToJson(verfy_final_vals).c_str());
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
        SHARDORA_DEBUG("%s get verified g2s: local_member_idx: %u, valid_t: %u, id: %s",
            common::Encode::HexEncode(id).c_str(), local_member_idx, valid_t,
            common::Encode::HexEncode(id).c_str());
        std::string val;
        auto st = db_->Get(key, &val);
        if (!st.ok()) {
            SHARDORA_DEBUG("get data from db failed local_member_idx: %u, valid_t: %u, id: %s",
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
            SHARDORA_ERROR("get data from db failed!");
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
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
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
            SHARDORA_DEBUG("get db failed!");
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
            SHARDORA_DEBUG("get db failed!");
            return false;
        }

        uint64_t* tmp = (uint64_t*)val.c_str();
        *height = tmp[0];
        return true;
    }

    bool SaveIdBandwidth(
            const std::string& id,
            uint64_t bw,
            uint64_t* all_bw) {
        std::string key;
        key.reserve(128);
        key.append(kBandwidthPrefix);
        key.append(id);
        ws::protobuf::BandwidthItem item;
        uint64_t day = common::TimeUtils::TimestampDays();
        std::string val;
        auto st = db_->Get(key, &val);
        if (st.ok()) {
            if (item.ParseFromString(val)) {
                if (item.timestamp() == day) {
                    bw += item.bandwidth();
                }
            }
        }

        *all_bw = bw;
        item.set_bandwidth(*all_bw);
        item.set_timestamp(day);
        st = db_->Put(key, item.SerializeAsString());
        if (!st.ok()) {
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
        }

        return true;
    }

    void SaveSellout(
            const std::string& id,
            const ws::protobuf::SellInfo& sell_info) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSelloutPrefix);
        key.append(id);
        auto st = db_->Put(key, sell_info.SerializeAsString());
        if (!st.ok()) {
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
        }
    }

    void GetAllSellout(std::vector<std::shared_ptr<ws::protobuf::SellInfo>>* sells) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSelloutPrefix);
        std::map<std::string, std::string> sell_map;
        db_->GetAllPrefix(key, sell_map);
        for (auto iter = sell_map.begin(); iter != sell_map.end(); ++iter) {
            auto sell_ptr = std::make_shared<ws::protobuf::SellInfo>();
            auto& sell_info = *sell_ptr;
            if (!sell_info.ParseFromString(iter->second)) {
                continue;
            }

            sells->push_back(sell_ptr);
        }
    }

    bool GetSellout(
            const std::string& id,
            ws::protobuf::SellInfo* sell_info) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSelloutPrefix);
        key.append(id);
        std::string val;
        auto pst = db_->Get(key, &val);
        if (!pst.ok()) {
            return false;
        }

        return sell_info->ParseFromString(val);
    }

    void SaveSellOrder(
            const std::string& id,
            const ws::protobuf::SellInfo& sell_info) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSellorderPrefix);
        key.append(id);
        auto st = db_->Put(key, sell_info.SerializeAsString());
        if (!st.ok()) {
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
        }
    }

    void GetAllOrder(std::vector<std::shared_ptr<ws::protobuf::SellInfo>>* orders) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSellorderPrefix);
        std::map<std::string, std::string> sell_map;
        db_->GetAllPrefix(key, sell_map);
        for (auto iter = sell_map.begin(); iter != sell_map.end(); ++iter) {
            auto sell_ptr = std::make_shared<ws::protobuf::SellInfo>();
            auto& sell_info = *sell_ptr;
            if (!sell_info.ParseFromString(iter->second)) {
                continue;
            }

            orders->push_back(sell_ptr);
        }
    }

    bool GetOrder(
            const std::string& id,
            ws::protobuf::SellInfo* sell_info) {
        std::string key;
        key.reserve(128);
        key.append(kC2cSellorderPrefix);
        key.append(id);
        std::string val;
        auto pst = db_->Get(key, &val);
        if (!pst.ok()) {
            return false;
        }

        return sell_info->ParseFromString(val);
    }

    void SaveLatestToBlock(
            const view_block::protobuf::ViewBlockItem& view_block, 
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kLatestToTxBlock);
        std::string value;
        value.resize(24);
        uint64_t* val_data = (uint64_t*)value.data();
        memset(val_data, 0, 24);
        val_data[0] = view_block.qc().network_id();
        val_data[1] = view_block.qc().pool_index();
        val_data[2] = view_block.block_info().height();
        db_batch.Put(key, value);
        SHARDORA_DEBUG("success save latest to block: %u_%u_%lu",
            view_block.qc().network_id(), 
            view_block.qc().pool_index(), 
            view_block.block_info().height());
    }

    bool GetLatestToBlock(view_block::protobuf::ViewBlockItem* block) {
        std::string key;
        key.reserve(64);
        key.append(kLatestToTxBlock);
        std::string value;
        auto st = db_->Get(key, &value);
        if (!st.ok()) {
            return false;
        }

        uint64_t* val_data = (uint64_t*)value.data();
        SHARDORA_DEBUG("success get latest to block: %u_%u_%lu", static_cast<uint32_t>(val_data[0]), 
            static_cast<uint32_t>(val_data[1]), 
            val_data[2]);
        return GetBlockWithHeight(
            static_cast<uint32_t>(val_data[0]), 
            static_cast<uint32_t>(val_data[1]), 
            val_data[2],
            block);
    }

    void SaveLatestPoolStatisticTag(
            uint64_t network_id, 
            const pools::protobuf::PoolStatisticTxInfo& statistic_info) {
        std::string key;
        key.reserve(64);
        key.append(kLatestPoolStatisticTagPrefix);
        key.append((char*)&network_id, sizeof(network_id));
        auto st = db_->Put(key, statistic_info.SerializeAsString());
        if (!st.ok()) {
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
        }
        
        SHARDORA_DEBUG("success SaveLatestPoolStatisticTag network: %u, message: %s",
            network_id, ProtobufToJson(statistic_info).c_str());
    }

    void SaveLatestPoolStatisticTag(
            uint64_t network_id, 
            const pools::protobuf::PoolStatisticTxInfo& statistic_info, 
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(64);
        key.append(kLatestPoolStatisticTagPrefix);
        key.append((char*)&network_id, sizeof(network_id));
        db_batch.Put(key, statistic_info.SerializeAsString());
        SHARDORA_DEBUG("success SaveLatestPoolStatisticTag network: %u, message: %s",
            network_id, ProtobufToJson(statistic_info).c_str());
    }

    bool GetLatestPoolStatisticTag(
            uint64_t network_id, 
            pools::protobuf::PoolStatisticTxInfo* statistic_info) {
        std::string key;
        key.reserve(64);
        key.append(kLatestPoolStatisticTagPrefix);
        key.append((char*)&network_id, sizeof(network_id));
        std::string value;
        auto st = db_->Get(key, &value);
        if (!st.ok()) {
            return false;
        }

        if (!statistic_info->ParseFromString(value)) {
            return false;
        }

        SHARDORA_DEBUG("success GetLatestPoolStatisticTag network: %u, message: %s",
            network_id, ProtobufToJson(*statistic_info).c_str());
        return true;
    }

    // Used to save the private key of agg bls, currently the private key has nothing to do with elect_height
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
            SHARDORA_FATAL("write block to db failed: %d, status: %s", 1, st.ToString().c_str());
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
            SHARDORA_DEBUG("get agg bls failed: %s",
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

        SHARDORA_DEBUG("save agg bls success: %s",
            common::Encode::HexEncode(security_ptr->GetAddress()).c_str());

        *bls_prikey = libff::alt_bn128_Fr(prikey_str.c_str());
        return true;
    }

    bool ParentHashExists(const std::string& hash) {
        std::string key;
        key.reserve(48);
        key.append(kViewBlockVaildParentHash);
        key.append(hash);
        return db_->Exist(key);
    }

    void SaveValidViewBlockParentHash(
            const std::string& parent_hash, 
            uint32_t network_id, 
            uint32_t pool_index, 
            uint64_t view,
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(48);
        key.append(kViewBlockVaildParentHash);
        key.append(parent_hash);
        char value[16] = {0};
        uint32_t *u32_arr = (uint32_t*)value;
        u32_arr[0] = network_id;
        u32_arr[1] = pool_index;
        uint64_t* u64_arr = (uint64_t*)(value + 8);
        u64_arr[0] = view;
        db_batch.Put(key, std::string(value, sizeof(value)));
        SHARDORA_DEBUG("save valid view block parent hash: %s, %u_%u_%lu",
            common::Encode::HexEncode(parent_hash).c_str(), 
            network_id, 
            pool_index, 
            view);
    }

    void SaveElectHeightWithBlock(
            uint32_t sharding_id, 
            uint64_t elect_height, 
            const std::string& block_hash, 
            db::DbWriteBatch& db_batch) {
        std::string key;
        key.reserve(48);
        key.append(kElectHeightWithElectBlock);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&elect_height, sizeof(elect_height));
        db_batch.Put(key, block_hash);
        SHARDORA_DEBUG("save elect height with block hash: %s, sharding id: %u, elect height: %lu",
            common::Encode::HexEncode(block_hash).c_str(), 
            sharding_id, 
            elect_height);
    }

    bool GetBlockWithElectHeight(
            uint32_t sharding_id, 
            uint64_t elect_height, 
            view_block::protobuf::ViewBlockItem* block) {
        std::string key;
        key.reserve(48);
        key.append(kElectHeightWithElectBlock);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&elect_height, sizeof(elect_height));
        std::string block_hash;
        auto st = db_->Get(key, &block_hash);
        if (!st.ok()) {
            SHARDORA_DEBUG("failed get elect height with block hash: %s, sharding id: %u, elect height: %lu",
                common::Encode::HexEncode(block_hash).c_str(), 
                sharding_id, 
                elect_height);
            return false;
        }
        
        return GetBlock(block_hash, block);
    }

    bool SaveLatestLeaderProposeMessage(const transport::protobuf::Header& msg, uint64_t latest_qc_view) {
        std::string key;
        key.reserve(48);
        key.append(kLeaderLatestProposeMessage);
        auto& view_item = msg.hotstuff().pro_msg().view_item();
        uint32_t sharding_id = view_item.qc().network_id();
        uint32_t pool_index = view_item.qc().pool_index();
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        auto val = msg.SerializeAsString();
        char data[val.size() + sizeof(latest_qc_view)];
        uint64_t* udata = (uint64_t*)data;
        udata[0] = latest_qc_view;
        memcpy(data + sizeof(latest_qc_view), val.data(), val.size());
        std::string value(data, sizeof(data));
        auto st = db_->Put(key, value);
        SHARDORA_DEBUG("success SaveLatestLeaderProposeMessage network: %u, pool: %u, view: %lu, latest_qc_view: %lu",
            sharding_id, pool_index, view_item.qc().view(), latest_qc_view);
        return st.ok();
    }

    bool GetLatestLeaderProposeMessage(
            uint32_t sharding_id, 
            uint32_t pool_index, 
            transport::protobuf::Header* msg, 
            uint64_t* latest_qc_view) {
        std::string key;
        key.reserve(48);
        key.append(kLeaderLatestProposeMessage);
        key.append((char*)&sharding_id, sizeof(sharding_id));
        key.append((char*)&pool_index, sizeof(pool_index));
        std::string data;
        auto st = db_->Get(key, &data);
        if (!st.ok()) {
            return false;
        }
        
        uint64_t* udata = (uint64_t*)data.c_str();
        *latest_qc_view = udata[0];
        SHARDORA_DEBUG("success GetLatestLeaderProposeMessage network: %u, pool: %u, latest_qc_view: %lu",
            sharding_id, pool_index, *latest_qc_view);
        return msg->ParseFromArray(data.data() + sizeof(*latest_qc_view), data.size() - sizeof(*latest_qc_view));
    }

private:
    static const uint32_t kSaveElectHeightCount = 4u;

    std::shared_ptr<db::Db> db_ = nullptr;
    uint64_t prev_gid_tm_us_ = 0;
    common::Tick db_batch_tick_;
    std::atomic<bool> dumped_gid_ = false;

    static void AppendUint64ForKey(uint64_t value, std::string* key) {
        for (int32_t i = 7; i >= 0; --i) {
            key->push_back(static_cast<char>((value >> (i * 8)) & 0xff));
        }
    }

    static void AppendUint32ForKey(uint32_t value, std::string* key) {
        for (int32_t i = 3; i >= 0; --i) {
            key->push_back(static_cast<char>((value >> (i * 8)) & 0xff));
        }
    }

    static uint64_t ReadUint64FromKey(const char* data) {
        uint64_t value = 0;
        for (uint32_t i = 0; i < sizeof(uint64_t); ++i) {
            value = (value << 8) | static_cast<unsigned char>(data[i]);
        }
        return value;
    }

    static uint32_t ReadUint32FromKey(const char* data) {
        uint32_t value = 0;
        for (uint32_t i = 0; i < sizeof(uint32_t); ++i) {
            value = (value << 8) | static_cast<unsigned char>(data[i]);
        }
        return value;
    }

    DISALLOW_COPY_AND_ASSIGN(PrefixDb);
};

};  // namespace protos

};  // namespace shardora
