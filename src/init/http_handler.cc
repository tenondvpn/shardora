#include "init/http_handler.h"

#include <functional>

#include <cpppbc/GT.h>
#include <cpppbc/Pairing.h>
#include <json/json.hpp>

#include "common/encode.h"
#include "common/string_utils.h"
#include "contract/contract_reencryption.h"
#include "dht/dht_key.h"
#include "network/route.h"
#include "pools/tx_utils.h"
#include "protos/transport.pb.h"
#include "transport/tcp_transport.h"
#include "zjcvm/execution.h"
#include "zjcvm/zjc_host.h"
#include "zjcvm/zjcvm_utils.h"

#include <google/protobuf/util/json_util.h>

namespace shardora {

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

static int CreateTransactionWithAttr(
        const std::string& gid,
        const std::string& from_pk,
        const std::string& to,
        const std::string& sign_r,
        const std::string& sign_s,
        int32_t sign_v,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id,
        evhtp_kvs_t* evhtp_kvs,
        transport::protobuf::Header& msg) {
    auto from = http_handler->security_ptr()->GetAddress(from_pk);
    if (from.empty()) {
        return kAccountNotExists;
    }

    if (from == to) {
        return kFromEqualToInvalid;
    }

    if (from.size() != 20 || to.size() != 20) {
        return kAccountNotExists;
    }

    ZJC_DEBUG("from: %s, to: %s, gid: %s",
        common::Encode::HexEncode(from).c_str(),
        common::Encode::HexEncode(to).c_str(),
        common::Encode::HexEncode(gid).c_str());
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    msg.set_hop_count(0);
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_gid(gid);
    new_tx->set_pubkey(from_pk);
    const char* step = evhtp_kv_find(evhtp_kvs, "type");
    if (step == nullptr) {
        return kHttpError;
    }

    uint32_t step_val = 0;
    if (!common::StringUtil::ToUint32(std::string(step), &step_val)) {
        return kHttpError;
    }

    new_tx->set_step(static_cast<pools::protobuf::StepType>(step_val));
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    
    const char* key = evhtp_kv_find(evhtp_kvs, "key");
    const char* val = evhtp_kv_find(evhtp_kvs, "val");
    if (key != nullptr) {
        new_tx->set_key(key);
        if (val != nullptr) {
            new_tx->set_value(val);
        }
    }

    const char* contract_bytes = evhtp_kv_find(evhtp_kvs, "bytes_code");
    if (contract_bytes != nullptr) {
        new_tx->set_contract_code(common::Encode::HexDecode(contract_bytes));
    }

    const char* input = evhtp_kv_find(evhtp_kvs, "input");
    if (input != nullptr) {
        new_tx->set_contract_input(common::Encode::HexDecode(input));
    }

    const char* pepay = evhtp_kv_find(evhtp_kvs, "pepay");
    if (pepay != nullptr) {
        uint64_t pepay_val = 0;
        if (!common::StringUtil::ToUint64(std::string(pepay), &pepay_val)) {
            ZJC_WARN("get prepay failed %s", pepay);
            return kSignatureInvalid;
        }

        new_tx->set_contract_prepayment(pepay_val);
    }

    if (key != nullptr && val != nullptr) {
        ZJC_WARN("create transaction key: %s, value: %s", key, val);
    }
    
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign = sign_r + sign_s + "0";// http_handler->security_ptr()->GetSign(sign_r, sign_s, sign_v);
    sign[64] = char(sign_v);
    if (http_handler->security_ptr()->Verify(
            tx_hash, from_pk, sign) != security::kSecuritySuccess) {
        ZJC_ERROR("verify signature failed tx_hash: %s, "
            "sign_r: %s, sign_s: %s, sign_v: %d, pk: %s, hash64: %lu",
            common::Encode::HexEncode(tx_hash).c_str(),
            common::Encode::HexEncode(sign_r).c_str(),
            common::Encode::HexEncode(sign_s).c_str(),
            sign_v,
            common::Encode::HexEncode(from_pk).c_str(),
            msg.hash64());
        return kSignatureInvalid;
    }

    new_tx->set_sign(sign);
    return kHttpSuccess;
}
 
static void HttpTransaction(evhtp_request_t* req, void* data) {
    ZJC_DEBUG("http transaction coming.");
    auto header1 = evhtp_header_new("Access-Control-Allow-Origin", "*", 0, 0);
    auto header2 = evhtp_header_new("Access-Control-Allow-Methods", "POST", 0, 0);
    auto header3 = evhtp_header_new(
        "Access-Control-Allow-Headers",
        "x-requested-with,content-type", 0, 0);
    evhtp_headers_add_header(req->headers_out, header1);
    evhtp_headers_add_header(req->headers_out, header2);
    evhtp_headers_add_header(req->headers_out, header3);
    const char* gid = evhtp_kv_find(req->uri->query, "gid");
    const char* frompk = evhtp_kv_find(req->uri->query, "pubkey");
    const char* to = evhtp_kv_find(req->uri->query, "to");
    const char* amount = evhtp_kv_find(req->uri->query, "amount");
    const char* gas_limit = evhtp_kv_find(req->uri->query, "gas_limit");
    const char* gas_price = evhtp_kv_find(req->uri->query, "gas_price");
    const char* sign_r = evhtp_kv_find(req->uri->query, "sign_r");
    const char* sign_s = evhtp_kv_find(req->uri->query, "sign_s");
    const char* sign_v = evhtp_kv_find(req->uri->query, "sign_v");
    const char* shard_id = evhtp_kv_find(req->uri->query, "shard_id");
    if (gid == nullptr || frompk == nullptr || to == nullptr ||
            amount == nullptr || gas_limit == nullptr ||
            gas_price == nullptr || sign_r == nullptr ||
            sign_s == nullptr || shard_id == nullptr) {
        std::string res = common::StringUtil::Format(
            "param invalid gid: %d, frompk: %d, to: %d,"
            "amount: %d, gas_limit: %d, gas_price: %d, "
            "sign_r: %d, sign_s: %d, shard_id: %d \n",
            (gid != nullptr), (frompk != nullptr), (to != nullptr),
            (amount != nullptr), (gas_limit != nullptr),
            (gas_price != nullptr), (sign_r != nullptr),
            (sign_s != nullptr), (shard_id != nullptr));
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        ZJC_INFO("http transaction param error: %s.", res.c_str());
        return;
    }

    ZJC_DEBUG("gid: %s, frompk: %s, to: %s, amount: %s, gas_limit: %s, "
        "gas_price: %s, sign_r: %s, sign_s: %s, sign_v: %s, shard_id: %s",
        gid, frompk, to, amount, gas_limit, gas_price, sign_r, sign_s, sign_v, shard_id);
    uint64_t amount_val = 0;
    if (!common::StringUtil::ToUint64(std::string(amount), &amount_val)) {
        std::string res = std::string("amount not integer: ") + amount;
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    uint64_t gas_limit_val = 0;
    if (!common::StringUtil::ToUint64(std::string(gas_limit), &gas_limit_val)) {
        std::string res = std::string("gas_limit not integer: ") + gas_limit;
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    uint64_t gas_price_val = 0;
    if (!common::StringUtil::ToUint64(std::string(gas_price), &gas_price_val)) {
        std::string res = std::string("gas_price not integer: ") + gas_price;
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    int32_t shard_id_val = 0;
    if (!common::StringUtil::ToInt32(std::string(shard_id), &shard_id_val)) {
        std::string res = std::string("shard_id not integer: ") + shard_id;
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
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
    if (!common::StringUtil::ToInt32(std::string(sign_v), &tmp_sign_v)) {
        std::string res = std::string("sign_v not integer: ") + sign_v;
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    int status = CreateTransactionWithAttr(
        common::Encode::HexDecode(gid),
        common::Encode::HexDecode(frompk),
        common::Encode::HexDecode(to),
        common::Encode::HexDecode(tmp_sign_r),
        common::Encode::HexDecode(tmp_sign_s),
        tmp_sign_v,
        amount_val,
        gas_limit_val,
        gas_price_val,
        shard_id_val,
        req->uri->query,
        msg);
    if (status != http::kHttpSuccess) {
        std::string res = std::string("transaction invalid: ") + GetStatus(status);
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
    ZJC_DEBUG("http handler success get http server thread index: %d, address: %s", 
        thread_index, 
        common::Encode::HexEncode(
            http_handler->security_ptr()->GetAddress(common::Encode::HexDecode(frompk))).c_str());
    msg_ptr->msg_hash = pools::GetTxMessageHash(msg.tx_proto());
    msg_ptr->address_info = http_handler->acc_mgr()->GetAccountInfo(
        http_handler->security_ptr()->GetAddress(common::Encode::HexDecode(frompk)));
    if (msg_ptr->address_info == nullptr) {
        std::string res = std::string("address invalid: ") + common::Encode::HexEncode(
            http_handler->security_ptr()->GetAddress(common::Encode::HexDecode(frompk)));
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }
    
    transport::TcpTransport::Instance()->SetMessageHash(msg_ptr->header);
    ZJC_WARN("http handler success get http server thread index: %d, address: %s, hash64: %lu", 
        thread_index, 
        common::Encode::HexEncode(
            http_handler->security_ptr()->GetAddress(common::Encode::HexDecode(frompk))).c_str(),
        msg_ptr->header.hash64());
    http_handler->net_handler()->NewHttpServer(msg_ptr);
    std::string res = std::string("ok");
    evbuffer_add(req->buffer_out, res.c_str(), res.size());
    evhtp_send_reply(req, EVHTP_RES_OK);
    ZJC_WARN("http transaction success %s, %s, gid: %s", common::Encode::HexEncode(
            http_handler->security_ptr()->GetAddress(common::Encode::HexDecode(frompk))).c_str(), to, gid);
}

static void QueryContract(evhtp_request_t* req, void* data) {
    ZJC_DEBUG("query contract coming.");
    auto header1 = evhtp_header_new("Access-Control-Allow-Origin", "*", 0, 0);
    auto header2 = evhtp_header_new("Access-Control-Allow-Methods", "POST", 0, 0);
    auto header3 = evhtp_header_new(
        "Access-Control-Allow-Headers",
        "x-requested-with,content-type", 0, 0);
    evhtp_headers_add_header(req->headers_out, header1);
    evhtp_headers_add_header(req->headers_out, header2);
    evhtp_headers_add_header(req->headers_out, header3);
    const char* tmp_contract_addr = evhtp_kv_find(req->uri->query, "address");
    const char* tmp_input = evhtp_kv_find(req->uri->query, "input");
    const char* tmp_from = evhtp_kv_find(req->uri->query, "from");
    if (tmp_contract_addr == nullptr || tmp_from == nullptr || tmp_input == nullptr) {
        std::string res = common::StringUtil::Format(
            "param invalid contract_addr valid: %d, input valid: %d",
            (tmp_contract_addr != nullptr), (tmp_input != nullptr), (tmp_from != nullptr));
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        ZJC_INFO("query contract param error: %s.", res.c_str());
        return;
    }

    std::string from = common::Encode::HexDecode(tmp_from);
    std::string contract_addr = common::Encode::HexDecode(tmp_contract_addr);
    std::string input = common::Encode::HexDecode(tmp_input);
    uint64_t height = 0;
    uint64_t prepayment = 0;
    auto res = prefix_db->GetContractUserPrepayment(contract_addr, from, &height, &prepayment);
    if (!res) {
        std::string res = "get from prepayment failed: " + std::string(tmp_contract_addr) + ", " + std::string(tmp_from);
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        ZJC_INFO("query contract param error: %s.", res.c_str());
        return;
    }

    auto contract_addr_info = prefix_db->GetAddressInfo(contract_addr);
    if (contract_addr_info == nullptr) {
        std::string res = "get contract addr failed: " + std::string(tmp_contract_addr);
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        ZJC_INFO("query contract param error: %s.", res.c_str());
        return;
    }

    zjcvm::ZjchainHost zjc_host;
    zjc_host.tx_context_.tx_origin = evmc::address{};
    zjc_host.tx_context_.block_coinbase = evmc::address{};
    zjc_host.tx_context_.block_number = 0;
    zjc_host.tx_context_.block_timestamp = 0;
    uint64_t chanin_id = 0;
    zjcvm::Uint64ToEvmcBytes32(
        zjc_host.tx_context_.chain_id,
        chanin_id);
    zjc_host.contract_mgr_ = contract_mgr;
    zjc_host.acc_mgr_ = nullptr;
    zjc_host.my_address_ = contract_addr;
    zjc_host.tx_context_.block_gas_limit = prepayment;
    // user caller prepayment 's gas
    uint64_t from_balance = prepayment;
    uint64_t to_balance = contract_addr_info->balance();
    zjc_host.AddTmpAccountBalance(
        from,
        from_balance);
    zjc_host.AddTmpAccountBalance(
        contract_addr,
        to_balance);
    evmc_result evmc_res = {};
    evmc::Result result{ evmc_res };
    int exec_res = zjcvm::Execution::Instance()->execute(
        contract_addr_info->bytes_code(),
        input,
        from,
        contract_addr,
        from,
        0,
        prepayment,
        0,
        zjcvm::kJustCall,
        zjc_host,
        &result);
    if (exec_res != zjcvm::kZjcvmSuccess || result.status_code != EVMC_SUCCESS) {
        std::string res = "query contract failed: " + std::to_string(result.status_code);
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        ZJC_INFO("query contract error: %s.", res.c_str());
        return;
    }
	
    std::string qdata((char*)result.output_data, result.output_size);
    ZJC_DEBUG("LLLLLhttp: %s, size %d", common::Encode::HexEncode(qdata).c_str(), result.output_size);
    if (result.output_size < 64) {
        auto res = common::Encode::HexEncode(qdata); 
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_OK);
        return;
    }
    evmc_bytes32 len_bytes;
    memcpy(len_bytes.bytes, qdata.c_str() + 32, 32);
    uint64_t len = zjcvm::EvmcBytes32ToUint64(len_bytes);
    std::string http_res(qdata.c_str() + 64, len);
    evbuffer_add(req->buffer_out, http_res.c_str(), http_res.size());
    evhtp_send_reply(req, EVHTP_RES_OK);
    ZJC_INFO("query contract success data: %s", http_res.c_str());
}

static void QueryAccount(evhtp_request_t* req, void* data) {
    ZJC_DEBUG("query account.");
    auto header1 = evhtp_header_new("Access-Control-Allow-Origin", "*", 0, 0);
    auto header2 = evhtp_header_new("Access-Control-Allow-Methods", "POST", 0, 0);
    auto header3 = evhtp_header_new(
        "Access-Control-Allow-Headers",
        "x-requested-with,content-type", 0, 0);
    evhtp_headers_add_header(req->headers_out, header1);
    evhtp_headers_add_header(req->headers_out, header2);
    evhtp_headers_add_header(req->headers_out, header3);

    const char* tmp_addr = evhtp_kv_find(req->uri->query, "address");
    if (tmp_addr == nullptr) {
        std::string res = common::StringUtil::Format("param address is null");
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    std::string addr = common::Encode::HexDecode(tmp_addr);
    auto addr_info = prefix_db->GetAddressInfo(addr);

    if (addr_info == nullptr) {
        std::string res = "get address failed from db: " + addr;
        addr_info =  http_handler->acc_mgr()->GetAccountInfo(addr);
    }

    if (addr_info == nullptr) {
        std::string res = "get address failed from cache: " + addr;
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    std::string json_str;
    auto st = google::protobuf::util::MessageToJsonString(*addr_info, &json_str);
    if (!st.ok()) {
        std::string res = "json parse failed: " + addr;
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    evbuffer_add(req->buffer_out, json_str.c_str(), json_str.size());
    evhtp_send_reply(req, EVHTP_RES_OK);
    return;
}


static void GetProxyReencInfo(evhtp_request_t* req, void* data) {
    auto header1 = evhtp_header_new("Access-Control-Allow-Origin", "*", 0, 0);
    auto header2 = evhtp_header_new("Access-Control-Allow-Methods", "POST", 0, 0);
    auto header3 = evhtp_header_new(
        "Access-Control-Allow-Headers",
        "x-requested-with,content-type", 0, 0);
    evhtp_headers_add_header(req->headers_out, header1);
    evhtp_headers_add_header(req->headers_out, header2);
    evhtp_headers_add_header(req->headers_out, header3);

    const char* id = evhtp_kv_find(req->uri->query, "id");
    if (id == nullptr) {
        std::string res = common::StringUtil::Format("param address is null");
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    const char* contract = evhtp_kv_find(req->uri->query, "contract");
    if (contract == nullptr) {
        std::string res = common::StringUtil::Format("param contract is null");
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    const char* count_str = evhtp_kv_find(req->uri->query, "count");
    if (count_str == nullptr) {
        std::string res = common::StringUtil::Format("param count is null");
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    std::string proxy_id = id;
    std::string contract_str = common::Encode::HexDecode(contract);
    uint32_t count = 0;
    if (!common::StringUtil::ToUint32(count_str, &count) || count > 10) {
        std::string res = common::StringUtil::Format("param count is null");
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }
    
    nlohmann::json res_json;
    auto bls_pk_json = res_json["value"];
    res_json["status"] = 0;
    res_json["msg"] = "success";
    ZJC_WARN("GetProxyReencInfo 4.");
    for (uint32_t i = 0; i < count; ++i) {
        auto private_key = proxy_id + "_" + std::string("init_prikey_") + std::to_string(i);
        std::string prikey;
        zjcvm::Execution::Instance()->GetStorage(contract_str, private_key, &prikey);
        auto public_key = proxy_id + "_" + std::string("init_pubkey_") + std::to_string(i);
        std::string pubkey;
        zjcvm::Execution::Instance()->GetStorage(contract_str, public_key, &pubkey);
        ZJC_WARN("contract_reencryption get member private and public key: %s, %s sk: %s, pk: %s",
            common::Encode::HexEncode(private_key).c_str(), 
            common::Encode::HexEncode(public_key).c_str(), 
            common::Encode::HexEncode(prikey).c_str(),
            common::Encode::HexEncode(pubkey).c_str());

        
        res_json["value"][i]["node_index"] = i;
        res_json["value"][i]["private_key"] = common::Encode::HexEncode(prikey);
        res_json["value"][i]["public_key"] = common::Encode::HexEncode(pubkey);
    }
   
    auto json_str = res_json.dump();
    evbuffer_add(req->buffer_out, json_str.c_str(), json_str.size());
    evhtp_send_reply(req, EVHTP_RES_OK);
    return;
}


static void GetSecAndEncData(evhtp_request_t* req, void* req_data) {
    ZJC_DEBUG("http transaction coming.");
    contract::ContractReEncryption prox_renc;
    zjcvm::ZjchainHost zjc_host;
    contract::CallParameters param;
    param.zjc_host = &zjc_host;
    auto header1 = evhtp_header_new("Access-Control-Allow-Origin", "*", 0, 0);
    auto header2 = evhtp_header_new("Access-Control-Allow-Methods", "POST", 0, 0);
    auto header3 = evhtp_header_new(
        "Access-Control-Allow-Headers",
        "x-requested-with,content-type", 0, 0);
    evhtp_headers_add_header(req->headers_out, header1);
    evhtp_headers_add_header(req->headers_out, header2);
    evhtp_headers_add_header(req->headers_out, header3);
    const char* data = evhtp_kv_find(req->uri->query, "data");
    if (data == nullptr) {
        std::string res = common::StringUtil::Format("param data is null");
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    std::string test_data(data);
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
    std::string hash256 = common::Hash::Hash256(m.toString());
   
    std::string sec_data;
    secptr->Encrypt(data, hash256, &sec_data);
    ZJC_WARN("get m data src data: %s, hex data: %s, m: %s, hash sec: %s, sec data: %s", 
        data.c_str(), 
        common::Encode::HexEncode(data).c_str(),
        common::Encode::HexEncode(m.toString()).c_str(), 
        common::Encode::HexEncode(hash256).c_str(),
        common::Encode::HexEncode(sec_data).c_str());
    nlohmann::json res_json;
    res_json["status"] = 0;
    res_json["seckey"] = common::Encode::HexEncode(m.toString());
    res_json["hash_seckey"] = common::Encode::HexEncode(hash256);
    res_json["secdata"] = common::Encode::HexEncode(sec_data);
    auto json_str = res_json.dump();
    evbuffer_add(req->buffer_out, json_str.c_str(), json_str.size());
    evhtp_send_reply(req, EVHTP_RES_OK);
}

static void ProxDecryption(evhtp_request_t* req, void* req_data) {
    ZJC_WARN("ProxDecryption coming 0.");
    contract::ContractReEncryption prox_renc;
    zjcvm::ZjchainHost zjc_host;
    contract::CallParameters param;
    param.from = common::Encode::HexDecode("48e1eab96c9e759daa3aff82b40e77cd615a41d0");
    param.zjc_host = &zjc_host;
    auto header1 = evhtp_header_new("Access-Control-Allow-Origin", "*", 0, 0);
    auto header2 = evhtp_header_new("Access-Control-Allow-Methods", "POST", 0, 0);
    auto header3 = evhtp_header_new(
        "Access-Control-Allow-Headers",
        "x-requested-with,content-type", 0, 0);
    evhtp_headers_add_header(req->headers_out, header1);
    evhtp_headers_add_header(req->headers_out, header2);
    evhtp_headers_add_header(req->headers_out, header3);
    const char* id = evhtp_kv_find(req->uri->query, "id");
    if (id == nullptr) {
        std::string res = common::StringUtil::Format("param data is null");
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    ZJC_WARN("ProxDecryption coming 1.");
    const char* enc_data = evhtp_kv_find(req->uri->query, "enc_data");
    if (enc_data == nullptr) {
        std::string res = common::StringUtil::Format("param enc_data is null");
        evbuffer_add(req->buffer_out, res.c_str(), res.size());
        evhtp_send_reply(req, EVHTP_RES_BADREQ);
        return;
    }

    ZJC_WARN("ProxDecryption coming 2.");
    std::string res_data;
    prox_renc.Decryption(param, "", std::string(id) + ";", &res_data);
    std::string hash256 = common::Hash::Hash256(res_data);
    std::string dec_data;
    secptr->Decrypt(common::Encode::HexDecode(enc_data), hash256, &dec_data);
    ZJC_WARN("get m data src data: %s, hex data: %s, m: %s, hash sec: %s, sec data: %s", 
        dec_data.c_str(), 
        common::Encode::HexEncode(dec_data).c_str(),
        common::Encode::HexEncode(res_data).c_str(), 
        common::Encode::HexEncode(hash256).c_str(),
        enc_data.c_str());
    nlohmann::json res_json;
    res_json["status"] = 0;
    res_json["seckey"] = common::Encode::HexEncode(res_data);
    res_json["hash_seckey"] = common::Encode::HexEncode(hash256);
    res_json["decdata"] = common::Encode::HexEncode(dec_data);
    auto json_str = res_json.dump();
    ZJC_WARN("ProxDecryption coming 3.");
    evbuffer_add(req->buffer_out, json_str.c_str(), json_str.size());
    evhtp_send_reply(req, EVHTP_RES_OK);
    ZJC_WARN("ProxDecryption coming 4.");
}

static void QueryInit(evhtp_request_t* req, void* data) {
    auto thread_index = common::GlobalInfo::Instance()->get_thread_index();
    std::string res = "ok";
    evbuffer_add(req->buffer_out, res.c_str(), res.size());
    evhtp_send_reply(req, EVHTP_RES_OK);
}

HttpHandler::HttpHandler() {
    http_handler = this;
}

HttpHandler::~HttpHandler() {}

void HttpHandler::Init(
        std::shared_ptr<block::AccountManager>& acc_mgr,
        transport::MultiThreadHandler* net_handler,
        std::shared_ptr<security::Security>& security_ptr,
        std::shared_ptr<protos::PrefixDb>& tmp_prefix_db,
        std::shared_ptr<contract::ContractManager>& tmp_contract_mgr,
        http::HttpServer& http_server) {
    acc_mgr_ = acc_mgr;
    net_handler_ = net_handler;
    security_ptr_ = security_ptr;
    secptr = security_ptr;
    prefix_db = tmp_prefix_db;
    contract_mgr = tmp_contract_mgr;
    http_server.AddCallback("/transaction", HttpTransaction);
    http_server.AddCallback("/get_seckey_and_encrypt_data", GetSecAndEncData);
    http_server.AddCallback("/proxy_decrypt", ProxDecryption);
    http_server.AddCallback("/query_contract", QueryContract);
    http_server.AddCallback("/query_account", QueryAccount);
    http_server.AddCallback("/query_init", QueryInit);
    http_server.AddCallback("/get_proxy_reenc_info", GetProxyReencInfo);
}

};  // namespace init

};  // namespace shardora
