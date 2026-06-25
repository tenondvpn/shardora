#include "init/http_handler.h"
#include "init/uws_adapter.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <fstream>
#include <vector>

#include <cpppbc/GT.h>
#include <cpppbc/Pairing.h>
#include <nlohmann/json.hpp>

#include "common/encode.h"
#include "common/global_info.h"
#include "common/string_utils.h"
#include "consensus/hotstuff/view_block_chain.h"
#include "consensus/hotstuff/hotstuff_manager.h"
#include "contract/contract_ars.h"
#include "contract/contract_reencryption.h"
#include "dht/dht_key.h"
#include "network/network_utils.h"
#include "network/route.h"
#include "pools/tx_utils.h"
#include "protos/pools.pb.h"
#include "protos/transport.pb.h"
#include "protos/view_block.pb.h"
#include "security/gmssl/gmssl.h"
#include "security/oqs/oqs.h"
#include "security/ecdsa/ecdsa.h"
#include "security/ecdsa/secp256k1.h"
#include "transport/tcp_transport.h"
#include "sethvm/execution.h"
#include "sethvm/seth_host.h"
#include "sethvm/sethvm_utils.h"

#include <google/protobuf/util/json_util.h>

namespace seth {

namespace init {

static HttpHandler* http_handler = nullptr;
static std::shared_ptr<protos::PrefixDb> prefix_db = nullptr;
std::shared_ptr<contract::ContractManager> contract_mgr = nullptr;
static std::shared_ptr<security::Security> secptr = nullptr;

enum HttpStatusCode : int32_t {
    kHttpSuccess = 0,
    kHttpError = 1,
    kAccountNotExists = 2,
    kBalanceInvalid = 3,
    kShardIdInvalid = 4,
    kSignatureInvalid = 5,
    kFromEqualToInvalid = 6,
};

static const std::string kHttpParamTaskId = "tid";

static const std::map<int, const char*> kStatusMap = {
    {kHttpSuccess, "kHttpSuccess"},
    {kHttpError, "kHttpError"},
    {kAccountNotExists, "kAccountNotExists"},
    {kBalanceInvalid, "kBalanceInvalid"},
    {kShardIdInvalid, "kShardIdInvalid"},
    {kSignatureInvalid, "kSignatureInvalid"},
    {kFromEqualToInvalid, "kFromEqualToInvalid"},
};

static const char* GetStatus(int status) {
    auto iter = kStatusMap.find(status);
    if (iter != kStatusMap.end()) {
        return iter->second;
    }

    return "unknown";
}

static bool NormalizeHexAddress(const std::string& input, std::string* output) {
    std::string address = input;
    if (address.size() >= 2 && address[0] == '0' &&
            (address[1] == 'x' || address[1] == 'X')) {
        address = address.substr(2);
    }

    if (address.size() != common::kUnicastAddressLength * 2) {
        return false;
    }

    for (char c : address) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }

