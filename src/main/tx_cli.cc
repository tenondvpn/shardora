#include <iostream>
#include <queue>
#include <vector>

#include "common/random.h"
#include "common/split.h"
#include "db/db.h"
#include "dht/dht_key.h"
#include "pools/tx_utils.h"
#include "security/ecdsa/ecdsa.h"
#include "transport/multi_thread.h"
#include "transport/tcp_transport.h"

using namespace zjchain;
static bool global_stop = false;
static void SignalCallback(int sig_int) {
    global_stop = true;
}

void SignalRegister() {
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
    signal(SIGABRT, SIG_IGN);
    signal(SIGINT, SignalCallback);
    signal(SIGTERM, SignalCallback);

    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &signal_mask, NULL);
#endif
}

static void WriteDefaultLogConf() {
    FILE* file = NULL;
    file = fopen("./log4cpp.properties", "w");
    if (file == NULL) {
        return;
    }
    std::string log_str = ("# log4cpp.properties\n"
        "log4cpp.rootCategory = DEBUG\n"
        "log4cpp.category.sub1 = DEBUG, programLog\n"
        "log4cpp.appender.rootAppender = ConsoleAppender\n"
        "log4cpp.appender.rootAppender.layout = PatternLayout\n"
        "log4cpp.appender.rootAppender.layout.ConversionPattern = %d [%p] %m%n\n"
        "log4cpp.appender.programLog = RollingFileAppender\n"
        "log4cpp.appender.programLog.fileName = ./txcli.log\n") +
        std::string("log4cpp.appender.programLog.maxFileSize = 1073741824\n"
            "log4cpp.appender.programLog.maxBackupIndex = 1\n"
            "log4cpp.appender.programLog.layout = PatternLayout\n"
            "log4cpp.appender.programLog.layout.ConversionPattern = %d [%p] %m%n\n");
    fwrite(log_str.c_str(), log_str.size(), 1, file);
    fclose(file);
}

static transport::MessagePtr CreateTransactionWithAttr(
        std::shared_ptr<security::Security>& security,
        const std::string& gid,
        const std::string& from_prikey,
        const std::string& to,
        const std::string& key,
        const std::string& val,
        uint64_t amount,
        uint64_t gas_limit,
        uint64_t gas_price,
        int32_t des_net_id) {
    auto msg_ptr = std::make_shared<transport::TransportMessage>();
    transport::protobuf::Header& msg = msg_ptr->header;
    dht::DhtKeyManager dht_key(des_net_id);
    msg.set_src_sharding_id(des_net_id);
    msg.set_des_dht_key(dht_key.StrKey());
    msg.set_type(common::kPoolsMessage);
    msg.set_hop_count(0);
    auto broadcast = msg.mutable_broadcast();
    auto new_tx = msg.mutable_tx_proto();
    new_tx->set_gid(gid);
    new_tx->set_pubkey(security->GetPublicKeyUnCompressed());
    new_tx->set_step(pools::protobuf::kNormalFrom);
    new_tx->set_to(to);
    new_tx->set_amount(amount);
    new_tx->set_gas_limit(gas_limit);
    new_tx->set_gas_price(gas_price);
    if (!key.empty()) {
        if (key == "create_contract") {
            new_tx->set_step(pools::protobuf::kContractCreate);
            new_tx->set_contract_code(val);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "prepayment") {
            new_tx->set_step(pools::protobuf::kContractGasPrepayment);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "call") {
            new_tx->set_step(pools::protobuf::kContractExcute);
            new_tx->set_contract_input(val);
        } else {
            new_tx->set_key(key);
            if (!val.empty()) {
                new_tx->set_value(val);
            }
        }
    }

    transport::TcpTransport::Instance()->SetMessageHash(msg);
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign;
    if (security->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return nullptr;
    }

    msg.set_sign(sign);
    assert(new_tx->gas_price() > 0);
    return msg_ptr;
}

