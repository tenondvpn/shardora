#include "elect/node_history_credit.h"

#include "common/string_utils.h"
#include "common/encode.h"
#include "db/db.h"
#include "db/db_utils.h"

namespace zjchain {

namespace elect {

NodeHistoryCredit::NodeHistoryCredit(std::shared_ptr<db::Db>& db) : db_(db) {}

NodeHistoryCredit::~NodeHistoryCredit() {}

void NodeHistoryCredit::OnNewElectBlock(
        std::shared_ptr<security::Security>& security,
        uint64_t height,
        protobuf::ElectBlock& elect_block) {
    std::lock_guard<std::mutex> g(mutex_);
    std::string height_key = "db::kElectionHistoryCredit" + std::to_string(height);
    if (db_->Exist(height_key)) {
        return;
    }

    db::DbWriteBatch write_batch;
    write_batch.Put(height_key, "1");
    for (int32_t i = 0; i < elect_block.in_size(); ++i) {
        auto id = security->GetAddress(elect_block.in(i).pubkey());
        ChangeCredit(id, false, write_batch);
    }

    for (int32_t i = 0; i < elect_block.weedout_ids_size(); ++i) {
        ChangeCredit(elect_block.weedout_ids(i), true, write_batch);
    }

    db_->Put(write_batch);
}

void NodeHistoryCredit::ChangeCredit(
        const std::string& id,
        bool weedout,
        db::DbWriteBatch& write_batch) {
    auto iter = credit_map_.find(id);
    std::string id_key = "db::kElectionHistoryCredit" + id;
    int32_t credit = kInitNodeCredit;
    if (iter != credit_map_.end()) {
        credit = iter->second;
    } else {
        std::string value;
        auto st = db_->Get(id_key, &value);
        if (st.ok()) {
            common::StringUtil::ToInt32(value, &credit);
        }
    }

    int32_t add_credit = credit;
    if (weedout) {
        if (add_credit > 0) {
            --add_credit;
        }
    } else {
        if (add_credit < kMaxNodeCredit) {
            ++add_credit;
        }
    }

    if (add_credit != credit) {
        write_batch.Put(id_key, std::to_string(add_credit));
        if (iter != credit_map_.end()) {
            iter->second = add_credit;
        } else {
            credit_map_[id] = add_credit;
        }
    }

    ELECT_DEBUG("change credit %s, %d, %d", common::Encode::HexEncode(id).c_str(), add_credit, weedout);
}

int NodeHistoryCredit::GetNodeHistoryCredit(const std::string& id, int32_t* credit) {
    std::lock_guard<std::mutex> g(mutex_);
    auto iter = credit_map_.find(id);
    if (iter != credit_map_.end()) {
        *credit = iter->second;
    } else {
        std::string id_key = "db::kElectionHistoryCredit" + id;
        std::string value;
        auto st = db_->Get(id_key, &value);
        if (!st.ok()) {
            return kElectError;
        }

        if (common::StringUtil::ToInt32(value, credit)) {
            credit_map_[id] = *credit;
        }
    }

    return kElectSuccess;
}

}  // namespace elect

}  // namespace zjchain