    std::transform(address.begin(), address.end(), address.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    *output = address;
    return true;
}

static int CreateOqsTransactionWithAttr(
        uint64_t nonce,
        const std::string& from_pk,
        const std::string& to,
        const std::string& sign,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id,
        const UWSRequest& req,
        transport::protobuf::Header& msg) {
    security::Oqs oqs;
    auto from = oqs.GetAddress(from_pk);
    if (from.empty()) {
        SETH_INFO("failed get address from pk: %s", common::Encode::HexEncode(from_pk).c_str());
        return kAccountNotExists;
    }

    if (from == to) {
        SETH_INFO("failed get address from == to: %s", common::Encode::HexEncode(from).c_str());
        return kFromEqualToInvalid;
    }

    if (from.size() != 20 || to.size() != 20) {
        SETH_INFO("failed get address size error: %lu, %lu", from.size(), to.size());
        return kAccountNotExists;
    }

    SETH_INFO("OQS transaction from: %s, to: %s, nonce: %lu",
        common::Encode::HexEncode(from).c_str(),
        common::Encode::HexEncode(to).c_str(),
        nonce);

    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    msg.set_hop_count(0);

    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_nonce(nonce);
    new_tx->set_pubkey(from_pk);
    
    auto step = req.get_param_value("type");
    uint32_t step_val = 0;
    if (!step.empty() && !common::StringUtil::ToUint32(step, &step_val)) {
        SETH_ERROR("invalid step parameter: %s", step.c_str());
        return kHttpError;
    }

    auto contract_bytes_hex = req.get_param_value("bytes_code");
    std::string contract_bytes;
    if (!contract_bytes_hex.empty()) {
        contract_bytes = common::Encode::HexDecode(contract_bytes_hex);
        if (step_val == pools::protobuf::kCreateLibrary || step_val == pools::protobuf::kCreateContract) {
            if (common::IsContractBytescodeValid(contract_bytes) != common::ValidationStatus::SUCCESS) {
                SETH_INFO("create contract not has valid code: %s", common::Encode::HexEncode(contract_bytes).c_str());
                return kHttpError;
            }
        }
    }

    new_tx->set_step(static_cast<pools::protobuf::StepType>(step_val));
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    ADD_TX_DEBUG_INFO(new_tx);
    
    auto key = req.get_param_value("key");
    auto val = req.get_param_value("val");
    if (!key.empty()) {
        new_tx->set_key(key);
        if (!val.empty()) {
            new_tx->set_value(val);
        }
    }

    if (!contract_bytes.empty()) {
        new_tx->set_contract_code(contract_bytes);
    }

    auto input = req.get_param_value("input");
    if (!input.empty()) {
        new_tx->set_contract_input(common::Encode::HexDecode(input));
    }

    auto prefund = req.get_param_value("prefund");
    if (!prefund.empty()) {
        uint64_t prefund_val = 0;
        if (!common::StringUtil::ToUint64(prefund, &prefund_val)) {
            SETH_WARN("get prefund failed %s", prefund.c_str());
            return kSignatureInvalid;
        }
        new_tx->set_contract_prefund(prefund_val);
    }

    if (sign.empty()) {
        SETH_ERROR("OQS Signature is empty!");
        return kSignatureInvalid;
    }

    try {
        auto tx_hash = pools::GetTxMessageHash(*new_tx);
        if (oqs.Verify(tx_hash, from_pk, sign) != security::kSecuritySuccess) {
            SETH_ERROR("OQS verify signature failed! tx_hash: %s, pk: %s, sign: %s",
                common::Encode::HexEncode(tx_hash).c_str(),
                common::Encode::HexEncode(from_pk).c_str(),
                common::Encode::HexEncode(sign).c_str());
            return kSignatureInvalid;
        }

        new_tx->set_sign(sign);
    } catch (const std::exception& e) {
        SETH_ERROR("exception during oqs transaction creation: %s", e.what());
        return kSignatureInvalid;
    }

    return kHttpSuccess;
}

static void OqsHttpTransaction(const UWSRequest& req, UWSResponse& http_res) {
    if (http_handler->net_handler() == nullptr) {
        std::string res = std::string("node not ready!");
        http_res.set_content(res, "text/plain");
        return;
    }

    SETH_INFO("OQS http transaction request received.");
    
    auto from_pk_hex = req.get_param_value("pubkey");
    auto to_hex = req.get_param_value("to");
    auto sign_hex = req.get_param_value("sign");
    auto nonce_str = req.get_param_value("nonce");
    auto amount_str = req.get_param_value("amount");
    auto shard_id_str = req.get_param_value("shard_id");
    auto gas_limit = req.get_param_value("gas_limit");
    auto gas_price = req.get_param_value("gas_price");

    uint64_t gas_limit_val = 0;
    if (!common::StringUtil::ToUint64(gas_limit, &gas_limit_val)) {
        std::string res = std::string("gas_limit not integer: ") + gas_limit;
        http_res.set_content(res, "text/plain");
        return;
    }

    uint64_t gas_price_val = 0;
    if (!common::StringUtil::ToUint64(gas_price, &gas_price_val)) {
        std::string res = std::string("gas_price not integer: ") + gas_price;
        http_res.set_content(res, "text/plain");
        return;
    }

    uint64_t nonce = 0, amount = 0;
    int32_t shard_id = 0;
    if (!common::StringUtil::ToUint64(nonce_str, &nonce) || 
        !common::StringUtil::ToUint64(amount_str, &amount) ||
        !common::StringUtil::ToInt32(shard_id_str, &shard_id)) {
        http_res.set_content("error: invalid numeric parameters", "text/plain");
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    int status = CreateOqsTransactionWithAttr(
        nonce,
        common::Encode::HexDecode(from_pk_hex),
        common::Encode::HexDecode(to_hex),
        common::Encode::HexDecode(sign_hex),
        amount,
        gas_limit_val,
        gas_price_val,
        shard_id,
        req,
        msg_ptr->header);

    if (status != kHttpSuccess) {
        http_res.set_content(std::string("transaction invalid: ") + GetStatus(status), "text/plain");
        return;
    }

    security::Oqs oqs;
    auto addr = oqs.GetAddress(common::Encode::HexDecode(from_pk_hex));
    msg_ptr->msg_hash = pools::GetTxMessageHash(msg_ptr->header.tx_proto());
    msg_ptr->address_info = http_handler->acc_mgr()->GetAccountInfo(addr);
    
    if (msg_ptr->address_info == nullptr) {
        http_res.set_content("kAccountNotExists", "text/plain");
        return;
    }

    msg_ptr->header.set_hash64(common::Random::RandomUint64());
    http_handler->net_handler()->NewHttpServer(msg_ptr);
    msg_ptr->handle_status = transport::kMessageHandle;
    
    {
        std::lock_guard<std::mutex> lock(http_handler->tx_msg_map_mutex());
        http_handler->tx_msg_map().Put(msg_ptr->msg_hash, msg_ptr);
    }

    http_res.set_content("ok", "text/plain");
    SETH_INFO("OQS transaction successfully processed and broadcasted.");
}

static int CreateGmTransactionWithAttr(
        uint64_t nonce,
        const std::string& from_pk,
        const std::string& to,
        const std::string& sign,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id,
        const UWSRequest& req,
        transport::protobuf::Header& msg) {
    security::GmSsl gm;
    auto from = gm.GetAddress(from_pk);
    if (from.empty()) {
        SETH_INFO("failed get gm address from pk: %s", common::Encode::HexEncode(from_pk).c_str());
        return kAccountNotExists;
    }

    if (from == to) {
        return kFromEqualToInvalid;
    }

    if (from.size() != 20 || to.size() != 20) {
        return kAccountNotExists;
    }

    SETH_INFO("GmSSL transaction from: %s, to: %s, nonce: %lu",
        common::Encode::HexEncode(from).c_str(),
        common::Encode::HexEncode(to).c_str(),
        nonce);

    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    msg.set_hop_count(0);

    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_nonce(nonce);
    new_tx->set_pubkey(from_pk);
    
    auto step = req.get_param_value("type");
    uint32_t step_val = 0;
    if (!step.empty() && !common::StringUtil::ToUint32(step, &step_val)) {
        SETH_ERROR("invalid step parameter: %s", step.c_str());
        return kHttpError;
    }

    auto contract_bytes_hex = req.get_param_value("bytes_code");
    std::string contract_bytes;
    if (!contract_bytes_hex.empty()) {
        contract_bytes = common::Encode::HexDecode(contract_bytes_hex);
        if (step_val == pools::protobuf::kCreateLibrary || step_val == pools::protobuf::kCreateContract) {
            if (common::IsContractBytescodeValid(contract_bytes) != common::ValidationStatus::SUCCESS) {
                SETH_ERROR("create contract not has valid code: %s", common::Encode::HexEncode(contract_bytes).c_str());
                return kHttpError;
            }
        }
    }

    new_tx->set_step(static_cast<pools::protobuf::StepType>(step_val));
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    ADD_TX_DEBUG_INFO(new_tx);
    
    auto key = req.get_param_value("key");
    auto val = req.get_param_value("val");
    if (!key.empty()) {
        new_tx->set_key(key);
        if (!val.empty()) new_tx->set_value(val);
    }

    if (!contract_bytes.empty()) {
        new_tx->set_contract_code(contract_bytes);
    }

    auto input = req.get_param_value("input");
    if (!input.empty()) {
        new_tx->set_contract_input(common::Encode::HexDecode(input));
    }

    auto prefund = req.get_param_value("prefund");
    if (!prefund.empty()) {
        uint64_t prefund_val = 0;
        if (common::StringUtil::ToUint64(prefund, &prefund_val)) {
            new_tx->set_contract_prefund(prefund_val);
        }
    }

    if (sign.empty()) {
        SETH_ERROR("GmSSL Signature is empty!");
        return kSignatureInvalid;
    }

    try {
        auto tx_hash = pools::GetTxMessageHash(*new_tx);
        if (gm.Verify(tx_hash, from_pk, sign) != security::kSecuritySuccess) {
            SETH_ERROR("GmSSL verify signature failed! hash: %s", 
                common::Encode::HexEncode(tx_hash).c_str());
            return kSignatureInvalid;
        }

        new_tx->set_sign(sign);
    } catch (...) {
        return kSignatureInvalid;
    }

    return kHttpSuccess;
}

static void GmHttpTransaction(const UWSRequest& req, UWSResponse& http_res) {
    if (http_handler->net_handler() == nullptr) {
        std::string res = std::string("node not ready!");
        http_res.set_content(res, "text/plain");
        return;
    }

    SETH_INFO("GmSSL http transaction request received.");
    
    auto from_pk_hex = req.get_param_value("pubkey");
    auto to_hex = req.get_param_value("to");
    auto sign_hex = req.get_param_value("sign");
    auto nonce_str = req.get_param_value("nonce");
    auto amount_str = req.get_param_value("amount");
    auto shard_id_str = req.get_param_value("shard_id");
    auto gas_limit = req.get_param_value("gas_limit");
    auto gas_price = req.get_param_value("gas_price");

    uint64_t gas_limit_val = 0, gas_price_val = 0, nonce = 0, amount = 0;
    int32_t shard_id = 0;
    
    if (!common::StringUtil::ToUint64(gas_limit, &gas_limit_val) ||
        !common::StringUtil::ToUint64(gas_price, &gas_price_val) ||
        !common::StringUtil::ToUint64(nonce_str, &nonce) || 
        !common::StringUtil::ToUint64(amount_str, &amount) ||
        !common::StringUtil::ToInt32(shard_id_str, &shard_id)) {
        http_res.set_content("error: invalid numeric parameters", "text/plain");
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    int status = CreateGmTransactionWithAttr(
        nonce,
        common::Encode::HexDecode(from_pk_hex),
        common::Encode::HexDecode(to_hex),
        common::Encode::HexDecode(sign_hex),
        amount,
        gas_limit_val,
        gas_price_val,
        shard_id,
        req,
        msg_ptr->header);

    if (status != kHttpSuccess) {
        http_res.set_content(std::string("gm transaction invalid: ") + GetStatus(status), "text/plain");
        return;
    }

    security::GmSsl gm;
    auto addr = gm.GetAddress(common::Encode::HexDecode(from_pk_hex));
    msg_ptr->msg_hash = pools::GetTxMessageHash(msg_ptr->header.tx_proto());
    msg_ptr->address_info = http_handler->acc_mgr()->GetAccountInfo(addr);
    
    if (msg_ptr->address_info == nullptr) {
        http_res.set_content("kAccountNotExists", "text/plain");
        return;
    }

    msg_ptr->header.set_hash64(common::Random::RandomUint64());
    http_handler->net_handler()->NewHttpServer(msg_ptr);
    msg_ptr->handle_status = transport::kMessageHandle;
    
    {
        std::lock_guard<std::mutex> lock(http_handler->tx_msg_map_mutex());
        http_handler->tx_msg_map().Put(msg_ptr->msg_hash, msg_ptr);
    }

    http_res.set_content("ok", "text/plain");
    SETH_INFO("GmSSL transaction successfully processed.");
}

static int CreateTransactionWithAttr(
        uint64_t nonce,
        const std::string& from_pk,
        const std::string& to,
        const std::string& sign_r,
        const std::string& sign_s,
        int32_t sign_v,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id,
        const UWSRequest& req,
        transport::protobuf::Header& msg) {
    auto from = http_handler->security_ptr()->GetAddressWithPublicKey(from_pk);
    if (from.empty()) {
        SETH_INFO("failed get address from pk: %s", common::Encode::HexEncode(from_pk).c_str());
        return kAccountNotExists;
    }

    if (from == to) {
        SETH_INFO("failed get address from == to: %s", common::Encode::HexEncode(from).c_str());
        return kFromEqualToInvalid;
    }

    if (from.size() != 20 || to.size() != 20) {
        SETH_INFO("failed get address size error: %s, %s",
            common::Encode::HexEncode(from).c_str(), 
            common::Encode::HexEncode(to).c_str());
        return kAccountNotExists;
    }

    SETH_INFO("from: %s, to: %s, nonce: %lu",
        common::Encode::HexEncode(from).c_str(),
        common::Encode::HexEncode(to).c_str(),
        nonce);
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    msg.set_hop_count(0);
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_nonce(nonce);
    new_tx->set_pubkey(from_pk);
    auto step = req.get_param_value("type");
    uint32_t step_val = 0;
    if (!common::StringUtil::ToUint32(step, &step_val)) {
        SETH_ERROR("invalid step parameter: %s", step.c_str());
        return kHttpError;
    }

    auto contract_bytes = req.get_param_value("bytes_code");
    if (step_val == pools::protobuf::kCreateLibrary || step_val == pools::protobuf::kCreateContract) {
        contract_bytes = common::Encode::HexDecode(contract_bytes);
        if (common::IsContractBytescodeValid(contract_bytes) != common::ValidationStatus::SUCCESS) {
            SETH_INFO("create contract not has valid contract code: %s",
                common::Encode::HexEncode(contract_bytes).c_str());
            return kHttpError;
        }
    }

    new_tx->set_step(static_cast<pools::protobuf::StepType>(step_val));
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    ADD_TX_DEBUG_INFO(new_tx);
    
    auto key = req.get_param_value("key");
    auto val = req.get_param_value("val");
    if (!key.empty()) {
        new_tx->set_key(key);
        if (!val.empty()) {
            new_tx->set_value(val);
        }
    }

    if (!contract_bytes.empty()) {
        new_tx->set_contract_code(contract_bytes);
    }

    auto input = req.get_param_value("input");
    if (!input.empty()) {
        new_tx->set_contract_input(common::Encode::HexDecode(input));
    }

    auto prefund = req.get_param_value("prefund");
    if (!prefund.empty()) {
        uint64_t prefund_val = 0;
        if (!common::StringUtil::ToUint64(prefund, &prefund_val)) {
            SETH_WARN("get prefund failed %s", prefund);
            return kSignatureInvalid;
        }

        new_tx->set_contract_prefund(prefund_val);
    }

    if (sign_r.empty() || sign_s.empty()) {
        SETH_ERROR("Missing signature components! r_len: %lu, s_len: %lu", 
            sign_r.size(), sign_s.size());
        return kSignatureInvalid;
    }

    std::string sign;
    sign.reserve(65);
    sign.append(sign_r);
    sign.append(sign_s);
    sign.push_back(static_cast<char>(sign_v));

    if (sign.size() != 65) {
        SETH_ERROR("Invalid signature length constructed: %lu. (r: %lu, s: %lu)", 
            sign.size(), sign_r.size(), sign_s.size());
        return kSignatureInvalid;
    }

    if (from_pk.empty()) {
        SETH_ERROR("Public key is empty in Verify!");
        return kSignatureInvalid;
    }
    
    SETH_INFO("now call get tx hash: %s", ProtobufToJson(*new_tx).c_str());
    try {
        auto tx_hash = pools::GetTxMessageHash(*new_tx);
        SETH_INFO("new tx hash: %s, tx: %s", 
            common::Encode::HexEncode(tx_hash).c_str(), ProtobufToJson(*new_tx).c_str());
        if (http_handler->security_ptr()->Verify(
                tx_hash, from_pk, sign) != security::kSecuritySuccess) {
            sign[64] = sign_v == 0 ? 1 : 0;
            if (http_handler->security_ptr()->Verify(
                    tx_hash, from_pk, sign) != security::kSecuritySuccess) {
                SETH_ERROR("verify signature failed tx_hash: %s, "
                    "sign_r: %s, sign_s: %s, sign_v: %d, pk: %s, hash64: %lu",
                    common::Encode::HexEncode(tx_hash).c_str(),
                    common::Encode::HexEncode(sign_r).c_str(),
                    common::Encode::HexEncode(sign_s).c_str(),
                    sign_v,
                    common::Encode::HexEncode(from_pk).c_str(),
                    msg.hash64());
                return kSignatureInvalid;
            }
        }

        new_tx->set_sign(sign);
    } catch (const std::exception& e) {
        SETH_ERROR("exception when create transaction: %s", e.what());
        return kSignatureInvalid;
    }
    return kHttpSuccess;
}
 
static inline std::string HttpProtobufToJson(
        const google::protobuf::Message& message, 
        bool pretty_print = false) {
    std::string json_str;
    google::protobuf::util::JsonPrintOptions options;
    options.add_whitespace = pretty_print;
    auto status = google::protobuf::util::MessageToJsonString(message, &json_str, options);
    if (!status.ok()) {
        return "";
    }
    return json_str;
}

static void HttpTransaction(const UWSRequest& req, UWSResponse& http_res) {
    if (http_handler->net_handler() == nullptr) {
        std::string res = std::string("node not ready!");
        http_res.set_content(res, "text/plain");
        return;
    }

    SETH_INFO("http transaction coming.");
    auto nonce_str = req.get_param_value("nonce");
    auto frompk = req.get_param_value("pubkey");
    auto to = req.get_param_value("to");
    auto amount = req.get_param_value("amount");
    auto gas_limit = req.get_param_value("gas_limit");
    auto gas_price = req.get_param_value("gas_price");
    auto sign_r = req.get_param_value("sign_r");
    auto sign_s = req.get_param_value("sign_s");
    auto sign_v = req.get_param_value("sign_v");
    auto shard_id = req.get_param_value("shard_id");
    uint64_t nonce = 0;
    if (!common::StringUtil::ToUint64(nonce_str, &nonce)) {
        std::string res = std::string("nonce not integer: ") + nonce_str;
        http_res.set_content(res, "text/plain");
        return;
    }
    
    uint64_t amount_val = 0;
    if (!common::StringUtil::ToUint64(amount, &amount_val)) {
        std::string res = std::string("amount not integer: ") + amount;
        http_res.set_content(res, "text/plain");
        return;
    }

    uint64_t gas_limit_val = 0;
    if (!common::StringUtil::ToUint64(gas_limit, &gas_limit_val)) {
        std::string res = std::string("gas_limit not integer: ") + gas_limit;
        http_res.set_content(res, "text/plain");
        return;
    }

    uint64_t gas_price_val = 0;
    if (!common::StringUtil::ToUint64(gas_price, &gas_price_val)) {
        std::string res = std::string("gas_price not integer: ") + gas_price;
        http_res.set_content(res, "text/plain");
        return;
    }

    int32_t shard_id_val = 0;
    if (!common::StringUtil::ToInt32(shard_id, &shard_id_val)) {
        std::string res = std::string("shard_id not integer: ") + shard_id;
        http_res.set_content(res, "text/plain");
        return;
    }

    std::string tmp_sign_r = sign_r;
    std::string tmp_sign_s = sign_s;
    if (tmp_sign_r.size() < 64) {
        tmp_sign_r = std::string(64 - tmp_sign_r.size(), '0') + sign_r;
    }

    if (tmp_sign_s.size() < 64) {
        tmp_sign_s = std::string(64 - tmp_sign_s.size(), '0') + sign_s;
    }

    int32_t tmp_sign_v = 0;
    if (!common::StringUtil::ToInt32(sign_v, &tmp_sign_v)) {
        std::string res = std::string("sign_v not integer: ") + sign_v;
        http_res.set_content(res, "text/plain");
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    int status = CreateTransactionWithAttr(
        nonce,
        common::Encode::HexDecode(frompk),
        common::Encode::HexDecode(to),
        common::Encode::HexDecode(tmp_sign_r),
        common::Encode::HexDecode(tmp_sign_s),
        tmp_sign_v,
        amount_val,
        gas_limit_val,
        gas_price_val,
        shard_id_val,
        req,
        msg);
    if (status != kHttpSuccess) {
        std::string res = std::string("transaction invalid: ") + GetStatus(status);
        http_res.set_content(res, "text/plain");
        return;
    }

    auto thread_index = -1;
    SETH_INFO("http handler success get http server thread index: %d, address: %s", 
        thread_index, 
        common::Encode::HexEncode(
            http_handler->security_ptr()->GetAddressWithPublicKey(common::Encode::HexDecode(frompk))).c_str());
    msg_ptr->msg_hash = pools::GetTxMessageHash(msg.tx_proto());
    msg_ptr->address_info = http_handler->acc_mgr()->GetAccountInfo(
        http_handler->security_ptr()->GetAddressWithPublicKey(common::Encode::HexDecode(frompk)));
    if (msg_ptr->address_info == nullptr) {
        std::string res = std::string("address invalid: ") + common::Encode::HexEncode(
            http_handler->security_ptr()->GetAddressWithPublicKey(common::Encode::HexDecode(frompk)));
        http_res.set_content(res, "text/plain");
        return;
    }
    
    msg_ptr->header.set_hash64(common::Random::RandomUint64());
    SETH_INFO("http handler success get http server thread index: %d, address: %s, hash64: %lu", 
        thread_index, 
        common::Encode::HexEncode(
            http_handler->security_ptr()->GetAddressWithPublicKey(common::Encode::HexDecode(frompk))).c_str(),
        msg_ptr->header.hash64());
    http_handler->net_handler()->NewHttpServer(msg_ptr);
    std::string res = std::string("ok");
    http_res.set_content(res, "text/plain");
    msg_ptr->handle_status = transport::kMessageHandle;
    {
        std::lock_guard<std::mutex> lock(http_handler->tx_msg_map_mutex());
        http_handler->tx_msg_map().Put(msg_ptr->msg_hash, msg_ptr);
    }

    SETH_INFO("http transaction success %s, %s, nonce: %lu, txhash: %s", 
        common::Encode::HexEncode(
        http_handler->security_ptr()->GetAddressWithPublicKey(common::Encode::HexDecode(frompk))).c_str(), 
        to, nonce,
        common::Encode::HexEncode(msg_ptr->msg_hash).c_str());
}

static void QueryContract(const UWSRequest& req, UWSResponse& http_res) {
    SETH_INFO("query contract coming.");
    auto tmp_contract_addr = req.get_param_value("address");
    auto tmp_input = req.get_param_value("input");
    auto tmp_from = req.get_param_value("from");
    std::string from = common::Encode::HexDecode(tmp_from);
    if (from.size() != common::kUnicastAddressLength) {
        from = common::Encode::HexDecode(std::string(common::kUnicastAddressLength * 2, '0'));
    }

    std::string contract_addr = common::Encode::HexDecode(tmp_contract_addr);
    std::string input = common::Encode::HexDecode(tmp_input);

    uint64_t height = 0;
    // auto contract_prefund_id = contract_addr + from;
    // protos::AddressInfoPtr addr_info =  http_handler->acc_mgr()->GetAccountInfo(contract_prefund_id);
    // if (!addr_info) {
    //     addr_info = prefix_db->GetAddressInfo(contract_prefund_id);
    // }

    // if (!addr_info) {
    //     std::string res = "get from prefund failed: " + std::string(tmp_contract_addr) + ", " + std::string(tmp_from);
    //     SETH_INFO("query contract param error: %s.", res.c_str());
    //     http_res.set_content(res, "text/plain");
    //     return;
    // }

    uint64_t prefund = 9999999999lu;//addr_info->balance();
    auto contract_addr_info = prefix_db->GetAddressInfo(contract_addr);
    if (contract_addr_info == nullptr) {
        std::string res = "get contract addr failed: " + std::string(tmp_contract_addr);
        http_res.set_content(res, "text/plain");
        SETH_INFO("query contract param error: %s.", res.c_str());
        return;
    }

    sethvm::SethhainHost seth_host;
    seth_host.tx_context_.tx_origin = evmc::address{};
    seth_host.tx_context_.block_coinbase = evmc::address{};
    seth_host.tx_context_.block_number = 0;
    seth_host.tx_context_.block_timestamp = 0;
    uint64_t chanin_id = hotstuff::kGlobalChainId;
    sethvm::Uint64ToEvmcBytes32(
        seth_host.tx_context_.chain_id,
        chanin_id);
    seth_host.contract_mgr_ = contract_mgr;
    seth_host.my_address_ = contract_addr;
    seth_host.tx_context_.block_gas_limit = prefund;
    seth_host.view_block_chain_ = http_handler->view_block_chain();
    // user caller prefund 's gas
    uint64_t from_balance = prefund;
    uint64_t to_balance = contract_addr_info->balance();
    seth_host.AddTmpAccountBalance(
        from,
        from_balance);
    seth_host.AddTmpAccountBalance(
        contract_addr,
        to_balance);
    evmc_result evmc_res = {};
    evmc::Result result{ evmc_res };
    int exec_res = sethvm::Execution::Instance()->execute(
        contract_addr_info->bytes_code(),
        input,
        from,
        contract_addr,
        from,
        0,
        prefund,
        0,
        sethvm::kJustCall,
        seth_host,
        &result);
    if (exec_res != sethvm::kSethvmSuccess || result.status_code != EVMC_SUCCESS) {
        std::string res = "query contract failed: " + 
            std::to_string(result.status_code) + 
            ", exec_res: " + std::to_string(exec_res);
        http_res.set_content(res, "text/plain");
        SETH_INFO("query contract error: %s.", res.c_str());
        return;
    }
	
    std::string qdata((char*)result.output_data, result.output_size);
    SETH_INFO("LLLLLhttp: %s, size %d", common::Encode::HexEncode(qdata).c_str(), result.output_size);
    if (result.output_size < 64) {
        auto res = common::Encode::HexEncode(qdata); 
        http_res.set_content(res, "text/plain");
        return;
    }
    evmc_bytes32 len_bytes;
    memcpy(len_bytes.bytes, qdata.c_str() + 32, 32);
    uint64_t len = sethvm::EvmcBytes32ToUint64(len_bytes);
    std::string http_res_str(qdata.c_str() + 64, len);
    http_res.set_content(http_res_str, "text/plain");
    SETH_INFO("query contract success data: %s", http_res_str.c_str());
}

/**
 * Helper function: Encodes a string into pure EVM ABI string format (no selector).
 * Layout (Standard ABI for a single 'string' return):
 * [0:32]  - Offset: 0x00...0020 (Points to the start of the length field)
 * [32:64] - Length: 0x00...00LL (The actual byte length of the string)
 * [64:]   - Data: The string content, right-padded with '\0' to a 32-byte boundary
 */
static std::string EncodeEvmError(const std::string& msg) {
    std::string encoded;
    encoded.reserve(64 + ((msg.size() + 31) / 32) * 32);

    // 1. Offset (32 bytes)
    // For a single return value of type string, the offset is always 32 (0x20)
    uint8_t offset[32] = {0};
    offset[31] = 0x20; 
    encoded.append((char*)offset, 32);

    // 2. Length (32-byte Big-endian uint256)
    uint8_t len_bytes[32] = {0};
    uint32_t msg_len = static_cast<uint32_t>(msg.size());
    for (int i = 0; i < 4; ++i) {
        len_bytes[31 - i] = (msg_len >> (i * 8)) & 0xFF;
    }
    encoded.append((char*)len_bytes, 32);

    // 3. Actual string data
    encoded.append(msg);

    // 4. Padding (Right-pad with zeros to 32-byte boundary)
    if (msg.size() % 32 != 0) {
        size_t padding_size = 32 - (msg.size() % 32);
        encoded.append(padding_size, '\0');
    }

    // Return Hex encoded string with "0x" prefix
    return "0x" + common::Encode::HexEncode(encoded);
}

static void AbiQueryContract(const UWSRequest& req, UWSResponse& http_res) {
    SETH_INFO("query contract coming.");
    auto tmp_contract_addr = req.get_param_value("address");
    auto tmp_input = req.get_param_value("input");
    auto tmp_from = req.get_param_value("from");
    std::string from = common::Encode::HexDecode(tmp_from);
    if (from.size() != common::kUnicastAddressLength) {
        from = common::Encode::HexDecode(std::string(common::kUnicastAddressLength * 2, '0'));
    }

    std::string contract_addr = common::Encode::HexDecode(tmp_contract_addr);
    std::string input = common::Encode::HexDecode(tmp_input);
    uint64_t height = 0;

    // auto contract_prefund_id = contract_addr + from;
    // protos::AddressInfoPtr addr_info =  http_handler->acc_mgr()->GetAccountInfo(contract_prefund_id);
    // if (!addr_info) {
    //     addr_info = prefix_db->GetAddressInfo(contract_prefund_id);
    // }

    // if (!addr_info) {
    //     std::string res = "get from prefund failed: " + std::string(tmp_contract_addr) + ", " + std::string(tmp_from);
    //     http_res.set_content(res, "text/plain");
    //     SETH_INFO("query contract param error: %s.", res.c_str());
    //     return;
    // }

    uint64_t prefund = 9999999999lu;  // addr_info->balance();
    auto contract_addr_info = prefix_db->GetAddressInfo(contract_addr);
    if (contract_addr_info == nullptr) {
        std::string res = "get contract addr failed: " + std::string(tmp_contract_addr);
        http_res.set_content(EncodeEvmError(res), "text/plain");
        SETH_INFO("query contract param error: %s.", res.c_str());
        return;
    }

    if (contract_addr_info->destructed()) {
        std::string res = "get contract addr destructed!";
        http_res.set_content(EncodeEvmError(res), "text/plain");
        SETH_INFO("query contract param error: %s.", res.c_str());
        return;
    }

    sethvm::SethhainHost seth_host;
    seth_host.tx_context_.tx_origin = evmc::address{};
    seth_host.tx_context_.block_coinbase = evmc::address{};
    seth_host.tx_context_.block_number = 0;
    seth_host.tx_context_.block_timestamp = 0;
    uint64_t chanin_id = hotstuff::kGlobalChainId;
    sethvm::Uint64ToEvmcBytes32(
        seth_host.tx_context_.chain_id,
        chanin_id);
    seth_host.contract_mgr_ = contract_mgr;
    seth_host.my_address_ = contract_addr;
    seth_host.tx_context_.block_gas_limit = prefund;
    // user caller prefund 's gas
    uint64_t from_balance = prefund;
    uint64_t to_balance = contract_addr_info->balance();
    seth_host.view_block_chain_ = http_handler->view_block_chain();
    seth_host.AddTmpAccountBalance(
        from,
        from_balance);
    seth_host.AddTmpAccountBalance(
        contract_addr,
        to_balance);
    evmc_result evmc_res = {};
    evmc::Result result{ evmc_res };
    int exec_res = sethvm::Execution::Instance()->execute(
        contract_addr_info->bytes_code(),
        input,
        from,
        contract_addr,
        from,
        0,
        999999999999lu,
        0,
        sethvm::kJustCall,
        seth_host,
        &result);
    if (exec_res != sethvm::kSethvmSuccess || result.status_code != EVMC_SUCCESS) {
        std::string res = "query contract failed: " + 
            std::to_string(result.status_code) + 
            ", exec_res: " + std::to_string(exec_res);
        http_res.set_content(EncodeEvmError(res), "text/plain");
        SETH_INFO("query contract error: %s.", res.c_str());
        return;
    }
	
    std::string qdata((char*)result.output_data, result.output_size);
    auto hex_data = common::Encode::HexEncode(qdata);
    // SETH_INFO("LLLLLhttp: %s, size %d", common::Encode::HexEncode(qdata).c_str(), result.output_size);
    // if (result.output_size < 64) {
    //     auto res = common::Encode::HexEncode(qdata); 
    //     evbuffer_add(req->buffer_out, res.c_str(), res.size());
    //     evhtp_send_reply(req, EVHTP_RES_OK);
    //     return;
    // }
    // evmc_bytes32 len_bytes;
    // memcpy(len_bytes.bytes, qdata.c_str() + 32, 32);
    // uint64_t len = sethvm::EvmcBytes32ToUint64(len_bytes);
    // std::string http_res(qdata.c_str() + 64, len);
    http_res.set_content(hex_data, "text/plain");
    SETH_INFO("query contract success data: %s", hex_data.c_str());
}

// Returns leader routing table: pool_index -> {ip, port} for the local shard.
// Uses hotstuff_mgr_->is_other_leader(pool_index) to get the actual current
// leader for each pool. If this node IS the leader for a pool, returns this
// node's own IP:port for that pool.
static void QueryLeaders(const UWSRequest& req, UWSResponse& http_res) {
    nlohmann::json res_json;
    res_json["status"] = 0;
    res_json["leaders"] = nlohmann::json::object();

    auto hotstuff_mgr = http_handler->hotstuff_mgr();
    if (!hotstuff_mgr) {
        res_json["status"] = 1;
        res_json["msg"] = "hotstuff manager not available";
        http_res.set_content(res_json.dump(), "application/json");
        return;
    }

    auto network_id = common::GlobalInfo::Instance()->network_id();
    std::string local_ip = common::GlobalInfo::Instance()->config_public_ip();
    uint16_t local_port = common::GlobalInfo::Instance()->config_public_port();
    // Fallback: if config_public_port is 0, use config_local_port
    if (local_port == 0) {
        local_port = common::GlobalInfo::Instance()->config_local_port();
    }
    res_json["network_id"] = network_id;

    for (uint32_t pool_idx = 0; pool_idx < common::kImmutablePoolSize; ++pool_idx) {
        auto leader = hotstuff_mgr->GetLeader(pool_idx);
        nlohmann::json leader_info;
        if (leader && leader->public_ip != 0 && leader->public_port != 0) {
            leader_info["ip"] = common::Uint32ToIp(leader->public_ip);
            leader_info["port"] = leader->public_port;
            leader_info["index"] = leader->index;
        } else {
            // Leader unknown or has no valid ip/port — return self
            leader_info["ip"] = local_ip;
            leader_info["port"] = local_port;
            leader_info["index"] = -1;
        }
        res_json["leaders"][std::to_string(pool_idx)] = leader_info;
    }

    res_json["pool_count"] = common::kImmutablePoolSize;
    http_res.set_content(res_json.dump(), "application/json");
    SETH_INFO("query_leaders: %s", res_json.dump().c_str());
}

static void QueryAccount(const UWSRequest& req, UWSResponse& http_res) {
    auto tmp_addr = req.get_param_value("address");
    SETH_INFO("coming query account: %s", tmp_addr.c_str());
    if (tmp_addr.empty()) {
        std::string res = common::StringUtil::Format("param address is null");
        http_res.set_content(res, "text/plain");
        SETH_INFO("%s", res.c_str());
        return;
    }

    std::string addr = common::Encode::HexDecode(tmp_addr);
    auto addr_info = prefix_db->GetAddressInfo(addr);
    if (addr_info == nullptr) {
        std::string res = "get address failed from db: " + tmp_addr;
        addr_info =  http_handler->acc_mgr()->GetAccountInfo(addr);
    }

    if (addr_info == nullptr) {
        std::string res = "get address failed from cache: " + tmp_addr;
        http_res.set_content(res, "text/plain");
        SETH_INFO("%s", res.c_str());
        return;
    }

    std::string json_str;
    auto st = google::protobuf::util::MessageToJsonString(*addr_info, &json_str);
    if (!st.ok()) {
        std::string res = "json parse failed: " + addr;
        http_res.set_content(res, "text/plain");
        SETH_INFO("%s", res.c_str());
        return;
    }

    http_res.set_content(json_str, "text/plain");
    SETH_INFO("%s", json_str.c_str());
}

// Batch query multiple accounts at once.
// Input:  POST param "addresses" — comma-separated hex addresses (max 500).
// Output: JSON { "status":0, "accounts": { "<addr>": { "nonce":"...", "balance":"..." }, ... }, "not_found": ["addr1",...] }
static void BatchQueryAccounts(const UWSRequest& req, UWSResponse& http_res) {
    auto tmp_addrs = req.get_param_value("addresses");
    if (tmp_addrs.empty()) {
        nlohmann::json err;
        err["status"] = 1;
        err["msg"] = "param addresses is empty";
        http_res.set_content(err.dump(), "application/json");
        return;
    }

    auto addrs_splits = common::Split<2048>(tmp_addrs.c_str(), ',');
    if (addrs_splits.Count() == 0) {
        nlohmann::json err;
        err["status"] = 1;
        err["msg"] = "no valid addresses";
        http_res.set_content(err.dump(), "application/json");
        return;
    }

    if (addrs_splits.Count() > 500) {
        nlohmann::json err;
        err["status"] = 1;
        err["msg"] = "too many addresses, max 500";
        http_res.set_content(err.dump(), "application/json");
        return;
    }

    nlohmann::json res_json;
    res_json["status"] = 0;
    res_json["msg"] = "ok";
    nlohmann::json accounts_json = nlohmann::json::object();
    nlohmann::json not_found_json = nlohmann::json::array();
    uint32_t from_prefix_db = 0;
    uint32_t from_acc_mgr = 0;

    for (uint32_t i = 0; i < addrs_splits.Count(); ++i) {
        std::string hex_addr(addrs_splits[i]);
        if (hex_addr.empty()) continue;

        std::string addr = common::Encode::HexDecode(hex_addr);
        if (addr.length() < 20) {
            not_found_json.push_back(hex_addr);
            continue;
        }

        auto addr_info = prefix_db->GetAddressInfo(addr);
        if (addr_info == nullptr) {
            addr_info = http_handler->acc_mgr()->GetAccountInfo(addr);
            if (addr_info != nullptr) {
                ++from_acc_mgr;
            }
        } else {
            ++from_prefix_db;
        }

        // For prepayment addresses (40 bytes = contract + user), also try
        // looking up by the first 20 bytes (contract address) pool.
        if (addr_info == nullptr && addr.length() == common::kPreypamentAddressLength) {
            SETH_INFO("batch_query: prepayment addr not found: %s (len=%u)",
                hex_addr.c_str(), (uint32_t)addr.length());
        }

        if (addr_info == nullptr) {
            not_found_json.push_back(hex_addr);
            continue;
        }

        nlohmann::json acc;
        acc["nonce"] = std::to_string(addr_info->nonce());
        acc["balance"] = std::to_string(addr_info->balance());
        // Use the on-chain pool_index if available (correct for contracts),
        // fall back to address-based computation for normal accounts.
        if (addr_info->has_pool_index() && addr_info->pool_index() < common::kInvalidPoolIndex) {
            acc["pool_index"] = addr_info->pool_index();
        } else {
            acc["pool_index"] = common::GetAddressPoolIndex(addr);
        }
        accounts_json[hex_addr] = acc;
    }

    res_json["accounts"] = accounts_json;
    res_json["not_found"] = not_found_json;
    auto json_str = res_json.dump();
    http_res.set_content(json_str, "application/json");

    std::string details;
    uint32_t detail_idx = 0;
    for (auto it = accounts_json.begin(); it != accounts_json.end() && detail_idx < 20; ++it, ++detail_idx) {
        if (!details.empty()) {
            details += " | ";
        }
        details += it.key();
        details += "{len=" + std::to_string(it.key().size());
        details += ",nonce=" + it.value()["nonce"].get<std::string>();
        details += ",balance=" + it.value()["balance"].get<std::string>();
        details += ",pool=" + std::to_string(it.value()["pool_index"].get<uint32_t>()) + "}";
    }
    if (accounts_json.size() > 20) {
        details += " | ... +" + std::to_string(accounts_json.size() - 20) + " more";
    }

    SETH_WARN("batch_query_accounts: requested=%u found=%u not_found=%u "
        "prefix_db=%u acc_mgr=%u resp_bytes=%zu details=[%s]",
        addrs_splits.Count(),
        (uint32_t)accounts_json.size(),
        (uint32_t)not_found_json.size(),
        from_prefix_db,
        from_acc_mgr,
        json_str.size(),
        details.empty() ? "-" : details.c_str());
    if (!not_found_json.empty()) {
        std::string nf_list;
        for (uint32_t i = 0; i < not_found_json.size() && i < 5; ++i) {
            if (!nf_list.empty()) nf_list += ",";
            nf_list += not_found_json[i].get<std::string>();
        }
        if (not_found_json.size() > 5) {
            nf_list += ",... +" + std::to_string(not_found_json.size() - 5) + " more";
        }
        SETH_WARN("batch_query not_found addrs: [%s]", nf_list.c_str());
    }
}

static void QueryAccountTxs(const UWSRequest& req, UWSResponse& http_res) {
    nlohmann::json res_json;
    auto tmp_addr = req.get_param_value("address");
    std::string hex_addr;
    if (!NormalizeHexAddress(tmp_addr, &hex_addr)) {
        res_json["status"] = 1;
        res_json["msg"] = "param address is invalid";
        http_res.set_content(res_json.dump(), "application/json");
        return;
    }

    uint32_t limit = 20;
    uint32_t offset = 0;
    auto limit_str = req.get_param_value("limit");
    if (!limit_str.empty() && !common::StringUtil::ToUint32(limit_str, &limit)) {
        res_json["status"] = 1;
        res_json["msg"] = "param limit is invalid";
        http_res.set_content(res_json.dump(), "application/json");
        return;
    }

    auto offset_str = req.get_param_value("offset");
    if (!offset_str.empty() && !common::StringUtil::ToUint32(offset_str, &offset)) {
        res_json["status"] = 1;
        res_json["msg"] = "param offset is invalid";
        http_res.set_content(res_json.dump(), "application/json");
        return;
    }

    if (limit == 0) {
        limit = 20;
    } else if (limit > 200) {
        limit = 200;
    }

    std::vector<protos::PrefixDb::UserTxItem> txs;
    if (!prefix_db->GetUserTxs(common::Encode::HexDecode(hex_addr), limit, offset, &txs)) {
        res_json["status"] = 1;
        res_json["msg"] = "query account transactions failed";
        http_res.set_content(res_json.dump(), "application/json");
        return;
    }

    nlohmann::json txs_json = nlohmann::json::array();
    for (const auto& item : txs) {
        const auto& tx = item.tx;
        nlohmann::json tx_json;
        tx_json["height"] = std::to_string(item.height);
        tx_json["txIndex"] = item.tx_index;
        tx_json["nonce"] = std::to_string(tx.nonce());
        tx_json["from"] = common::Encode::HexEncode(tx.from());
        tx_json["to"] = common::Encode::HexEncode(tx.to());
        tx_json["amount"] = std::to_string(tx.amount());
        tx_json["gasLimit"] = std::to_string(tx.gas_limit());
        tx_json["gasUsed"] = std::to_string(tx.gas_used());
        tx_json["gasPrice"] = std::to_string(tx.gas_price());
        tx_json["balance"] = std::to_string(tx.balance());
        tx_json["step"] = tx.step();
        tx_json["status"] = tx.status();
        tx_json["contractPrefund"] = std::to_string(tx.contract_prefund());
        tx_json["txHash"] = common::Encode::HexEncode(tx.tx_hash());
        tx_json["uniqueHash"] = common::Encode::HexEncode(tx.unique_hash());
        txs_json.push_back(tx_json);
    }

    res_json["status"] = 0;
    res_json["msg"] = "ok";
    res_json["address"] = hex_addr;
    res_json["limit"] = limit;
    res_json["offset"] = offset;
    res_json["transactions"] = txs_json;
    http_res.set_content(res_json.dump(), "application/json");
    SETH_INFO("query_account_txs: address=%s, limit=%u, offset=%u, count=%u",
        hex_addr.c_str(), limit, offset, static_cast<uint32_t>(txs.size()));
}

static void AccountsValid(const UWSRequest& req, UWSResponse& http_res) {
    SETH_INFO("query account.");
    auto balance = req.get_param_value("balance");
    uint64_t balance_val = 0;
    if (!common::StringUtil::ToUint64(balance, &balance_val)) {
        std::string res = std::string("balance not integer: ") + balance;
        http_res.set_content(res, "text/plain");
        return;
    }

    auto tmp_addrs = req.get_param_value("addrs");
    if (tmp_addrs.empty()) {
        std::string res = common::StringUtil::Format("param address is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    nlohmann::json res_json;
    auto tmp_res_addrs = res_json["addrs"];
    res_json["status"] = 0;
    res_json["msg"] = "success";
    auto addrs_splits = common::Split<1024>(tmp_addrs.c_str(), '_');
    uint32_t invalid_addr_index = 0;
    for (uint32_t i = 0; i < addrs_splits.Count(); ++i) {
        std::string addr = common::Encode::HexDecode(addrs_splits[i]);
        if (addr.length() < 20) {
            continue;
        }

        protos::AddressInfoPtr addr_info =  http_handler->acc_mgr()->GetAccountInfo(addr);
        if (addr_info == nullptr) {
            std::string res = "get address failed from db: " + addr;
            addr_info = prefix_db->GetAddressInfo(addr);
        }

        if (addr_info == nullptr) {
            addr_info = nullptr;
        }

        if (addr_info != nullptr && addr_info->balance() >= balance_val) {
            res_json["addrs"][invalid_addr_index++] = addrs_splits[i];
            SETH_INFO("valid addr: %s, balance: %lu", addrs_splits[i], addr_info->balance());
        } else {
            SETH_INFO("invalid addr: %s, balance: %lu",
                addrs_splits[i], 
                (addr_info ? addr_info->balance() : 0));
        }
    }

    auto json_str = res_json.dump();
    http_res.set_content(json_str, "text/plain");
}

static void GetBlockWithGid(const UWSRequest& req, UWSResponse& http_res) {
    SETH_INFO("query account.");
    auto addr = req.get_param_value("addr");
    if (addr.empty()) {
        std::string res = std::string("addr not exists.");
        http_res.set_content(res, "text/plain");
        return;
    }

    auto nonce = req.get_param_value("nonce");
    if (nonce.empty()) {
        std::string res = std::string("nonce not exists.");
        http_res.set_content(res, "text/plain");
        return;
    }

    uint64_t tmp_nonce = 0;
    if (!common::StringUtil::ToUint64(nonce, &tmp_nonce)) {
        std::string res = std::string("nonce not exists.");
        http_res.set_content(res, "text/plain");
        return;
    }

    nlohmann::json res_json;
    res_json["status"] = 0;
    res_json["msg"] = "success";
    view_block::protobuf::ViewBlockItem view_block;
    bool res = prefix_db->GetBlockWithHeight(3, 15, 3, &view_block);
    if (res) {
        res_json["block"]["height"] = view_block.block_info().height();
        res_json["block"]["hash"] = common::Encode::HexEncode(view_block.qc().view_block_hash());
        res_json["block"]["parent_hash"] = common::Encode::HexEncode(view_block.parent_hash());
        res_json["block"]["timestamp"] = view_block.block_info().timestamp();
        res_json["block"]["no"] = view_block.qc().view();
    }
       
    auto json_str = res_json.dump();
    SETH_INFO("success get addr: %s, nonce: %lu, res: %s", addr, tmp_nonce, json_str.c_str());
    http_res.set_content(res_json, "text/plain");
}

static void PrefundsValid(const UWSRequest& req, UWSResponse& http_res) {
    SETH_INFO("query account.");
    auto balance = req.get_param_value("balance");
    if (balance.empty()) {
        std::string res = std::string("balance not exists.");
        http_res.set_content(res, "text/plain");
        return;
    }

    uint64_t balance_val = 0;
    if (!common::StringUtil::ToUint64(std::string(balance), &balance_val)) {
        std::string res = std::string("balance not integer: ") + balance;
        http_res.set_content(res, "text/plain");
        return;
    }

    auto contract = req.get_param_value("contract");
    if (contract.empty()) {
        std::string res = std::string("contract not exists.");
        http_res.set_content(res, "text/plain");
        return;
    }

    auto contract_addr = common::Encode::HexDecode(contract);
    auto tmp_addrs = req.get_param_value("addrs");
    if (tmp_addrs.empty()) {
        std::string res = common::StringUtil::Format("param address is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    nlohmann::json res_json;
    auto tmp_res_addrs = res_json["prefunds"];
    res_json["status"] = 0;
    res_json["msg"] = "success";
    auto addrs_splits = common::Split<1024>(tmp_addrs.c_str(), '_');
    uint32_t invalid_addr_index = 0;
    for (uint32_t i = 0; i < addrs_splits.Count(); ++i) {
        std::string addr = common::Encode::HexDecode(addrs_splits[i]);
        if (addr.length() < 20) {
            continue;
        }

        uint64_t height = 0;
        uint64_t tmp_balance = 0;
        auto contract_prefund_id = contract_addr + addr;
        protos::AddressInfoPtr addr_info =  http_handler->acc_mgr()->GetAccountInfo(contract_prefund_id);
        if (addr_info == nullptr) {
            std::string res = "get address failed from db: " + contract_prefund_id;
            addr_info = prefix_db->GetAddressInfo(contract_prefund_id);
        }

        if (addr_info != nullptr && addr_info->balance() >= balance_val) {
            res_json["prefunds"][invalid_addr_index++] = addrs_splits[i];
            SETH_INFO("valid prefund: %s, balance: %lu", addrs_splits[i], addr_info->balance());
        } else {
            SETH_INFO("invalid prefund: %s, balance: %lu",
                addrs_splits[i], 
                (addr_info ? addr_info->balance() : 0));
        }
    }

    auto json_str = res_json.dump();
    http_res.set_content(json_str, "text/plain");
}

static void GidsValid(const UWSRequest& req, UWSResponse& http_res) {
    auto tmp_gids = req.get_param_value("gids");
    if (tmp_gids.empty()) {
        std::string res = common::StringUtil::Format("param gids is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    nlohmann::json res_json;
    auto tmp_res_addrs = res_json["gids"];
    res_json["status"] = 0;
    res_json["msg"] = "success";
    auto addrs_splits = common::Split<1024>(tmp_gids.c_str(), '_');
    uint32_t invalid_addr_index = 0;
    for (uint32_t i = 0; i < addrs_splits.Count(); ++i) {
        std::string gid = common::Encode::HexDecode(addrs_splits[i]);
        if (gid.length() < 32) {
            continue;
        }

        SETH_INFO("now get tx gid: %s", common::Encode::HexEncode(gid).c_str());
        auto res = false; //prefix_db->JustCheckCommitedGidExists(gid);
        if (res) {
            res_json["gids"][invalid_addr_index++] = addrs_splits[i];
            SETH_INFO("success get tx gid: %s", common::Encode::HexEncode(gid).c_str());
        }
    }

    auto json_str = res_json.dump();
    http_res.set_content(json_str, "text/plain");
}

static void GetProxyReencInfo(const UWSRequest& req, UWSResponse& http_res) {
    auto id = req.get_param_value("id");
    if (id.empty()) {
        std::string res = common::StringUtil::Format("param address is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    auto contract = req.get_param_value("contract");
    if (contract.empty()) {
        std::string res = common::StringUtil::Format("param contract is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    auto count_str = req.get_param_value("count");
    if (count_str.empty()) {
        std::string res = common::StringUtil::Format("param count is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    std::string proxy_id = id;
    std::string contract_str = common::Encode::HexDecode(contract);
    uint32_t count = 0;
    if (!common::StringUtil::ToUint32(count_str, &count) || count > 10) {
        std::string res = common::StringUtil::Format("param count is null");
        http_res.set_content(res, "text/plain");
        return;
    }
    
    nlohmann::json res_json;
    auto bls_pk_json = res_json["value"];
    res_json["status"] = 0;
    res_json["msg"] = "success";
    SETH_WARN("GetProxyReencInfo 4.");
    for (uint32_t i = 0; i < count; ++i) {
        auto private_key = proxy_id + "_" + std::string("init_prikey_") + std::to_string(i);
        std::string prikey;
        sethvm::Execution::Instance()->GetStorage(contract_str, private_key, &prikey);
        auto public_key = proxy_id + "_" + std::string("init_pubkey_") + std::to_string(i);
        std::string pubkey;
        sethvm::Execution::Instance()->GetStorage(contract_str, public_key, &pubkey);
        SETH_WARN("contract_reencryption get member private and public key: %s, %s sk: %s, pk: %s",
            common::Encode::HexEncode(private_key).c_str(), 
            common::Encode::HexEncode(public_key).c_str(), 
            common::Encode::HexEncode(prikey).c_str(),
            common::Encode::HexEncode(pubkey).c_str());

        
        res_json["value"][i]["node_index"] = i;
        res_json["value"][i]["private_key"] = common::Encode::HexEncode(prikey);
        res_json["value"][i]["public_key"] = common::Encode::HexEncode(pubkey);
    }
   
    auto json_str = res_json.dump();
    http_res.set_content(json_str, "text/plain");
}


static void GetSecAndEncData(const UWSRequest& req, UWSResponse& http_res) {
    SETH_INFO("http transaction coming.");
    contract::ContractReEncryption prox_renc;
    sethvm::SethhainHost seth_host;
    contract::CallParameters param;
    param.seth_host = &seth_host;
    auto data = req.get_param_value("data");
    if (data.empty()) {
        std::string res = common::StringUtil::Format("param data is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    std::string hash256 = common::Hash::Hash256(data);
    std::string test_data(common::Encode::HexEncode(hash256));
    std::string pair_param = ("type a\n"
        "q 8780710799663312522437781984754049815806883199414208211028653399266475630880222957078625179422662221423155858769582317459277713367317481324925129998224791\n"
        "h 12016012264891146079388821366740534204802954401251311822919615131047207289359704531102844802183906537786776\n"
        "r 730750818665451621361119245571504901405976559617\n"
        "exp2 159\n"
        "exp1 107\n"
        "sign1 1\n"
        "sign0 1\n");
    auto pairing_ptr = std::make_shared<Pairing>(pair_param.c_str(), pair_param.size());
    auto& e = *pairing_ptr;
    GT m(e, test_data.c_str(), test_data.size());
    auto seckey = common::Hash::Hash256(m.toString());
    std::string sec_data;
    auto raw_key = std::make_pair(seckey.c_str(), seckey.size());
    secptr->Encrypt(data, raw_key, &sec_data);
    SETH_WARN("get m data src data: %s, hex data: %s, m: %s, hash sec: %s, sec data: %s", 
        test_data.c_str(), 
        common::Encode::HexEncode(test_data).c_str(),
        common::Encode::HexEncode(m.toString()).c_str(), 
        common::Encode::HexEncode(hash256).c_str(),
        common::Encode::HexEncode(sec_data).c_str());
    nlohmann::json res_json;
    res_json["status"] = 0;
    res_json["seckey"] = common::Encode::HexEncode(m.toString());
    res_json["hash_seckey"] = common::Encode::HexEncode(hash256);
    res_json["secdata"] = common::Encode::HexEncode(sec_data);
    prefix_db->SaveTemporaryKv(std::string("proxy_reenc_") + seckey, sec_data);
    auto json_str = res_json.dump();
    http_res.set_content(json_str, "text/plain");
}

static void ProxDecryption(const UWSRequest& req, UWSResponse& http_res) {
    SETH_WARN("ProxDecryption coming 0.");
    contract::ContractReEncryption prox_renc;
    sethvm::SethhainHost seth_host;
    contract::CallParameters param;
    param.from = common::Encode::HexDecode("48e1eab96c9e759daa3aff82b40e77cd615a41d0");
    param.seth_host = &seth_host;
    auto id = req.get_param_value("id");
    if (id.empty()) {
        std::string res = common::StringUtil::Format("param data is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    SETH_WARN("ProxDecryption coming 2.");
    std::string res_data;
    prox_renc.Decryption(param, "", std::string(id) + ";", &res_data);
    std::string hash256 = common::Hash::Hash256(res_data);
    std::string tmp_encdata;
    if (!prefix_db->GetTemporaryKv(std::string("proxy_reenc_") + hash256, &tmp_encdata)) {
        std::string res = common::StringUtil::Format("get encdata is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    block::protobuf::KeyValueInfo kv_info;
    if (!kv_info.ParseFromString(tmp_encdata)) {
        std::string res = common::StringUtil::Format("get encdata is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    std::string dec_data;
    auto raw_key = std::make_pair(hash256.c_str(), hash256.size());
    secptr->Decrypt(kv_info.value(), raw_key, &dec_data);
    SETH_WARN("get m data src data: %s, hex data: %s, m: %s, hash sec: %s, sec data: %s", 
        dec_data.c_str(), 
        common::Encode::HexEncode(dec_data).c_str(),
        common::Encode::HexEncode(res_data).c_str(), 
        common::Encode::HexEncode(hash256).c_str(),
        common::Encode::HexEncode(kv_info.value()).c_str());
    nlohmann::json res_json;
    res_json["status"] = 0;
    res_json["seckey"] = common::Encode::HexEncode(res_data);
    res_json["hash_seckey"] = common::Encode::HexEncode(hash256);
    res_json["encdata"] = common::Encode::HexEncode(kv_info.value());
    res_json["decdata"] = std::string(dec_data.c_str());
    auto json_str = res_json.dump();
    SETH_WARN("ProxDecryption coming 3.");
    http_res.set_content(json_str, "text/plain");
    SETH_WARN("ProxDecryption coming 4.");
}

static void ArsCreateSecKeys(const UWSRequest& req, UWSResponse& http_res) {
    SETH_WARN("ArsCreateSecKeys coming 0.");
    auto keys = req.get_param_value("keys");
    if (keys.empty()) {
        std::string res = common::StringUtil::Format("param keys is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    auto tmp_signer_count = req.get_param_value("signer_count");
    if (tmp_signer_count.empty()) {
        std::string res = common::StringUtil::Format("param signer_count is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    contract::ContractArs ars;
    auto signer_count = 0;
    if (!common::StringUtil::ToInt32(tmp_signer_count, &signer_count)) {
        std::string res = common::StringUtil::Format("get signer_count is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    if (signer_count <= 0 || signer_count >= ars.ring_size()) {
        std::string res = common::StringUtil::Format("get signer_count is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    // Create public and private key pairs in the ring
    contract::ArsElementVector private_keys(ars.ring_size());
    contract::ArsElementVector public_keys(ars.ring_size());
    nlohmann::json res_json;
    res_json["status"] = 0;
    auto nodes = res_json["nodes"];
    auto keys_splits = common::Split<>(keys.c_str(), '-');
    ars.set_ring_size(keys_splits.Count());
    for (int i = 0; i < ars.ring_size(); ++i) {
        ars.KeyGen(keys_splits[i], private_keys[i].value, public_keys[i].value);
        unsigned char bytes_data[10240] = {0};
        auto len = element_to_bytes(bytes_data, private_keys[i].value);
        std::string x_i_str((char*)bytes_data, len);
        len = element_to_bytes_compressed(bytes_data, public_keys[i].value);
        std::string y_i_str((char*)bytes_data, len);
        res_json["nodes"][i]["node_index"] = i;
        res_json["nodes"][i]["private_key"] = common::Encode::HexEncode(x_i_str);
        res_json["nodes"][i]["public_key"] = common::Encode::HexEncode(y_i_str);
        element_clear(private_keys[i].value);
        element_clear(public_keys[i].value);
    }

    auto json_str = res_json.dump();
    SETH_WARN("ArsCreateSecKeys coming 3.");
    http_res.set_content(json_str, "text/plain");
    SETH_WARN("ArsCreateSecKeys coming 4.");
}

static void QueryInit(const UWSRequest& req, UWSResponse& http_res) {
    auto thread_index = 0;//common::GlobalInfo::Instance()->get_thread_index();
    std::string res = "ok";
    http_res.set_content(res, "text/plain");
    SETH_INFO("sunccess init http ser: %d", thread_index);
}

static void GetBlocks(const UWSRequest& req, UWSResponse& http_res) {
    auto network = req.get_param_value("network");
    if (network.empty()) {
        std::string res = common::StringUtil::Format("param network is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    auto pool = req.get_param_value("pool_index");
    if (pool.empty()) {
        std::string res = common::StringUtil::Format("param pool is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    auto height = req.get_param_value("height");
    if (height.empty()) {
        std::string res = common::StringUtil::Format("param height is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    uint32_t network_id = 0;
    if (!common::StringUtil::ToUint32(network, &network_id)) {
        std::string res = common::StringUtil::Format("param network is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    if (!network::IsSameToLocalShard(network_id)) {
        std::string res = common::StringUtil::Format("param network invalid");
        http_res.set_content(res, "text/plain");
        return;
    }

    uint32_t pool_index = 0;
    if (!common::StringUtil::ToUint32(pool, &pool_index)) {
        std::string res = common::StringUtil::Format("param pool is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    uint64_t height_val = 0;
    if (!common::StringUtil::ToUint64(height, &height_val)) {
        std::string res = common::StringUtil::Format("param height is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    uint32_t count_val = 1;
    auto count = req.get_param_value("count");
    if (!count.empty()) {
        common::StringUtil::ToUint32(count, &count_val);
    }

    if (count_val > 128) {
        count_val = 128;
    }

    nlohmann::json res_json;
    res_json["status"] = 0;
    res_json["blocks"] = nlohmann::json::array();
    for (uint32_t i = 0; i < count_val; ++i) {
        view_block::protobuf::ViewBlockItem view_block;
        bool res = prefix_db->GetBlockWithHeight(network_id, pool_index, height_val + i, &view_block);
        if (!res) {
            break;
        }

        res_json["blocks"].push_back(nlohmann::json::parse(HttpProtobufToJson(view_block)));
    }

    auto json_str = res_json.dump();
    http_res.set_content(json_str, "text/plain");
}

static void GetLatestPoolHeights(const UWSRequest& req, UWSResponse& http_res) {
    auto network = req.get_param_value("network");
    if (network.empty()) {
        std::string res = common::StringUtil::Format("param network is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    uint32_t network_id = 0;
    if (!common::StringUtil::ToUint32(network, &network_id)) {
        std::string res = common::StringUtil::Format("param network is null");
        http_res.set_content(res, "text/plain");
        return;
    }

    if (!network::IsSameToLocalShard(network_id)) {
        std::string res = common::StringUtil::Format("param network invalid");
        http_res.set_content(res, "text/plain");
        return;
    }

    nlohmann::json res_json;
    res_json["status"] = 0;
    res_json["pools"] = nlohmann::json::array();
    for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
        pools::protobuf::PoolLatestInfo pool_info;
        bool res = prefix_db->GetLatestPoolInfo(network_id, i, &pool_info);
        if (!res) {
            res_json["pools"].push_back(nlohmann::json::parse("{}"));
        } else {
            res_json["pools"].push_back(nlohmann::json::parse(HttpProtobufToJson(pool_info)));
        }
    }

    auto json_str = res_json.dump();
    http_res.set_content(json_str, "text/plain");
}

static void GetBlockWithHash(const UWSRequest& req, UWSResponse& http_res) {
    nlohmann::json res_json;
    res_json["status"] = 0;
    res_json["blocks"] = nlohmann::json::array();
    nlohmann::json req_json;
    try {
        req_json = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        res_json["status"] = 1;
        res_json["error"] = "Invalid JSON format";
        http_res.set_content(res_json.dump(), "application/json");
        return;
    }

    if (!req_json.contains("hash_list") || !req_json["hash_list"].is_array()) {
        res_json["status"] = 1;
        res_json["error"] = "param hash_list is required and must be an array";
        http_res.set_content(res_json.dump(), "application/json");
        return;
    }

    auto hash_list = req_json["hash_list"];
    int32_t count = 0;
    for (auto& hash_val : hash_list) {
        if (!hash_val.is_string()) {
            continue;
        }

        ++count;
        if (count > 128) {
            break;
        }

        std::string block_hash = common::Encode::HexDecode(hash_val.get<std::string>());
        view_block::protobuf::ViewBlockItem view_block;
        bool res = prefix_db->GetBlock(block_hash, &view_block);
        if (res) {
            try {
                res_json["blocks"].push_back(nlohmann::json::parse(HttpProtobufToJson(view_block)));
            } catch (...) {
                SETH_ERROR("Parse block json failed for hash: %s", block_hash.c_str());
            }
        }
    }

    http_res.set_content(res_json.dump(), "application/json");
}

static void TransactionReceipt(const UWSRequest& req, UWSResponse& http_res) {
    nlohmann::json res_json;
    res_json["status"] = transport::kUnkonwn;
    res_json["msg"] = transport::MessageStatusToString(res_json["status"]);
    if (!req.has_param("tx_hash")) {
        res_json["status"] = transport::kRequestInvalid;
        res_json["msg"] = std::string("not has tx hash param");
        http_res.set_content(res_json.dump(), "application/json");
        return;
    }

    auto tx_hash = common::Encode::HexDecode(req.get_param_value("tx_hash"));
    std::string res;
    auto addr = evmc::address{};
    auto id = std::string("tx");
    memcpy(addr.bytes, id.c_str(), id.size());
    if (prefix_db->GetTemporaryKv(std::string((char*)addr.bytes, sizeof(addr.bytes)) + tx_hash, &res)) {
        block::protobuf::KeyValueInfo kv_info;
        if (kv_info.ParseFromString(res)) {
            block::protobuf::TxHashStatus tx_status;
            if (!tx_status.ParseFromString(kv_info.value())) {
                res_json["status"] = transport::kUnkonwn;
                res_json["msg"] = transport::MessageStatusToString(res_json["status"]);
            } else {
                try {
                    res_json = nlohmann::json::parse(HttpProtobufToJson(tx_status));
                    res_json["msg"] = transport::MessageStatusToString(res_json["status"]);;
                } catch (std::exception& e) {

                }
            }
        } else {
            res_json["status"] = transport::kUnkonwn;
            res_json["msg"] = transport::MessageStatusToString(res_json["status"]);
        }

        {
            std::lock_guard<std::mutex> lock(http_handler->tx_msg_map_mutex());
            http_handler->tx_msg_map().Remove(tx_hash);
        }
    } else {
        transport::MessagePtr msg_ptr = nullptr;
        {
            std::lock_guard<std::mutex> lock(http_handler->tx_msg_map_mutex());
            http_handler->tx_msg_map().Get(tx_hash, msg_ptr);
        }

        if (msg_ptr) {
            res_json["status"] = (int32_t)msg_ptr->handle_status.load();
            res_json["msg"] = transport::MessageStatusToString(res_json["status"]);
        } else {
            res_json["status"] = transport::kNotExists;
            res_json["msg"] = transport::MessageStatusToString(res_json["status"]);
        }
    }
    
    SETH_INFO("transaction receipt query, tx hash: %s, res: %s", 
        req.get_param_value("tx_hash").c_str(), res_json.dump().c_str());
    http_res.set_content(res_json.dump(), "application/json");
}

static void UpdatePrivateKey(const UWSRequest& req, UWSResponse& http_res) {
    SETH_INFO("Update private key request received.");
    
    nlohmann::json res_json;
    res_json["status"] = 1;
    res_json["msg"] = "failed";
    
    // Get private key parameter
    auto private_key_hex = req.get_param_value("private_key");
    if (private_key_hex.empty()) {
        res_json["msg"] = "private_key parameter is required";
        http_res.set_content(res_json.dump(), "application/json");
        SETH_ERROR("Update private key failed: private_key parameter is empty");
        return;
    }
    
    // Decode private key
    std::string private_key = common::Encode::HexDecode(private_key_hex);
    if (private_key.empty()) {
        res_json["msg"] = "invalid private_key format (must be hex)";
        http_res.set_content(res_json.dump(), "application/json");
        SETH_ERROR("Update private key failed: invalid hex format");
        return;
    }
    
    // Call callback function to update private key
    if (!http_handler->private_key_update_callback_) {
        res_json["msg"] = "private key update callback not set";
        http_res.set_content(res_json.dump(), "application/json");
        SETH_ERROR("Update private key failed: callback not set");
        return;
    }
    
    int result = http_handler->private_key_update_callback_(private_key);
    if (result == 0) {
        res_json["status"] = 0;
        res_json["msg"] = "success";
        SETH_INFO("Private key updated successfully");
    } else {
        res_json["msg"] = "failed to update private key";
        SETH_ERROR("Update private key failed: callback returned error %d", result);
    }
    
    http_res.set_content(res_json.dump(), "application/json");
}

HttpHandler::HttpHandler() {
    http_handler = this;
}

HttpHandler::~HttpHandler() {
    running_ = false;
    if (http_svr_thread_ && http_svr_thread_->joinable()) {
        http_svr_thread_->join();
    }
}

// ── MetaMask / Ethereum JSON-RPC helpers ─────────────────────────────────────
// Chain ID for Seth — matches hotstuff::kGlobalChainId / kGlobalChainIdValue.
static constexpr uint64_t kSethChainId = hotstuff::kGlobalChainIdValue;

static inline std::string EthAddr(const std::string& raw20) {
    return "0x" + common::Encode::HexEncode(raw20);
}

static inline std::string SethAddr(const std::string& eth_addr) {
    std::string s = eth_addr;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    return common::Encode::HexDecode(s);
}

static inline std::string ToHex64(uint64_t v) {
    std::ostringstream ss;
    ss << "0x" << std::hex << v;
    return ss.str();
}

static nlohmann::json RpcOk(const nlohmann::json& id, const nlohmann::json& result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

static nlohmann::json RpcErr(const nlohmann::json& id, int code, const std::string& msg) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", msg}}}};
}

// Decode an Ethereum RLP-encoded signed transaction (legacy or EIP-1559).
// Returns false if decoding fails.
// Fields populated: nonce, to (20 bytes), value, gas_limit, gas_price,
//                   data (contract input / bytecode), v, r (32 bytes), s (32 bytes).
// For EIP-1559 (Type 2), gas_price is set to maxFeePerGas, and max_priority_fee is set to maxPriorityFeePerGas.
static bool DecodeEthRawTx(
        const std::string& raw_bytes,
        uint64_t& nonce,
        std::string& to,
        uint64_t& value,
        uint64_t& gas_limit,
        uint64_t& gas_price,
        std::string& data,
        uint8_t& v_byte,
        std::string& r,
        std::string& s,
        uint64_t* max_priority_fee = nullptr) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(raw_bytes.data());
    size_t len = raw_bytes.size();
    if (len < 1) return false;

    // EIP-2718 typed transactions start with a byte in [0x00, 0x7f].
    // Type 2 (EIP-1559) = 0x02
    // Legacy transactions start with 0xc0..0xff (RLP list).
    if (p[0] == 0x02) {
        // EIP-1559 (Type 2) transaction
        // Format: 0x02 || RLP([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, v, r, s])
        SETH_INFO("DecodeEthRawTx: EIP-1559 (Type 2) transaction detected");
        p++; len--;  // Skip type byte
        
        // Decode outer RLP list
        if (len < 1 || p[0] < 0xc0) return false;
        
        size_t list_len = 0;
        size_t hdr = 0;
        if (p[0] >= 0xf8) {
            hdr = 1 + (p[0] - 0xf7);
            if (hdr > len) return false;
            for (size_t i = 1; i < hdr; ++i) list_len = (list_len << 8) | p[i];
        } else if (p[0] >= 0xc0) {
            hdr = 1;
            list_len = p[0] - 0xc0;
        } else {
            return false;
        }
        p += hdr; len -= hdr;
        if (len < list_len) return false;

        // Helper: decode one RLP item
        auto decode_item = [](const uint8_t*& pp, size_t& ll, std::string& out) -> bool {
            if (ll < 1) return false;
            if (pp[0] <= 0x7f) {
                out = std::string(1, (char)pp[0]);
                pp++; ll--;
            } else if (pp[0] <= 0xb7) {
                size_t item_len = pp[0] - 0x80;
                if (ll < 1 + item_len) return false;
                out = std::string((char*)pp + 1, item_len);
                pp += 1 + item_len; ll -= 1 + item_len;
            } else if (pp[0] <= 0xbf) {
                size_t hlen = pp[0] - 0xb7;
                if (ll < 1 + hlen) return false;
                size_t item_len = 0;
                for (size_t i = 1; i <= hlen; ++i) item_len = (item_len << 8) | pp[i];
                if (ll < 1 + hlen + item_len) return false;
                out = std::string((char*)pp + 1 + hlen, item_len);
                pp += 1 + hlen + item_len; ll -= 1 + hlen + item_len;
            } else if (pp[0] <= 0xf7) {
                // Short list (0xc0-0xf7) - for accessList
                size_t list_len = pp[0] - 0xc0;
                if (ll < 1 + list_len) return false;
                out = std::string((char*)pp + 1, list_len);  // Store list content
                pp += 1 + list_len; ll -= 1 + list_len;
            } else {
                // Long list (0xf8-0xff) - for accessList
                size_t hlen = pp[0] - 0xf7;
                if (ll < 1 + hlen) return false;
                size_t list_len = 0;
                for (size_t i = 1; i <= hlen; ++i) list_len = (list_len << 8) | pp[i];
                if (ll < 1 + hlen + list_len) return false;
                out = std::string((char*)pp + 1 + hlen, list_len);  // Store list content
                pp += 1 + hlen + list_len; ll -= 1 + hlen + list_len;
            }
            return true;
        };

        auto be_to_u64 = [](const std::string& s) -> uint64_t {
            uint64_t v = 0;
            for (unsigned char c : s) v = (v << 8) | c;
            return v;
        };

        // Decode EIP-1559 fields: chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList, v, r, s
        std::string s_chainid, s_nonce, s_maxpriority, s_maxfee, s_gaslimit, s_to, s_value, s_data, s_accesslist, s_v, s_r, s_s;
        if (!decode_item(p, len, s_chainid))      { SETH_WARN("EIP-1559 RLP decode failed at: chainId"); return false; }
        if (!decode_item(p, len, s_nonce))        { SETH_WARN("EIP-1559 RLP decode failed at: nonce"); return false; }
        if (!decode_item(p, len, s_maxpriority))  { SETH_WARN("EIP-1559 RLP decode failed at: maxPriorityFeePerGas"); return false; }
        if (!decode_item(p, len, s_maxfee))       { SETH_WARN("EIP-1559 RLP decode failed at: maxFeePerGas"); return false; }
        if (!decode_item(p, len, s_gaslimit))     { SETH_WARN("EIP-1559 RLP decode failed at: gasLimit"); return false; }
        if (!decode_item(p, len, s_to))           { SETH_WARN("EIP-1559 RLP decode failed at: to"); return false; }
        if (!decode_item(p, len, s_value))        { SETH_WARN("EIP-1559 RLP decode failed at: value"); return false; }
        if (!decode_item(p, len, s_data))         { SETH_WARN("EIP-1559 RLP decode failed at: data"); return false; }
        if (!decode_item(p, len, s_accesslist))   { SETH_WARN("EIP-1559 RLP decode failed at: accessList"); return false; }
        if (!decode_item(p, len, s_v))            { SETH_WARN("EIP-1559 RLP decode failed at: v"); return false; }
        if (!decode_item(p, len, s_r))            { SETH_WARN("EIP-1559 RLP decode failed at: r"); return false; }
        if (!decode_item(p, len, s_s))            { SETH_WARN("EIP-1559 RLP decode failed at: s"); return false; }

        nonce     = be_to_u64(s_nonce);
        gas_price = be_to_u64(s_maxfee);  // Use maxFeePerGas as gas_price
        gas_limit = be_to_u64(s_gaslimit);
        value     = be_to_u64(s_value);
        to        = s_to;
        data      = s_data;
        
        // Store maxPriorityFeePerGas if pointer provided
        if (max_priority_fee) {
            *max_priority_fee = be_to_u64(s_maxpriority);
        }

        // EIP-1559: v is 0 or 1 (parity only, no chain_id encoding)
        uint64_t v_val = be_to_u64(s_v);
        v_byte = static_cast<uint8_t>(v_val);
        
        // NOTE: For EIP-1559, we use the v value directly from the transaction
        // without flipping. The eth_account library and libsecp256k1 appear to
        // use the same convention for EIP-1559 transactions.
        // v_byte = 1 - v_byte;  // DO NOT FLIP for EIP-1559

        // r and s must be 32 bytes (left-pad if shorter)
        r = std::string(32 - std::min<size_t>(s_r.size(), 32), '\0') + s_r.substr(s_r.size() > 32 ? s_r.size() - 32 : 0);
        s = std::string(32 - std::min<size_t>(s_s.size(), 32), '\0') + s_s.substr(s_s.size() > 32 ? s_s.size() - 32 : 0);

        SETH_INFO("EIP-1559 decoded: nonce=%lu, maxFeePerGas=%lu, gasLimit=%lu, value=%lu, v=%u",
                  nonce, gas_price, gas_limit, value, v_byte);
        SETH_INFO("EIP-1559 signature: r=%s, s=%s",
                  common::Encode::HexEncode(r).c_str(),
                  common::Encode::HexEncode(s).c_str());
        return true;
    }
    
    // Legacy transaction (starts with 0xc0..0xff = RLP list)
    if (p[0] < 0xc0) {
        SETH_WARN("DecodeEthRawTx: unsupported transaction type=0x%02x", p[0]);
        return false;
    }
    
    SETH_INFO("DecodeEthRawTx: Legacy transaction detected");

    // Outer list
    size_t list_len = 0;
    size_t hdr = 0;
    if (p[0] >= 0xf8) {
        hdr = 1 + (p[0] - 0xf7);
        if (hdr > len) return false;
        for (size_t i = 1; i < hdr; ++i) list_len = (list_len << 8) | p[i];
    } else if (p[0] >= 0xc0) {
        hdr = 1;
        list_len = p[0] - 0xc0;
    } else {
        return false;
    }
    p += hdr; len -= hdr;
    if (len < list_len) return false;

    // Helper: decode one RLP item, advance p/len, return bytes.
    auto decode_item = [](const uint8_t*& pp, size_t& ll, std::string& out) -> bool {
        if (ll < 1) return false;
        if (pp[0] <= 0x7f) {
            out = std::string(1, (char)pp[0]);
            pp++; ll--;
        } else if (pp[0] <= 0xb7) {
            size_t item_len = pp[0] - 0x80;
            if (ll < 1 + item_len) return false;
            out = std::string((char*)pp + 1, item_len);
            pp += 1 + item_len; ll -= 1 + item_len;
        } else if (pp[0] <= 0xbf) {
            size_t hlen = pp[0] - 0xb7;
            if (ll < 1 + hlen) return false;
            size_t item_len = 0;
            for (size_t i = 1; i <= hlen; ++i) item_len = (item_len << 8) | pp[i];
            if (ll < 1 + hlen + item_len) return false;
            out = std::string((char*)pp + 1 + hlen, item_len);
            pp += 1 + hlen + item_len; ll -= 1 + hlen + item_len;
        } else {
            return false;
        }
        return true;
    };

    auto be_to_u64 = [](const std::string& s) -> uint64_t {
        uint64_t v = 0;
        for (unsigned char c : s) v = (v << 8) | c;
        return v;
    };

    std::string s_nonce, s_gasprice, s_gaslimit, s_to, s_value, s_data, s_v, s_r, s_s;
    if (!decode_item(p, len, s_nonce))    { SETH_WARN("RLP decode failed at: nonce"); return false; }
    if (!decode_item(p, len, s_gasprice)) { SETH_WARN("RLP decode failed at: gasprice"); return false; }
    if (!decode_item(p, len, s_gaslimit)) { SETH_WARN("RLP decode failed at: gaslimit"); return false; }
    if (!decode_item(p, len, s_to))       { SETH_WARN("RLP decode failed at: to"); return false; }
    if (!decode_item(p, len, s_value))    { SETH_WARN("RLP decode failed at: value"); return false; }
    if (!decode_item(p, len, s_data))     { SETH_WARN("RLP decode failed at: data"); return false; }
    if (!decode_item(p, len, s_v))        { SETH_WARN("RLP decode failed at: v"); return false; }
    if (!decode_item(p, len, s_r))        { SETH_WARN("RLP decode failed at: r"); return false; }
    if (!decode_item(p, len, s_s))        { SETH_WARN("RLP decode failed at: s"); return false; }

    nonce     = be_to_u64(s_nonce);
    gas_price = be_to_u64(s_gasprice);
    gas_limit = be_to_u64(s_gaslimit);
    value     = be_to_u64(s_value);
    to        = s_to;   // 20 bytes or empty (contract creation)
    data      = s_data;

    // EIP-155: v = chain_id * 2 + 35 + parity (where parity is 0 or 1)
    // Since chain_id * 2 is always even and 35 is odd:
    //   - if parity = 0, then v = even + odd + 0 = odd
    //   - if parity = 1, then v = even + odd + 1 = even
    // Therefore: parity = (v + 1) % 2
    // For legacy (pre-EIP-155): v = 27 + parity
    uint64_t v_val = be_to_u64(s_v);
    if (v_val >= 37) {
        // EIP-155 format (v >= 37 means chain_id >= 1)
        v_byte = static_cast<uint8_t>((v_val + 1) % 2);
    } else if (v_val >= 27) {
        // Legacy format: v = 27 or 28
        v_byte = static_cast<uint8_t>(v_val - 27);
    } else {
        // Raw parity value (0 or 1)
        v_byte = static_cast<uint8_t>(v_val);
    }

    // r and s must be 32 bytes (left-pad if shorter)
    r = std::string(32 - std::min<size_t>(s_r.size(), 32), '\0') + s_r.substr(s_r.size() > 32 ? s_r.size() - 32 : 0);
    s = std::string(32 - std::min<size_t>(s_s.size(), 32), '\0') + s_s.substr(s_s.size() > 32 ? s_s.size() - 32 : 0);
    return true;
}

static void EthJsonRpc(const UWSRequest& req, UWSResponse& http_res) {
    nlohmann::json req_json, id = nullptr;
    try {
        req_json = nlohmann::json::parse(req.body);
        id = req_json.value("id", nlohmann::json(nullptr));
    } catch (...) {
        http_res.set_content(RpcErr(nullptr, -32700, "Parse error").dump(), "application/json");
        return;
    }

    auto method = req_json.value("method", std::string(""));
    auto& params = req_json["params"];

    // ── net_version ──────────────────────────────────────────────────────────
    if (method == "net_version") {
        http_res.set_content(RpcOk(id, std::to_string(kSethChainId)).dump(), "application/json");
        return;
    }

    // ── eth_chainId ──────────────────────────────────────────────────────────
    if (method == "eth_chainId") {
        http_res.set_content(RpcOk(id, ToHex64(kSethChainId)).dump(), "application/json");
        return;
    }

    // ── seth_getChainInfo ────────────────────────────────────────────────────
    // Returns basic chain information: chainId, maxShardId, poolSize
    if (method == "seth_getChainInfo") {
        nlohmann::json chain_info;
        chain_info["chainId"] = kSethChainId;
        chain_info["chainIdHex"] = ToHex64(kSethChainId);
        // Use current valid end shard instead of static max
        uint32_t current_max_shard = common::GlobalInfo::Instance()->now_valid_end_shard();
        chain_info["maxShardId"] = current_max_shard;
        chain_info["shardBeginId"] = network::kConsensusShardBeginNetworkId;
        chain_info["shardEndId"] = current_max_shard + 1;  // end is exclusive
        chain_info["poolSize"] = common::kImmutablePoolSize;
        chain_info["rootShardId"] = network::kRootCongressNetworkId;
        http_res.set_content(RpcOk(id, chain_info).dump(), "application/json");
        return;
    }

    // ── eth_gasPrice ─────────────────────────────────────────────────────────
    if (method == "eth_gasPrice") {
        http_res.set_content(RpcOk(id, "0x1").dump(), "application/json");
        return;
    }

    // ── eth_estimateGas ──────────────────────────────────────────────────────
    if (method == "eth_estimateGas") {
        http_res.set_content(RpcOk(id, ToHex64(5000000)).dump(), "application/json");
        return;
    }

    // ── eth_blockNumber ──────────────────────────────────────────────────────
    if (method == "eth_blockNumber") {
        // Return the latest committed height across all pools of the local shard.
        uint64_t max_height = 0;
        uint32_t net_id = common::GlobalInfo::Instance()->network_id();
        for (uint32_t i = 0; i < common::kInvalidPoolIndex; ++i) {
            pools::protobuf::PoolLatestInfo pool_info;
            if (prefix_db->GetLatestPoolInfo(net_id, i, &pool_info)) {
                if (pool_info.height() > max_height) max_height = pool_info.height();
            }
        }
        http_res.set_content(RpcOk(id, ToHex64(max_height)).dump(), "application/json");
        return;
    }

    // ── eth_getBalance ───────────────────────────────────────────────────────
    if (method == "eth_getBalance") {
        if (!params.is_array() || params.empty()) {
            http_res.set_content(RpcErr(id, -32602, "missing params").dump(), "application/json");
            return;
        }
        std::string addr = SethAddr(params[0].get<std::string>());
        auto addr_info = http_handler->acc_mgr()->GetAccountInfo(addr);
        if (!addr_info) addr_info = prefix_db->GetAddressInfo(addr);
        uint64_t balance = addr_info ? addr_info->balance() : 0;
        http_res.set_content(RpcOk(id, ToHex64(balance)).dump(), "application/json");
        return;
    }

    // ── eth_getTransactionCount (nonce) ──────────────────────────────────────
    if (method == "eth_getTransactionCount") {
        if (!params.is_array() || params.empty()) {
            http_res.set_content(RpcErr(id, -32602, "missing params").dump(), "application/json");
            return;
        }
        std::string addr = SethAddr(params[0].get<std::string>());
        auto addr_info = http_handler->acc_mgr()->GetAccountInfo(addr);
        if (!addr_info) addr_info = prefix_db->GetAddressInfo(addr);
        uint64_t nonce = addr_info ? addr_info->nonce() : 0;
        http_res.set_content(RpcOk(id, ToHex64(nonce)).dump(), "application/json");
        return;
    }

    // ── eth_getCode ──────────────────────────────────────────────────────────
    if (method == "eth_getCode") {
        if (!params.is_array() || params.empty()) {
            http_res.set_content(RpcErr(id, -32602, "missing params").dump(), "application/json");
            return;
        }
        std::string addr = SethAddr(params[0].get<std::string>());
        auto addr_info = prefix_db->GetAddressInfo(addr);
        if (addr_info && !addr_info->bytes_code().empty()) {
            http_res.set_content(
                RpcOk(id, "0x" + common::Encode::HexEncode(addr_info->bytes_code())).dump(),
                "application/json");
        } else {
            http_res.set_content(RpcOk(id, "0x").dump(), "application/json");
        }
        return;
    }

    // ── eth_call ─────────────────────────────────────────────────────────────
    if (method == "eth_call") {
        if (!params.is_array() || params.empty() || !params[0].is_object()) {
            http_res.set_content(RpcErr(id, -32602, "missing params").dump(), "application/json");
            return;
        }
        auto& tx_obj = params[0];
        std::string to_eth  = tx_obj.value("to", std::string(""));
        std::string data_hex = tx_obj.value("data", std::string("0x"));
        std::string from_eth = tx_obj.value("from", std::string(""));

        std::string contract_addr = SethAddr(to_eth);
        std::string input = common::Encode::HexDecode(
            data_hex.size() >= 2 && data_hex[0] == '0' ? data_hex.substr(2) : data_hex);
        std::string from = from_eth.empty() ? std::string(20, '\0') : SethAddr(from_eth);

        auto contract_addr_info = prefix_db->GetAddressInfo(contract_addr);
        if (!contract_addr_info || contract_addr_info->bytes_code().empty()) {
            http_res.set_content(RpcOk(id, "0x").dump(), "application/json");
            return;
        }

        sethvm::SethhainHost seth_host;
        seth_host.tx_context_.block_gas_limit = 9999999999lu;
        uint64_t chain_id = hotstuff::kGlobalChainId;
        sethvm::Uint64ToEvmcBytes32(seth_host.tx_context_.chain_id, chain_id);
        seth_host.contract_mgr_ = contract_mgr;
        seth_host.my_address_ = contract_addr;
        seth_host.view_block_chain_ = http_handler->view_block_chain();
        seth_host.AddTmpAccountBalance(from, 9999999999lu);
        seth_host.AddTmpAccountBalance(contract_addr, contract_addr_info->balance());

        evmc_result evmc_res = {};
        evmc::Result result{evmc_res};
        int exec_res = sethvm::Execution::Instance()->execute(
            contract_addr_info->bytes_code(), input, from, contract_addr, from,
            0, 9999999999lu, 0, sethvm::kJustCall, seth_host, &result);

        if (exec_res != sethvm::kSethvmSuccess || result.status_code != EVMC_SUCCESS) {
            http_res.set_content(
                RpcErr(id, 3, "execution reverted: " + std::to_string(result.status_code)).dump(),
                "application/json");
            return;
        }
        std::string out((char*)result.output_data, result.output_size);
        http_res.set_content(
            RpcOk(id, "0x" + common::Encode::HexEncode(out)).dump(), "application/json");
        return;
    }

    // ── eth_sendRawTransaction ────────────────────────────────────────────────
    if (method == "eth_sendRawTransaction") {
        if (!params.is_array() || params.empty()) {
            http_res.set_content(RpcErr(id, -32602, "missing params").dump(), "application/json");
            return;
        }
        std::string raw_hex = params[0].get<std::string>();
        // Strip "0x" or "0X" prefix if present
        if (raw_hex.size() >= 2 && raw_hex[0] == '0' && (raw_hex[1] == 'x' || raw_hex[1] == 'X')) {
            raw_hex = raw_hex.substr(2);
        }
        std::string raw_bytes = common::Encode::HexDecode(raw_hex);
        if (raw_bytes.empty()) {
            SETH_WARN("eth_sendRawTransaction: HexDecode failed, raw_hex_len=%zu", raw_hex.size());
            http_res.set_content(RpcErr(id, -32602, "invalid hex encoding").dump(), "application/json");
            return;
        }

        uint64_t nonce = 0, value = 0, gas_limit = 0, gas_price = 0, max_priority_fee = 0;
        std::string to, data, r, s;
        uint8_t v_byte = 0;
        if (!DecodeEthRawTx(raw_bytes, nonce, to, value, gas_limit, gas_price, data, v_byte, r, s, &max_priority_fee)) {
            SETH_WARN("eth_sendRawTransaction: DecodeEthRawTx failed, raw_hex_len=%zu, first_byte=0x%02x",
                raw_bytes.size(), raw_bytes.empty() ? 0 : (uint8_t)raw_bytes[0]);
            http_res.set_content(RpcErr(id, -32602, "invalid raw transaction").dump(), "application/json");
            return;
        }

        if (to.size() != 20 && !to.empty()) {
            http_res.set_content(RpcErr(id, -32602, "invalid to address").dump(), "application/json");
            return;
        }

        // ── Recover sender public key ─────────────────────────────────────────
        // For legacy: EIP-155 signing preimage: RLP([nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0])
        // For EIP-1559: signing preimage: keccak256(0x02 || RLP([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList]))
        // We build a minimal RLP encoder inline.
        auto rlp_encode_uint = [](uint64_t v) -> std::string {
            if (v == 0) return std::string(1, '\x80');
            // Find minimal big-endian representation
            std::string be;
            while (v > 0) { be.push_back(static_cast<char>(v & 0xff)); v >>= 8; }
            std::reverse(be.begin(), be.end());
            // RLP: single byte < 0x80 encodes as itself; otherwise 0x80+len prefix
            if (be.size() == 1 && static_cast<uint8_t>(be[0]) < 0x80) {
                return be;
            }
            return std::string(1, static_cast<char>(0x80 + be.size())) + be;
        };
        auto rlp_encode_bytes = [](const std::string& b) -> std::string {
            if (b.empty()) return std::string(1, '\x80');
            if (b.size() == 1 && static_cast<uint8_t>(b[0]) < 0x80) return b;
            if (b.size() <= 55) {
                return std::string(1, static_cast<char>(0x80 + b.size())) + b;
            }
            // Long string
            std::string len_be;
            size_t sz = b.size();
            while (sz > 0) { len_be.push_back(static_cast<char>(sz & 0xff)); sz >>= 8; }
            std::reverse(len_be.begin(), len_be.end());
            return std::string(1, static_cast<char>(0xb7 + len_be.size())) + len_be + b;
        };
        auto rlp_list = [](const std::string& payload) -> std::string {
            if (payload.size() <= 55) {
                return std::string(1, static_cast<char>(0xc0 + payload.size())) + payload;
            }
            std::string len_be;
            size_t sz = payload.size();
            while (sz > 0) { len_be.push_back(static_cast<char>(sz & 0xff)); sz >>= 8; }
            std::reverse(len_be.begin(), len_be.end());
            return std::string(1, static_cast<char>(0xf7 + len_be.size())) + len_be + payload;
        };

        // Determine transaction type and build signing hash
        std::string signing_hash;
        std::string signing_rlp_for_debug;  // For logging only
        bool is_eip1559 = (raw_bytes[0] == 0x02);
        
        if (is_eip1559) {
            // EIP-1559 (Type 2) signing hash
            // signing_hash = keccak256(0x02 || RLP([chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList]))
            std::string payload;
            payload += rlp_encode_uint(kSethChainId);
            payload += rlp_encode_uint(nonce);
            payload += rlp_encode_uint(max_priority_fee);  // maxPriorityFeePerGas
            payload += rlp_encode_uint(gas_price);  // maxFeePerGas
            payload += rlp_encode_uint(gas_limit);
            payload += rlp_encode_bytes(to);
            payload += rlp_encode_uint(value);
            payload += rlp_encode_bytes(data);
            payload += std::string(1, '\xc0');  // accessList (empty list = 0xc0)
            std::string signing_rlp = rlp_list(payload);
            
            // Prepend type byte 0x02
            std::string type_and_rlp = std::string(1, '\x02') + signing_rlp;
            signing_hash = common::Hash::keccak256(type_and_rlp);
            signing_rlp_for_debug = type_and_rlp;  // Store for logging
            
            SETH_INFO("EIP-1559 signing: type_and_rlp_hex=%s, signing_hash=%s",
                      common::Encode::HexEncode(type_and_rlp).c_str(),
                      common::Encode::HexEncode(signing_hash).c_str());
        } else {
            // Legacy EIP-155 signing hash
            std::string payload;
            payload += rlp_encode_uint(nonce);
            payload += rlp_encode_uint(gas_price);
            payload += rlp_encode_uint(gas_limit);
            payload += rlp_encode_bytes(to);          // empty = contract creation
            payload += rlp_encode_uint(value);
            payload += rlp_encode_bytes(data);
            payload += rlp_encode_uint(kSethChainId); // EIP-155: chain_id
            payload += rlp_encode_uint(0);            // v = 0
            payload += rlp_encode_uint(0);            // r = 0
            std::string signing_rlp = rlp_list(payload);
            signing_hash = common::Hash::keccak256(signing_rlp);
            signing_rlp_for_debug = signing_rlp;  // Store for logging
        }
        SETH_WARN("eth_sendRawTransaction: signing_rlp_hex=%s, signing_hash=%s, "
            "nonce=%lu, gas_price=%lu, gas_limit=%lu, value=%lu, to_hex=%s, data_len=%zu, "
            "chain_id=%lu, v_byte=%u, is_eip1559=%d",
            common::Encode::HexEncode(signing_rlp_for_debug).c_str(),
            common::Encode::HexEncode(signing_hash).c_str(),
            nonce, gas_price, gas_limit, value,
            common::Encode::HexEncode(to).c_str(),
            data.size(), kSethChainId, v_byte, is_eip1559 ? 1 : 0);

        SETH_INFO("eth_sendRawTransaction: signature for recovery: r=%s, s=%s, v=%u",
                  common::Encode::HexEncode(r).c_str(),
                  common::Encode::HexEncode(s).c_str(),
                  v_byte);

        // Recover uncompressed public key (64 bytes, no prefix).
        // Build Seth-format signature: r (32 bytes) || s (32 bytes) || v (1 byte)
        std::string sign_for_recover;
        sign_for_recover.reserve(65);
        sign_for_recover.append(r);
        sign_for_recover.append(s);
        sign_for_recover.push_back(static_cast<char>(v_byte));
        
        SETH_WARN("eth_sendRawTransaction: trying recovery with v=%u, r=%s, s=%s, hash=%s",
                  v_byte,
                  common::Encode::HexEncode(r).c_str(),
                  common::Encode::HexEncode(s).c_str(),
                  common::Encode::HexEncode(signing_hash).c_str());
        
        std::string pubkey = security::Secp256k1::Instance()->Recover(
            sign_for_recover, signing_hash, false);
        
        SETH_WARN("eth_sendRawTransaction: recovery with v=%u resulted in pubkey=%s (len=%zu)",
                  v_byte,
                  pubkey.empty() ? "EMPTY" : common::Encode::HexEncode(pubkey).c_str(),
                  pubkey.size());
        
        // Also try with flipped v to see both results
        uint8_t flipped_v = 1 - v_byte;
        sign_for_recover[64] = static_cast<char>(flipped_v);
        std::string pubkey_flipped = security::Secp256k1::Instance()->Recover(
            sign_for_recover, signing_hash, false);
        
        SETH_WARN("eth_sendRawTransaction: recovery with v=%u resulted in pubkey=%s (len=%zu)",
                  flipped_v,
                  pubkey_flipped.empty() ? "EMPTY" : common::Encode::HexEncode(pubkey_flipped).c_str(),
                  pubkey_flipped.size());
        
        // Use the flipped v if original failed
        if (pubkey.empty() || pubkey.size() != 64) {
            SETH_WARN("eth_sendRawTransaction: using flipped v=%u", flipped_v);
            pubkey = pubkey_flipped;
        }
        
        if (pubkey.empty() || pubkey.size() != 64) {
            SETH_WARN("eth_sendRawTransaction: failed to recover pubkey with both v values");
            http_res.set_content(RpcErr(id, -32602, "signature recovery failed").dump(), "application/json");
            return;
        }
        
        SETH_INFO("eth_sendRawTransaction: recovery succeeded, pubkey=%s",
                  common::Encode::HexEncode(pubkey).c_str());

        // Prepend 0x04 uncompressed prefix so GetAddressWithPublicKey routes
        // to ECDSA (65 bytes) instead of GmSSL (64 bytes).
        std::string pubkey_with_prefix = std::string(1, '\x04') + pubkey;

        // Derive sender address from recovered pubkey
        std::string sender_addr = http_handler->security_ptr()->GetAddressWithPublicKey(pubkey_with_prefix);
        SETH_WARN("eth_sendRawTransaction: pubkey_len=%zu, pubkey_hex=%s, sender=%s",
            pubkey.size(), common::Encode::HexEncode(pubkey).c_str(),
            common::Encode::HexEncode(sender_addr).c_str());
        if (sender_addr.empty() || sender_addr.size() != 20) {
            http_res.set_content(RpcErr(id, -32602, "invalid sender address").dump(), "application/json");
            return;
        }
        // ── End pubkey recovery ───────────────────────────────────────────────

        // ── Auto-infer Seth step type from ETH transaction fields ──────────
        // ETH transactions don't carry a "step" field. We infer it:
        //
        //   to empty  + data non-empty  → kCreateContract (6)
        //     Deploy a new contract. 'data' is the creation bytecode.
        //
        //   to present + data non-empty + target has bytecode → kContractExcute (8)
        //     Call an existing contract. 'data' is the ABI-encoded call.
        //
        //   to present + data non-empty + target has NO bytecode → kNormalFrom (0)
        //     Transfer with memo/data to an EOA. Treat as plain transfer.
        //
        //   to present + data empty → kNormalFrom (0)
        //     Plain native transfer.
        //
        //   to empty + data empty → invalid (reject)
        //
        uint32_t step = 0;
        if (to.empty() && !data.empty()) {
            step = pools::protobuf::kCreateContract;  // 6
        } else if (!to.empty() && !data.empty()) {
            // Check if the target address is a deployed contract
            auto target_info = prefix_db->GetAddressInfo(to);
            if (target_info && !target_info->bytes_code().empty()) {
                step = pools::protobuf::kContractExcute;  // 8
            } else {
                // Target is an EOA or unknown address — treat data as memo,
                // send as plain transfer.
                step = pools::protobuf::kNormalFrom;  // 0
                SETH_INFO("eth_sendRawTransaction: to=%s has no bytecode, "
                    "treating as plain transfer with data (memo)",
                    common::Encode::HexEncode(to).c_str());
            }
        } else if (!to.empty() && data.empty()) {
            step = pools::protobuf::kNormalFrom;  // 0
        } else {
            // to empty + data empty = invalid
            http_res.set_content(RpcErr(id, -32602, "empty to and empty data").dump(), "application/json");
            return;
        }

        SETH_INFO("eth_sendRawTransaction: inferred step=%u (%s), to=%s, data_len=%zu",
            step,
            step == 6 ? "CreateContract" : step == 8 ? "ContractExcute" : "NormalFrom",
            to.empty() ? "(empty)" : common::Encode::HexEncode(to).c_str(),
            data.size());

        // For eth_sendRawTransaction (standard CREATE), the contract address follows
        // Ethereum's CREATE formula: keccak256(RLP([sender, nonce]))[-20:]
        // GetContractAddress now implements this formula internally — just pass the
        // nonce as a minimal big-endian byte string (same as Ethereum's RLP uint encoding).
        std::string nonce_str;
        if (step == pools::protobuf::kCreateContract) {
            // Encode nonce as minimal big-endian bytes (Ethereum RLP uint style)
            if (nonce == 0) {
                nonce_str = "";  // RLP of 0 is 0x80 (empty string), handled inside
            } else {
                uint64_t v = nonce;
                while (v > 0) { nonce_str.push_back(static_cast<char>(v & 0xff)); v >>= 8; }
                std::reverse(nonce_str.begin(), nonce_str.end());
            }

            to = security::GetContractAddress(sender_addr, nonce_str);
            SETH_INFO("eth_sendRawTransaction: contract deploy (CREATE), sender=%s, nonce=%lu, "
                "contract_addr=%s",
                common::Encode::HexEncode(sender_addr).c_str(), nonce,
                common::Encode::HexEncode(to).c_str());
        }

        auto msg_ptr = std::make_shared<transport::TransportMessage>();
        auto& msg = msg_ptr->header;
        int32_t net_id = common::GlobalInfo::Instance()->network_id();
        dht::DhtKeyManager dht_key(net_id);
        msg.set_src_sharding_id(net_id);
        msg.set_des_dht_key(dht_key.StrKey());
        msg.set_type(common::kPoolsMessage);
        msg.set_hop_count(0);

        auto new_tx = msg.mutable_tx_proto();
        new_tx->set_nonce(nonce);
        new_tx->set_pubkey(pubkey_with_prefix);   // 65-byte uncompressed pubkey (0x04 + X + Y)
        new_tx->set_step(static_cast<pools::protobuf::StepType>(step));
        // For contract creation, 'to' is the computed contract address (set above).
        // For other steps, 'to' was decoded from the raw transaction.
        new_tx->set_to(to);
        new_tx->set_amount(value);
        new_tx->set_gas_limit(gas_limit > 0 ? gas_limit : 5000000);
        new_tx->set_gas_price(gas_price > 0 ? gas_price : 1);
        if (step == pools::protobuf::kCreateContract) {
            new_tx->set_contract_code(data);
        }

        if (step == pools::protobuf::kContractExcute) new_tx->set_contract_input(data);

        // Seth signature: r || s || v
        std::string sign;
        sign.reserve(65);
        sign.append(r);
        sign.append(s);
        sign.push_back(static_cast<char>(v_byte));
        new_tx->set_sign(sign);

        // Store original RLP bytes so the pool manager can verify using
        // the ETH signing hash instead of the Seth-native hash.
        new_tx->set_eth_raw_tx(raw_bytes);

        auto tx_hash = pools::GetTxMessageHash(*new_tx);
        msg_ptr->msg_hash = tx_hash;
        msg.set_hash64(common::Random::RandomUint64());

        // Look up address_info for the recovered sender
        msg_ptr->address_info = http_handler->acc_mgr()->GetAccountInfo(sender_addr);
        if (!msg_ptr->address_info) {
            msg_ptr->address_info = prefix_db->GetAddressInfo(sender_addr);
        }
        
        if (!msg_ptr->address_info) {
            // For transactions from eth_sendRawTransaction (EIP-1559 or legacy),
            // auto-create address info to allow Ethereum-style transactions where
            // addresses don't need pre-registration.
            SETH_WARN("Auto-registering sender from raw transaction: %s (pubkey: %s)", 
                      common::Encode::HexEncode(sender_addr).c_str(),
                      common::Encode::HexEncode(pubkey).c_str());
            
            auto new_addr_info = std::make_shared<address::protobuf::AddressInfo>();
            new_addr_info->set_balance(0);  // Will be validated later
            new_addr_info->set_nonce(0);
            new_addr_info->set_type(address::protobuf::kNormal);  // Normal address
            new_addr_info->set_pubkey(pubkey_with_prefix);
            
            msg_ptr->address_info = new_addr_info;
            
            // Note: This is a temporary address_info. If the transaction succeeds,
            // the address will be properly registered in the system.
        }

        // Register in tx_msg_map BEFORE dispatching so any synchronous set_status()
        // call from the pool manager (nonce invalid, signature error, pool full, etc.)
        // is visible to eth_getTransactionReceipt immediately.
        msg_ptr->handle_status = transport::kMessageHandle;
        {
            std::lock_guard<std::mutex> lock(http_handler->tx_msg_map_mutex());
            http_handler->tx_msg_map().Put(tx_hash, msg_ptr);
        }

        // Set status_notify_cb so that any set_status() call from the pool manager
        // (FirewallCheckMessage, HandleTx, nonce validation, etc.) updates handle_status
        // on this msg_ptr, which eth_getTransactionReceipt reads while the tx is pending.
        {
            auto weak_msg = std::weak_ptr<transport::TransportMessage>(msg_ptr);
            msg_ptr->status_notify_cb = [weak_msg](
                    const std::string& /*hash*/,
                    transport::MessageHandleStatus s) {
                auto m = weak_msg.lock();
                if (m) {
                    m->handle_status.store(s);
                }
            };
        }

        http_handler->net_handler()->NewHttpServer(msg_ptr);

        std::string tx_hash_hex = "0x" + common::Encode::HexEncode(tx_hash);
        SETH_INFO("eth_sendRawTransaction: tx_hash=%s, step=%u, from=%s, to=%s, value=%lu, "
            "handle_status=%d",
            tx_hash_hex.c_str(), step,
            common::Encode::HexEncode(sender_addr).c_str(),
            common::Encode::HexEncode(to).c_str(),
            value,
            (int32_t)msg_ptr->handle_status.load());
        http_res.set_content(RpcOk(id, tx_hash_hex).dump(), "application/json");
        return;
    }

    // ── eth_getTransactionReceipt ─────────────────────────────────────────────
    if (method == "eth_getTransactionReceipt") {
        if (!params.is_array() || params.empty()) {
            http_res.set_content(RpcOk(id, nullptr).dump(), "application/json");
            return;
        }
        std::string tx_hash_hex = params[0].get<std::string>();
        if (tx_hash_hex.size() >= 2 && tx_hash_hex[0] == '0') tx_hash_hex = tx_hash_hex.substr(2);
        std::string tx_hash = common::Encode::HexDecode(tx_hash_hex);

        std::string res;
        auto addr = evmc::address{};
        auto id_str = std::string("tx");
        memcpy(addr.bytes, id_str.c_str(), id_str.size());
        if (prefix_db->GetTemporaryKv(std::string((char*)addr.bytes, sizeof(addr.bytes)) + tx_hash, &res)) {
            block::protobuf::KeyValueInfo kv_info;
            block::protobuf::TxHashStatus tx_status;
            if (kv_info.ParseFromString(res) && tx_status.ParseFromString(kv_info.value())) {
                // Build Ethereum-compatible receipt.
                // TxHashStatus only has: status, output, events.
                // Block-level fields (height, hash, from, to, gasUsed) are not stored
                // in TxHashStatus — fill with neutral defaults.

                // Look up the original tx to get step and contract address.
                transport::MessagePtr orig_msg = nullptr;
                {
                    std::lock_guard<std::mutex> lock(http_handler->tx_msg_map_mutex());
                    http_handler->tx_msg_map().Get(tx_hash, orig_msg);
                }

                nlohmann::json receipt;
                receipt["transactionHash"]   = "0x" + tx_hash_hex;
                receipt["status"]            = (tx_status.status() == 0) ? "0x1" : "0x0";
                receipt["blockNumber"]       = "0x0";
                receipt["blockHash"]         = "0x" + std::string(64, '0');
                receipt["transactionIndex"]  = "0x0";
                receipt["from"]              = "0x" + std::string(40, '0');
                receipt["gasUsed"]           = ToHex64(5000000);
                receipt["cumulativeGasUsed"] = ToHex64(5000000);
                receipt["logs"]              = nlohmann::json::array();
                receipt["logsBloom"]         = "0x" + std::string(512, '0');

                // For contract deployments, populate contractAddress and set to=null.
                // For regular calls/transfers, set to=contract/recipient and contractAddress=null.
                if (orig_msg &&
                        orig_msg->header.has_tx_proto() &&
                        orig_msg->header.tx_proto().step() == pools::protobuf::kCreateContract) {
                    const std::string& contract_addr = orig_msg->header.tx_proto().to();
                    receipt["to"]              = nullptr;
                    receipt["contractAddress"] = "0x" + common::Encode::HexEncode(contract_addr);
                } else if (orig_msg && orig_msg->header.has_tx_proto()) {
                    const std::string& to_addr = orig_msg->header.tx_proto().to();
                    receipt["to"]              = to_addr.empty() ? nullptr
                                                    : nlohmann::json("0x" + common::Encode::HexEncode(to_addr));
                    receipt["contractAddress"] = nullptr;
                } else {
                    receipt["to"]              = nullptr;
                    receipt["contractAddress"] = nullptr;
                }

                http_res.set_content(RpcOk(id, receipt).dump(), "application/json");
            } else {
                http_res.set_content(RpcOk(id, nullptr).dump(), "application/json");
            }
        } else {
            // Still pending
            http_res.set_content(RpcOk(id, nullptr).dump(), "application/json");
        }
        return;
    }

    // ── Unsupported method ────────────────────────────────────────────────────
    SETH_INFO("eth_rpc: unsupported method: %s", method.c_str());
    http_res.set_content(
        RpcErr(id, -32601, "Method not found: " + method).dump(), "application/json");
}

// ── End MetaMask / Ethereum JSON-RPC ─────────────────────────────────────────

void HttpHandler::Run() {
    SETH_INFO("HTTPS server starting on %s:%d", http_ip_.c_str(), http_port_);

    auto safeHandler = [](auto handler, const char* endpoint) {
        return [handler, endpoint](auto *res, auto *req) {
            auto body = std::make_shared<std::string>();
            auto alive = std::make_shared<bool>(true);
            auto query_str = std::make_shared<std::string>(std::string(req->getQuery()));
            res->onAborted([alive]() { *alive = false; });
            std::weak_ptr<bool> weak_alive = alive;
            res->onData([res, query_str, body, handler, endpoint, weak_alive](std::string_view data, bool last) {
                auto alive_lock = weak_alive.lock();
                if (!alive_lock || !*alive_lock) return;
                try {
                    body->append(data.data(), data.size());
                    if (last) {
                        UWSRequest uws_req(*query_str, *body);
                        UWSResponse uws_res;
                        handler(uws_req, uws_res);
                        if (*alive_lock) {
                            res->writeStatus("200 OK")
                               ->writeHeader("Content-Type", uws_res.content_type())
                               ->end(uws_res.content());
                            *alive_lock = false;
                        }
                    }
                } catch (const std::exception& e) {
                    SETH_ERROR("Exception in %s: %s", endpoint, e.what());
                    auto a = weak_alive.lock();
                    if (a && *a) { res->writeStatus("500 Internal Server Error")->end("Internal server error"); *a = false; }
                } catch (...) {
                    SETH_ERROR("Unknown exception in %s", endpoint);
                    auto a = weak_alive.lock();
                    if (a && *a) { res->writeStatus("500 Internal Server Error")->end("Internal server error"); *a = false; }
                }
            });
        };
    };

    uWS::SSLApp({
        .key_file_name  = key_file_.c_str(),
        .cert_file_name = cert_file_.c_str(),
        .passphrase = ""
    }).post("/transaction", safeHandler(HttpTransaction, "/transaction")
    ).post("/oqs_transaction", safeHandler(OqsHttpTransaction, "/oqs_transaction")
    ).post("/gm_transaction", safeHandler(GmHttpTransaction, "/gm_transaction")
    ).post("/get_seckey_and_encrypt_data", safeHandler(GetSecAndEncData, "/get_seckey_and_encrypt_data")
    ).post("/proxy_decrypt", safeHandler(ProxDecryption, "/proxy_decrypt")
    ).post("/query_contract", safeHandler(QueryContract, "/query_contract")
    ).post("/abi_query_contract", safeHandler(AbiQueryContract, "/abi_query_contract")
    ).post("/query_account", safeHandler(QueryAccount, "/query_account")
    ).post("/batch_query_accounts", safeHandler(BatchQueryAccounts, "/batch_query_accounts")
    ).post("/query_account_txs", safeHandler(QueryAccountTxs, "/query_account_txs")
    ).post("/query_leaders", safeHandler(QueryLeaders, "/query_leaders")
    ).post("/query_init", safeHandler(QueryInit, "/query_init")
    ).post("/get_proxy_reenc_info", safeHandler(GetProxyReencInfo, "/get_proxy_reenc_info")
    ).post("/ars_create_sec_keys", safeHandler(ArsCreateSecKeys, "/ars_create_sec_keys")
    ).post("/accounts_valid", safeHandler(AccountsValid, "/accounts_valid")
    ).post("/commit_gid_valid", safeHandler(GidsValid, "/commit_gid_valid")
    ).post("/prefund_valid", safeHandler(PrefundsValid, "/prefund_valid")
    ).post("/get_block_with_gid", safeHandler(GetBlockWithGid, "/get_block_with_gid")
    ).post("/get_blocks", safeHandler(GetBlocks, "/get_blocks")
    ).post("/get_latest_pool_info", safeHandler(GetLatestPoolHeights, "/get_latest_pool_info")
    ).post("/get_block_with_hash", safeHandler(GetBlockWithHash, "/get_block_with_hash")
    ).post("/transaction_receipt", safeHandler(TransactionReceipt, "/transaction_receipt")
    ).post("/update_private_key", safeHandler(UpdatePrivateKey, "/update_private_key")
    ).post("/eth", safeHandler(EthJsonRpc, "/eth")
    ).get("/eth", safeHandler(EthJsonRpc, "/eth")
    ).listen("0.0.0.0", http_port_, [this](auto *listen_socket) {
        if (listen_socket) {
            SETH_INFO("HTTPS server listening on 0.0.0.0:%d", http_port_);
            running_ = true;
        } else {
            SETH_ERROR("Failed to listen on 0.0.0.0:%d", http_port_);
        }
    }).run();
    
    SETH_INFO("HTTPS server stopped");
}

void HttpHandler::Init(
        std::shared_ptr<block::AccountManager> acc_mgr,
        transport::MultiThreadHandler* net_handler,
        std::shared_ptr<security::Security> security_ptr,
        std::shared_ptr<protos::PrefixDb> tmp_prefix_db,
        std::shared_ptr<contract::ContractManager> tmp_contract_mgr,
        const std::string& ip,
        uint16_t port) {
    acc_mgr_ = acc_mgr;
    net_handler_ = net_handler;
    security_ptr_ = security_ptr;
    secptr = security_ptr;
    prefix_db = tmp_prefix_db;
    contract_mgr = tmp_contract_mgr;
    view_block_chain_ = std::make_shared<hotstuff::ViewBlockChain>();
    view_block_chain_->Init(
        hotstuff::kInvalidChain,
        0,
        nullptr,
        nullptr,
        acc_mgr_,
        nullptr,
        nullptr,
        nullptr,
        nullptr);
    http_ip_ = ip;
    http_port_ = port;
    
    // Set certificate and key file paths
    // Try multiple locations for certificate files
    std::vector<std::string> cert_paths = {
        common::GlobalInfo::Instance()->server_cert_path(),
        "server-cert.pem",           // Current directory
        "../server-cert.pem",        // Parent directory
        "../../server-cert.pem"      // Grandparent directory
    };
    
    std::vector<std::string> key_paths = {
        common::GlobalInfo::Instance()->server_key_path(),
        "server-key.pem",
        "../server-key.pem",
        "../../server-key.pem"
    };
    
    // Find certificate file
    for (const auto& path : cert_paths) {
        std::ifstream f(path);
        if (f.good()) {
            cert_file_ = path;
            SETH_INFO("Found certificate file: %s", path.c_str());
            break;
        }
    }
    
    // Find key file
    for (const auto& path : key_paths) {
        std::ifstream f(path);
        if (f.good()) {
            key_file_ = path;
            SETH_INFO("Found key file: %s", path.c_str());
            break;
        }
    }
    
    if (cert_file_.empty() || key_file_.empty()) {
        SETH_ERROR("Certificate or key file not found! cert: %s, key: %s", 
                   cert_file_.c_str(), key_file_.c_str());
    }

    http_svr_thread_ = std::make_shared<std::thread>(std::bind(&HttpHandler::Run, this));
}

};  // namespace init

};  // namespace seth