static std::unordered_map<std::string, std::string> addrs_map;
static std::vector<std::string> prikeys;
static std::vector<std::string> addrs;
static std::unordered_map<std::string, std::string> pri_pub_map;
static void LoadAllAccounts() {
    FILE* fd = fopen("../src/consensus/tests/init_acc", "r");
    if (fd == nullptr) {
        std::cout << "invalid init acc file." << std::endl;
        exit(1);
    }

    bool res = true;
    std::string filed;
    const uint32_t kMaxLen = 1024;
    char* read_buf = new char[kMaxLen];
    while (true) {
        char* read_res = fgets(read_buf, kMaxLen, fd);
        if (read_res == NULL) {
            break;
        }

        common::Split<> split(read_buf, '\t');
        if (split.Count() != 2) {
            break;
        }

        std::string prikey = common::Encode::HexDecode(
            std::string(split[1], split.SubLen(1) - 1));
        std::string addr = common::Encode::HexDecode(split[0]);
        addrs_map[prikey] = addr;
        addrs.push_back(addr);
        prikeys.push_back(prikey);
        std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
        security->SetPrivateKey(prikey);
        pri_pub_map[prikey] = security->GetPublicKey();
    }

    if (prikeys.size() != 256) {
        std::cout << "invalid init acc file." << std::endl;
        exit(1);
    }

    fclose(fd);
    delete[]read_buf;
}

int tx_main(int argc, char** argv) {
    LoadAllAccounts();
    SignalRegister();
    WriteDefaultLogConf();
    log4cpp::PropertyConfigurator::configure("./log4cpp.properties");
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init("./txclidb")) {
        std::cout << "init db failed!" << std::endl;
        return 1;
    }

    std::string val;
    uint64_t pos = 0;
    if (db_ptr->Get("txcli_pos", &val).ok()) {
        if (!common::StringUtil::ToUint64(val, &pos)) {
            std::cout << "get pos failed!" << std::endl;
            return 1;
        }
    }

    if (net_handler.Init(db_ptr) != 0) {
        std::cout << "init net handler failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Init(
            "127.0.0.1:13791",
            128,
            false,
            &net_handler) != 0) {
        std::cout << "init tcp client failed!" << std::endl;
        return 1;
    }
    
    if (transport::TcpTransport::Instance()->Start(false) != 0) {
        std::cout << "start tcp client failed!" << std::endl;
        return 1;
    }

    std::string gid = common::Random::RandomString(32);
    std::string prikey = common::Encode::HexDecode("03e76ff611e362d392efe693fe3e55e0e8ad9ea1cac77450fa4e56b35594fe11");
    std::string to = common::Encode::HexDecode("d9ec5aff3001dece14e1f4a35a39ed506bd6274b");
    uint32_t prikey_pos = 0;
    auto from_prikey = prikeys[prikey_pos % prikeys.size()];
    security->SetPrivateKey(from_prikey);
    uint64_t now_tm_us = common::TimeUtils::TimestampUs();
    uint32_t count = 0;
    for (; pos < common::kInvalidUint64 && !global_stop; ++pos) {
        uint64_t* gid_int = (uint64_t*)gid.data();
        gid_int[0] = pos;
        if (addrs_map[from_prikey] == to) {
            ++prikey_pos;
            from_prikey = prikeys[prikey_pos % prikeys.size()];
            security->SetPrivateKey(from_prikey);
            continue;
        }

        auto tx_msg_ptr = CreateTransactionWithAttr(
            security,
            gid,
            from_prikey,
            to,
            "",
            "",
            100000,
            10000000,
            ((uint32_t)(1000 - pos)) % 1000 + 1,
            3);
        if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 23001, tx_msg_ptr->header) != 0) {
            std::cout << "send tcp client failed!" << std::endl;
            return 1;
        }

        if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 22001, tx_msg_ptr->header) != 0) {
            std::cout << "send tcp client failed!" << std::endl;
            return 1;
        }

        if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 21001, tx_msg_ptr->header) != 0) {
            std::cout << "send tcp client failed!" << std::endl;
            return 1;
        }

        std::cout << "from private key: " << common::Encode::HexEncode(from_prikey) << ", to: " << common::Encode::HexEncode(to) << ", tx hash: " << tx_msg_ptr->header.hash64() << std::endl;
        if (pos % 1 == 0) {
            ++prikey_pos;
            from_prikey = prikeys[prikey_pos % prikeys.size()];
            security->SetPrivateKey(from_prikey);
            usleep(1000000);
        }
    }

    if (!db_ptr->Put("txcli_pos", std::to_string(pos)).ok()) {
        std::cout << "save pos failed!" << std::endl;
        return 1;
    }

    return 0;
}

