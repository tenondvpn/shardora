#include "init/http_handler.h"

#include <functional>

#include "common/encode.h"
#include "common/string_utils.h"
#include "dht/dht_key.h"
#include "network/route.h"
#include "pools/tx_utils.h"
#include "protos/transport.pb.h"

namespace zjchain {

namespace init {

static HttpHandler* http_handler = nullptr;
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

    ZJC_DEBUG("from: %s, to: %s",
        common::Encode::HexEncode(from).c_str(),
        common::Encode::HexEncode(to).c_str());
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    msg.set_hop_count(0);
    auto broadcast = msg.mutable_broadcast();
    broadcast->set_hop_limit(10);
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_gid(gid);
    new_tx->set_pubkey(from_pk);
    new_tx->set_step(pools::protobuf::kNormalFrom);
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
            return kSignatureInvalid;
        }

        new_tx->set_contract_prepayment(pepay_val);
    }

    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign = sign_r + sign_s + "0";// http_handler->security_ptr()->GetSign(sign_r, sign_s, sign_v);
    sign[64] = char(sign_v);
    if (http_handler->security_ptr()->Verify(
            tx_hash, from_pk, sign) != security::kSecuritySuccess) {
        ZJC_DEBUG("verify signature failed tx_hash: %s, "
            "sign_r: %s, sign_s: %s, sign_v: %d, pk: %s",
            common::Encode::HexEncode(tx_hash).c_str(),
            common::Encode::HexEncode(sign_r).c_str(),
            common::Encode::HexEncode(sign_s).c_str(),
            sign_v,
            common::Encode::HexEncode(from_pk).c_str());
        return kSignatureInvalid;
    }

    msg.set_sign(sign);
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

    ZJC_DEBUG("gid: %s, frompk: %s, to: %s, amount: %s, gas_limit: %s, gas_price: %s, sign_r: %s, sign_s: %s, sign_v: %s, shard_id: %s",
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

    http_handler->net_handler()->NewHttpServer(msg_ptr);
    std::string res = std::string("ok");
    evbuffer_add(req->buffer_out, res.c_str(), res.size());
    evhtp_send_reply(req, EVHTP_RES_OK);
    //ZJC_INFO("http transaction success.");
}

HttpHandler::HttpHandler() {
    http_handler = this;
}

HttpHandler::~HttpHandler() {}

void HttpHandler::Init(
        transport::MultiThreadHandler* net_handler,
        std::shared_ptr<security::Security>& security_ptr,
        http::HttpServer& http_server) {
    net_handler_ = net_handler;
    security_ptr_ = security_ptr;
    http_server.AddCallback("/transaction", HttpTransaction);
}

};  // namespace init

};  // namespace zjchain