int one_tx_main(int argc, char** argv) {
    LoadAllAccounts();
    SignalRegister();
    WriteDefaultLogConf();
    log4cpp::PropertyConfigurator::configure("./log4cpp.properties");
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init("./txclidb")) {
        std::cout << "init db failed!" << std::endl;
        return 1;
    }

    std::string val;
    uint64_t pos = 0;
    if (db_ptr->Get("txcli_pos", &val).ok()) {
        if (!common::StringUtil::ToUint64(val, &pos)) {
            std::cout << "get pos failed!" << std::endl;
            return 1;
        }
    }

    if (net_handler.Init(db_ptr) != 0) {
        std::cout << "init net handler failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Init(
            "127.0.0.1:13791",
            128,
            false,
            &net_handler) != 0) {
        std::cout << "init tcp client failed!" << std::endl;
        return 1;
    }
    
    if (transport::TcpTransport::Instance()->Start(false) != 0) {
        std::cout << "start tcp client failed!" << std::endl;
        return 1;
    }

    std::string gid = common::Random::RandomString(32);
    std::string prikey = common::Encode::HexDecode("03e76ff611e362d392efe693fe3e55e0e8ad9ea1cac77450fa4e56b35594fe11");
    std::string to = common::Encode::HexDecode(argv[2]);
    uint32_t prikey_pos = 0;
    auto from_prikey = prikeys[prikey_pos % prikeys.size()];
    security->SetPrivateKey(from_prikey);
    uint64_t now_tm_us = common::TimeUtils::TimestampUs();
    uint32_t count = 0;
    uint64_t* gid_int = (uint64_t*)gid.data();
    gid_int[0] = pos;
    if (addrs_map[from_prikey] == to) {
        ++prikey_pos;
        from_prikey = prikeys[prikey_pos % prikeys.size()];
        security->SetPrivateKey(from_prikey);
        return 1;
    }

    auto tx_msg_ptr = CreateTransactionWithAttr(
        security,
        gid,
        from_prikey,
        to,
        "",
        "",
        100000000lu,
        10000000,
        ((uint32_t)(1000 - pos)) % 1000 + 1,
        3);
    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 23001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 22001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 21001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }


    if (!db_ptr->Put("txcli_pos", std::to_string(pos)).ok()) {
        std::cout << "save pos failed!" << std::endl;
        return 1;
    }

    return 0;
}

int create_library(int argc, char** argv) {
    LoadAllAccounts();
    SignalRegister();
    WriteDefaultLogConf();
    log4cpp::PropertyConfigurator::configure("./log4cpp.properties");
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init("./txclidb")) {
        std::cout << "init db failed!" << std::endl;
        return 1;
    }

    std::string val;
    uint64_t pos = 0;
    if (db_ptr->Get("contract_pos", &val).ok()) {
        if (!common::StringUtil::ToUint64(val, &pos)) {
            std::cout << "get pos failed!" << std::endl;
            return 1;
        }
    }

    if (net_handler.Init(db_ptr) != 0) {
        std::cout << "init net handler failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Init(
        "127.0.0.1:13791",
        128,
        false,
        &net_handler) != 0) {
        std::cout << "init tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Start(false) != 0) {
        std::cout << "start tcp client failed!" << std::endl;
        return 1;
    }

    std::string gid = common::Random::RandomString(32);
    std::string prikey = common::Encode::HexDecode("03e76ff611e362d392efe693fe3e55e0e8ad9ea1cac77450fa4e56b35594fe11");
    uint32_t prikey_pos = 0;
    auto from_prikey = prikeys[254];
    security->SetPrivateKey(from_prikey);
    uint64_t now_tm_us = common::TimeUtils::TimestampUs();
    uint32_t count = 0;
    uint64_t* gid_int = (uint64_t*)gid.data();
    gid_int[0] = pos;
    std::string bytescode = common::Encode::HexDecode("61040d610053600b82828239805160001a607314610046577f4e487b7100000000000000000000000000000000000000000000000000000000600052600060045260246000fd5b30600052607381538281f3fe73000000000000000000000000000000000000000030146080604052600436106100565760003560e01c8063771602f71461005b578063a391c15b1461008b578063b67d77c5146100bb578063c8a4ac9c146100eb575b600080fd5b61007560048036038101906100709190610205565b61011b565b6040516100829190610254565b60405180910390f35b6100a560048036038101906100a09190610205565b610147565b6040516100b29190610254565b60405180910390f35b6100d560048036038101906100d09190610205565b610162565b6040516100e29190610254565b60405180910390f35b61010560048036038101906101009190610205565b610189565b6040516101129190610254565b60405180910390f35b600080828461012a919061029e565b90508381101561013d5761013c6102d2565b5b8091505092915050565b60008082846101569190610330565b90508091505092915050565b600082821115610175576101746102d2565b5b81836101819190610361565b905092915050565b60008082846101989190610395565b905060008414806101b357508284826101b19190610330565b145b6101c0576101bf6102d2565b5b8091505092915050565b600080fd5b6000819050919050565b6101e2816101cf565b81146101ed57600080fd5b50565b6000813590506101ff816101d9565b92915050565b6000806040838503121561021c5761021b6101ca565b5b600061022a858286016101f0565b925050602061023b858286016101f0565b9150509250929050565b61024e816101cf565b82525050565b60006020820190506102696000830184610245565b92915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b60006102a9826101cf565b91506102b4836101cf565b92508282019050808211156102cc576102cb61026f565b5b92915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052600160045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601260045260246000fd5b600061033b826101cf565b9150610346836101cf565b92508261035657610355610301565b5b828204905092915050565b600061036c826101cf565b9150610377836101cf565b925082820390508181111561038f5761038e61026f565b5b92915050565b60006103a0826101cf565b91506103ab836101cf565b92508282026103b9816101cf565b915082820484148315176103d0576103cf61026f565b5b509291505056fea26469706673582212209850f7b5b92245c41addb020bf5d3055d952405a32b53abda5541140ac3d65d964736f6c63430008110033");
    std::string to = security::GetContractAddress(security->GetAddress(), gid, common::Hash::keccak256(bytescode));
    auto tx_msg_ptr = CreateTransactionWithAttr(
        security,
        gid,
        from_prikey,
        to,
        "create_contract",
        bytescode,
        0,
        100000,
        10,
        3);
    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 23001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 22001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 21001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    std::cout << "from private key: " << common::Encode::HexEncode(from_prikey) << ", to: " << common::Encode::HexEncode(to) << std::endl;
    if (!db_ptr->Put("contract_pos", std::to_string(pos)).ok()) {
        std::cout << "save pos failed!" << std::endl;
        return 1;
    }

    return 0;
}

int contract_main(int argc, char** argv) {
    LoadAllAccounts();
    SignalRegister();
    WriteDefaultLogConf();
    log4cpp::PropertyConfigurator::configure("./log4cpp.properties");
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init("./txclidb")) {
        std::cout << "init db failed!" << std::endl;
        return 1;
    }

    std::string val;
    uint64_t pos = 0;
    if (db_ptr->Get("contract_pos", &val).ok()) {
        if (!common::StringUtil::ToUint64(val, &pos)) {
            std::cout << "get pos failed!" << std::endl;
            return 1;
        }
    }

    if (net_handler.Init(db_ptr) != 0) {
        std::cout << "init net handler failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Init(
            "127.0.0.1:13791",
            128,
            false,
            &net_handler) != 0) {
        std::cout << "init tcp client failed!" << std::endl;
        return 1;
    }
    
    if (transport::TcpTransport::Instance()->Start(false) != 0) {
        std::cout << "start tcp client failed!" << std::endl;
        return 1;
    }

    std::string gid = common::Random::RandomString(32);
    std::string prikey = common::Encode::HexDecode("03e76ff611e362d392efe693fe3e55e0e8ad9ea1cac77450fa4e56b35594fe11");
    uint32_t prikey_pos = 0;
    auto from_prikey = prikeys[254];
    security->SetPrivateKey(from_prikey);
    uint64_t now_tm_us = common::TimeUtils::TimestampUs();
    uint32_t count = 0;
    uint64_t* gid_int = (uint64_t*)gid.data();
    gid_int[0] = pos;
    std::string bytescode = common::Encode::HexDecode("608060405260405161095738038061095783398181016040528101906100259190610101565b600060039050826002819055508160038190555033600460006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff160217905550606f6000803373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002081905550505050610141565b600080fd5b6000819050919050565b6100de816100cb565b81146100e957600080fd5b50565b6000815190506100fb816100d5565b92915050565b60008060408385031215610118576101176100c6565b5b6000610126858286016100ec565b9250506020610137858286016100ec565b9150509250929050565b610807806101506000396000f3fe6080604052600436106100345760003560e01c80634162d68f146100395780638da5cb5b14610055578063d1e94e5714610080575b600080fd5b610053600480360381019061004e9190610552565b6100bd565b005b34801561006157600080fd5b5061006a610384565b60405161007791906105dc565b60405180910390f35b34801561008c57600080fd5b506100a760048036038101906100a29190610663565b6103aa565b6040516100b491906105dc565b60405180910390f35b60006003826040516100cf9190610714565b602060405180830381855afa1580156100ec573d6000803e3d6000fd5b5050506040515160601b6bffffffffffffffffffffffff1916905060006003905060006001905060008060003373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002054036101a25760636000803373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff168152602001908152602001600020819055506101f7565b6000803373ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16815260200190815260200160002060008154809291906101f19061075a565b91905055505b60016000848152602001908152602001600020339080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555060025460016000858152602001908152602001600020805490501061037e5760005b600254811015610342576001600085815260200190815260200160002081815481106102bc576102bb6107a2565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6003549081150290604051600060405180830381858888f1935050505015801561032e573d6000803e3d6000fd5b50808061033a9061075a565b91505061028d565b50600460009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16ff5b50505050565b600460009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600160205281600052604060002081815481106103c657600080fd5b906000526020600020016000915091509054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b61045f82610416565b810181811067ffffffffffffffff8211171561047e5761047d610427565b5b80604052505050565b60006104916103f8565b905061049d8282610456565b919050565b600067ffffffffffffffff8211156104bd576104bc610427565b5b6104c682610416565b9050602081019050919050565b82818337600083830152505050565b60006104f56104f0846104a2565b610487565b90508281526020810184848401111561051157610510610411565b5b61051c8482856104d3565b509392505050565b600082601f8301126105395761053861040c565b5b81356105498482602086016104e2565b91505092915050565b60006020828403121561056857610567610402565b5b600082013567ffffffffffffffff81111561058657610585610407565b5b61059284828501610524565b91505092915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b60006105c68261059b565b9050919050565b6105d6816105bb565b82525050565b60006020820190506105f160008301846105cd565b92915050565b6000819050919050565b61060a816105f7565b811461061557600080fd5b50565b60008135905061062781610601565b92915050565b6000819050919050565b6106408161062d565b811461064b57600080fd5b50565b60008135905061065d81610637565b92915050565b6000806040838503121561067a57610679610402565b5b600061068885828601610618565b92505060206106998582860161064e565b9150509250929050565b600081519050919050565b600081905092915050565b60005b838110156106d75780820151818401526020810190506106bc565b60008484015250505050565b60006106ee826106a3565b6106f881856106ae565b93506107088185602086016106b9565b80840191505092915050565b600061072082846106e3565b915081905092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b60006107658261062d565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff82036107975761079661072b565b5b600182019050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fdfea26469706673582212203cadcb595c79e9eb9aeabdc3bf930a6e8d55c1388f71a7d538581e3412ff576c64736f6c6343000811003300000000000000000000000000000000000000000000000000000000000027100000000000000000000000000000000000000000000000000000000005f5e100");
    if (argc > 2) {
        std::string library_addr = argv[2];
        bytescode = std::string("608060405234801561001057600080fd5b5061023b806100206000396000f3fe608060405234801561001057600080fd5b506004361061002b5760003560e01c80638e86b12514610030575b600080fd5b61004a60048036038101906100459190610121565b610060565b6040516100579190610170565b60405180910390f35b60008273") +
            library_addr +
            "63771602f79091846040518363ffffffff1660e01b815260040161009d92919061019a565b602060405180830381865af41580156100ba573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906100de91906101d8565b905092915050565b600080fd5b6000819050919050565b6100fe816100eb565b811461010957600080fd5b50565b60008135905061011b816100f5565b92915050565b60008060408385031215610138576101376100e6565b5b60006101468582860161010c565b92505060206101578582860161010c565b9150509250929050565b61016a816100eb565b82525050565b60006020820190506101856000830184610161565b92915050565b610194816100eb565b82525050565b60006040820190506101af600083018561018b565b6101bc602083018461018b565b9392505050565b6000815190506101d2816100f5565b92915050565b6000602082840312156101ee576101ed6100e6565b5b60006101fc848285016101c3565b9150509291505056fea264697066735822122035fac71bbc010b1850ea341ac28d89c76583b1690d2f5b2265574b844d45118264736f6c63430008110033";
        bytescode = common::Encode::HexDecode(bytescode);
    }

    std::string to = security::GetContractAddress(security->GetAddress(), gid, common::Hash::keccak256(bytescode));
    auto tx_msg_ptr = CreateTransactionWithAttr(
        security,
        gid,
        from_prikey,
        to,
        "create_contract",
        bytescode,
        100000,
        10000000,
        10,
        3);
    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 23001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 22001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 21001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    std::cout << "from private key: " << common::Encode::HexEncode(from_prikey) << ", to: " << common::Encode::HexEncode(to) << std::endl;
    if (!db_ptr->Put("contract_pos", std::to_string(pos)).ok()) {
        std::cout << "save pos failed!" << std::endl;
        return 1;
    }

    return 0;
}

int contract_set_prepayment(int argc, char** argv) {
    LoadAllAccounts();
    SignalRegister();
    WriteDefaultLogConf();
    log4cpp::PropertyConfigurator::configure("./log4cpp.properties");
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init("./txclidb")) {
        std::cout << "init db failed!" << std::endl;
        return 1;
    }

    std::string val;
    uint64_t pos = 0;
    if (db_ptr->Get("contract_pos", &val).ok()) {
        if (!common::StringUtil::ToUint64(val, &pos)) {
            std::cout << "get pos failed!" << std::endl;
            return 1;
        }
    }

    if (net_handler.Init(db_ptr) != 0) {
        std::cout << "init net handler failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Init(
            "127.0.0.1:13791",
            128,
            false,
            &net_handler) != 0) {
        std::cout << "init tcp client failed!" << std::endl;
        return 1;
    }
    
    if (transport::TcpTransport::Instance()->Start(false) != 0) {
        std::cout << "start tcp client failed!" << std::endl;
        return 1;
    }

    std::string gid = common::Random::RandomString(32);
    std::string prikey = common::Encode::HexDecode("03e76ff611e362d392efe693fe3e55e0e8ad9ea1cac77450fa4e56b35594fe11");
    uint32_t prikey_pos = 0;
    auto from_prikey = prikeys[254];
    security->SetPrivateKey(from_prikey);
    uint64_t now_tm_us = common::TimeUtils::TimestampUs();
    uint32_t count = 0;
    uint64_t* gid_int = (uint64_t*)gid.data();
    gid_int[0] = pos;
    std::string to = common::Encode::HexDecode(argv[2]);
    auto tx_msg_ptr = CreateTransactionWithAttr(
        security,
        gid,
        from_prikey,
        to,
        "prepayment",
        "",
        100000,
        10000000,
        10,
        3);
    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 23001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 22001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 21001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    std::cout << "from private key: " << common::Encode::HexEncode(from_prikey) << ", to: " << common::Encode::HexEncode(to) << std::endl;
    if (!db_ptr->Put("contract_pos", std::to_string(pos)).ok()) {
        std::cout << "save pos failed!" << std::endl;
        return 1;
    }

    return 0;
}

int contract_call(int argc, char** argv) {
    LoadAllAccounts();
    SignalRegister();
    WriteDefaultLogConf();
    log4cpp::PropertyConfigurator::configure("./log4cpp.properties");
    transport::MultiThreadHandler net_handler;
    std::shared_ptr<security::Security> security = std::make_shared<security::Ecdsa>();
    auto db_ptr = std::make_shared<db::Db>();
    if (!db_ptr->Init("./txclidb")) {
        std::cout << "init db failed!" << std::endl;
        return 1;
    }

    std::string val;
    uint64_t pos = 0;
    if (db_ptr->Get("contract_pos", &val).ok()) {
        if (!common::StringUtil::ToUint64(val, &pos)) {
            std::cout << "get pos failed!" << std::endl;
            return 1;
        }
    }

    if (net_handler.Init(db_ptr) != 0) {
        std::cout << "init net handler failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Init(
        "127.0.0.1:13791",
        128,
        false,
        &net_handler) != 0) {
        std::cout << "init tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Start(false) != 0) {
        std::cout << "start tcp client failed!" << std::endl;
        return 1;
    }

    std::string gid = common::Random::RandomString(32);
    std::string prikey = common::Encode::HexDecode("03e76ff611e362d392efe693fe3e55e0e8ad9ea1cac77450fa4e56b35594fe11");
    uint32_t prikey_pos = 0;
    auto from_prikey = prikeys[254];
    security->SetPrivateKey(from_prikey);
    uint64_t now_tm_us = common::TimeUtils::TimestampUs();
    uint32_t count = 0;
    uint64_t* gid_int = (uint64_t*)gid.data();
    gid_int[0] = pos;
    std::string to = common::Encode::HexDecode(argv[2]);
    std::string input = common::Encode::HexDecode("4162d68f00000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000006706b656574310000000000000000000000000000000000000000000000000000");
    if (argc > 3) {
        input = common::Encode::HexDecode(argv[3]);
        std::cout << "use input: " << argv[3] << std::endl;
    }

    auto tx_msg_ptr = CreateTransactionWithAttr(
        security,
        gid,
        from_prikey,
        to,
        "call",
        input,
        100000,
        10000000,
        10,
        3);
    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 23001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 22001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 21001, tx_msg_ptr->header) != 0) {
        std::cout << "send tcp client failed!" << std::endl;
        return 1;
    }

    std::cout << "from private key: " << common::Encode::HexEncode(from_prikey) << ", to: " << common::Encode::HexEncode(to) << std::endl;
    if (!db_ptr->Put("contract_pos", std::to_string(pos)).ok()) {
        std::cout << "save pos failed!" << std::endl;
        return 1;
    }

    return 0;
}

int main(int argc, char** argv) {
    std::cout << argc << std::endl;
    if (argc <= 1) {
        tx_main(argc, argv);
        return 0;
    }

    if (argv[1][0] == '1') {
        contract_main(argc, argv);
    } else if (argv[1][0] == '2') {
        if (argc > 2) {
            contract_set_prepayment(argc, argv);
        }
    } else if (argv[1][0] == '3') {
        if (argc > 2) {
            contract_call(argc, argv);
        }
    } else if (argv[1][0] == '4') {
        create_library(argc, argv);
    } else {
        one_tx_main(argc, argv);
    }

    return 0;
}
