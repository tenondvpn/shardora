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
//     auto broadcast = msg.mutable_broadcast();
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

    transport::TcpTransport::Instance()->SetMessageHash(msg, 0);
//     std::cout << "tx from: " << common::Encode::HexEncode(security->GetAddress())
//         << " to: " << common::Encode::HexEncode(to)
//         << " gid: " << common::Encode::HexEncode(gid)
//         << " amount: " << amount
//         << " hash64: " << msg.hash64()
//         << std::endl;
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

//         std::cout << "from private key: " << common::Encode::HexEncode(from_prikey) << ", to: " << common::Encode::HexEncode(to) << ", tx hash: " << tx_msg_ptr->header.hash64() << std::endl;
        if (pos % 1000 == 0) {
            ++prikey_pos;
            from_prikey = prikeys[prikey_pos % prikeys.size()];
            security->SetPrivateKey(from_prikey);
            usleep(100000);
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
    std::string bytescode = common::Encode::HexDecode("60806040523480156200001157600080fd5b506040516200204838038062002048833981810160405281019062000037919062000334565b80600090805190602001906200004f92919062000098565b5033600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055505062000385565b82805482825590600052602060002090810192821562000114579160200282015b82811115620001135782518260006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555091602001919060010190620000b9565b5b50905062000123919062000127565b5090565b5b808211156200014257600081600090555060010162000128565b5090565b6000604051905090565b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b620001aa826200015f565b810181811067ffffffffffffffff82111715620001cc57620001cb62000170565b5b80604052505050565b6000620001e162000146565b9050620001ef82826200019f565b919050565b600067ffffffffffffffff82111562000212576200021162000170565b5b602082029050602081019050919050565b600080fd5b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000620002558262000228565b9050919050565b620002678162000248565b81146200027357600080fd5b50565b60008151905062000287816200025c565b92915050565b6000620002a46200029e84620001f4565b620001d5565b90508083825260208201905060208402830185811115620002ca57620002c962000223565b5b835b81811015620002f75780620002e2888262000276565b845260208401935050602081019050620002cc565b5050509392505050565b600082601f8301126200031957620003186200015a565b5b81516200032b8482602086016200028d565b91505092915050565b6000602082840312156200034d576200034c62000150565b5b600082015167ffffffffffffffff8111156200036e576200036d62000155565b5b6200037c8482850162000301565b91505092915050565b611cb380620003956000396000f3fe608060405234801561001057600080fd5b50600436106100f55760003560e01c80636b83e05911610097578063ae0ad9d911610066578063ae0ad9d9146102c1578063b89e70b5146102f1578063e9c7feda14610321578063fdba8f1114610351576100f5565b80636b83e059146102105780637e280378146102405780638da5cb5b14610272578063a51f8aac14610290576100f5565b80634819544b116100d35780634819544b146101765780635f13d6ee146101a85780636189d2df146101d8578063691b3463146101f4576100f5565b806308ad872a146100fa5780630ef403f81461011657806329c1cb4b14610146575b600080fd5b610114600480360381019061010f9190611110565b61036d565b005b610130600480360381019061012b919061119b565b6104d5565b60405161013d91906111e3565b60405180910390f35b610160600480360381019061015b91906111fe565b610502565b60405161016d91906111e3565b60405180910390f35b610190600480360381019061018b9190611290565b610549565b60405161019f939291906112ee565b60405180910390f35b6101c260048036038101906101bd9190611325565b61059d565b6040516101cf9190611393565b60405180910390f35b6101f260048036038101906101ed9190611534565b6105dc565b005b61020e600480360381019061020991906115d3565b61078c565b005b61022a600480360381019061022591906111fe565b610899565b60405161023791906111e3565b60405180910390f35b61025a6004803603810190610255919061119b565b6108de565b604051610269939291906116dd565b60405180910390f35b61027a610a25565b6040516102879190611393565b60405180910390f35b6102aa60048036038101906102a5919061119b565b610a4b565b6040516102b8929190611722565b60405180910390f35b6102db60048036038101906102d6919061174b565b610a7c565b6040516102e891906111e3565b60405180910390f35b61030b6004803603810190610306919061119b565b610ac1565b60405161031891906111e3565b60405180910390f35b61033b600480360381019061033691906111fe565b610b48565b60405161034891906111e3565b60405180910390f35b61036b60048036038101906103669190611110565b610c61565b005b80516000805490501461037f57600080fd5b60005b815181101561043f576000818154811061039f5761039e6117a7565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1661040c6103ec85610dc9565b8484815181106103ff576103fe6117a7565b5b6020026020010151610df9565b73ffffffffffffffffffffffffffffffffffffffff161461042c57600080fd5b808061043790611805565b915050610382565b5060016003846040516104529190611889565b9081526020016040518091039020600084815260200190815260200160002060006101000a81548160ff021916908315150217905550600160046000848152602001908152602001600020846040516104ab9190611889565b908152602001604051809103902060006101000a81548160ff021916908315150217905550505050565b60006002600083815260200190815260200160002060020160009054906101000a900460ff169050919050565b60006003836040516105149190611889565b9081526020016040518091039020600083815260200190815260200160002060009054906101000a900460ff16905092915050565b6005602052816000526040600020818154811061056557600080fd5b9060005260206000209060030201600091509150508060000154908060010154908060020160009054906101000a900460ff16905083565b600081815481106105ad57600080fd5b906000526020600020016000915054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b3373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff161461063657600080fd5b805182511461064457600080fd5b604051806040016040528084815260200160011515815250600660008681526020019081526020016000206000820151816000015560208201518160010160006101000a81548160ff02191690831515021790555090505060008251905060005b81811015610784576005600086815260200190815260200160002060405180606001604052808684815181106106de576106dd6117a7565b5b602002602001015181526020018584815181106106fe576106fd6117a7565b5b60200260200101518152602001600115158152509080600181540180825580915050600190039060005260206000209060030201600090919091909150600082015181600001556020820151816001015560408201518160020160006101000a81548160ff0219169083151502179055505050808061077c90611805565b9150506106a5565b505050505050565b3373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16146107e657600080fd5b6002600084815260200190815260200160002060020160009054906101000a900460ff161561081457600080fd5b60405180606001604052808381526020018281526020016001151581525060026000858152602001908152602001600020600082015181600001908161085a9190611aac565b5060208201518160010190816108709190611aac565b5060408201518160020160006101000a81548160ff021916908315150217905550905050505050565b6003828051602081018201805184825260208301602085012081835280955050505050506020528060005260406000206000915091509054906101000a900460ff1681565b6002602052806000526040600020600091509050806000018054610901906118cf565b80601f016020809104026020016040519081016040528092919081815260200182805461092d906118cf565b801561097a5780601f1061094f5761010080835404028352916020019161097a565b820191906000526020600020905b81548152906001019060200180831161095d57829003601f168201915b50505050509080600101805461098f906118cf565b80601f01602080910402602001604051908101604052809291908181526020018280546109bb906118cf565b8015610a085780601f106109dd57610100808354040283529160200191610a08565b820191906000526020600020905b8154815290600101906020018083116109eb57829003601f168201915b5050505050908060020160009054906101000a900460ff16905083565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60066020528060005260406000206000915090508060000154908060010160009054906101000a900460ff16905082565b6004602052816000526040600020818051602081018201805184825260208301602085012081835280955050505050506000915091509054906101000a900460ff1681565b60003373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1614610b1d57600080fd5b6006600083815260200190815260200160002060010160009054906101000a900460ff169050919050565b6000806005600084815260200190815260200160002080549050905060008111610b7157600080fd5b60005b81811015610c545760046000600560008781526020019081526020016000208381548110610ba557610ba46117a7565b5b906000526020600020906003020160000154815260200190815260200160002085604051610bd39190611889565b908152602001604051809103902060009054906101000a900460ff168015610c31575042600560008681526020019081526020016000208281548110610c1c57610c1b6117a7565b5b90600052602060002090600302016001015410155b15610c4157600192505050610c5b565b8080610c4c90611805565b915050610b74565b5060009150505b92915050565b805160008054905014610c7357600080fd5b60005b8151811015610d335760008181548110610c9357610c926117a7565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16610d00610ce085610dc9565b848481518110610cf357610cf26117a7565b5b6020026020010151610df9565b73ffffffffffffffffffffffffffffffffffffffff1614610d2057600080fd5b8080610d2b90611805565b915050610c76565b506001600384604051610d469190611889565b9081526020016040518091039020600084815260200190815260200160002060006101000a81548160ff02191690831515021790555060016004600084815260200190815260200160002084604051610d9f9190611889565b908152602001604051809103902060006101000a81548160ff021916908315150217905550505050565b600081604051602001610ddc9190611bf6565b604051602081830303815290604052805190602001209050919050565b600080600080610e0885610e68565b92509250925060018684848460405160008152602001604052604051610e319493929190611c38565b6020604051602081039080840390855afa158015610e53573d6000803e3d6000fd5b50505060206040510351935050505092915050565b60008060006041845114610e7b57600080fd5b6020840151915060408401519050606084015160001a92509193909250565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b610f0182610eb8565b810181811067ffffffffffffffff82111715610f2057610f1f610ec9565b5b80604052505050565b6000610f33610e9a565b9050610f3f8282610ef8565b919050565b600067ffffffffffffffff821115610f5f57610f5e610ec9565b5b610f6882610eb8565b9050602081019050919050565b82818337600083830152505050565b6000610f97610f9284610f44565b610f29565b905082815260208101848484011115610fb357610fb2610eb3565b5b610fbe848285610f75565b509392505050565b600082601f830112610fdb57610fda610eae565b5b8135610feb848260208601610f84565b91505092915050565b6000819050919050565b61100781610ff4565b811461101257600080fd5b50565b60008135905061102481610ffe565b92915050565b600067ffffffffffffffff82111561104557611044610ec9565b5b602082029050602081019050919050565b600080fd5b600061106e6110698461102a565b610f29565b9050808382526020820190506020840283018581111561109157611090611056565b5b835b818110156110d857803567ffffffffffffffff8111156110b6576110b5610eae565b5b8086016110c38982610fc6565b85526020850194505050602081019050611093565b5050509392505050565b600082601f8301126110f7576110f6610eae565b5b813561110784826020860161105b565b91505092915050565b60008060006060848603121561112957611128610ea4565b5b600084013567ffffffffffffffff81111561114757611146610ea9565b5b61115386828701610fc6565b935050602061116486828701611015565b925050604084013567ffffffffffffffff81111561118557611184610ea9565b5b611191868287016110e2565b9150509250925092565b6000602082840312156111b1576111b0610ea4565b5b60006111bf84828501611015565b91505092915050565b60008115159050919050565b6111dd816111c8565b82525050565b60006020820190506111f860008301846111d4565b92915050565b6000806040838503121561121557611214610ea4565b5b600083013567ffffffffffffffff81111561123357611232610ea9565b5b61123f85828601610fc6565b925050602061125085828601611015565b9150509250929050565b6000819050919050565b61126d8161125a565b811461127857600080fd5b50565b60008135905061128a81611264565b92915050565b600080604083850312156112a7576112a6610ea4565b5b60006112b585828601611015565b92505060206112c68582860161127b565b9150509250929050565b6112d981610ff4565b82525050565b6112e88161125a565b82525050565b600060608201905061130360008301866112d0565b61131060208301856112df565b61131d60408301846111d4565b949350505050565b60006020828403121561133b5761133a610ea4565b5b60006113498482850161127b565b91505092915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b600061137d82611352565b9050919050565b61138d81611372565b82525050565b60006020820190506113a86000830184611384565b92915050565b600067ffffffffffffffff8211156113c9576113c8610ec9565b5b602082029050602081019050919050565b60006113ed6113e8846113ae565b610f29565b905080838252602082019050602084028301858111156114105761140f611056565b5b835b8181101561143957806114258882611015565b845260208401935050602081019050611412565b5050509392505050565b600082601f83011261145857611457610eae565b5b81356114688482602086016113da565b91505092915050565b600067ffffffffffffffff82111561148c5761148b610ec9565b5b602082029050602081019050919050565b60006114b06114ab84611471565b610f29565b905080838252602082019050602084028301858111156114d3576114d2611056565b5b835b818110156114fc57806114e8888261127b565b8452602084019350506020810190506114d5565b5050509392505050565b600082601f83011261151b5761151a610eae565b5b813561152b84826020860161149d565b91505092915050565b6000806000806080858703121561154e5761154d610ea4565b5b600061155c87828801611015565b945050602061156d87828801611015565b935050604085013567ffffffffffffffff81111561158e5761158d610ea9565b5b61159a87828801611443565b925050606085013567ffffffffffffffff8111156115bb576115ba610ea9565b5b6115c787828801611506565b91505092959194509250565b6000806000606084860312156115ec576115eb610ea4565b5b60006115fa86828701611015565b935050602084013567ffffffffffffffff81111561161b5761161a610ea9565b5b61162786828701610fc6565b925050604084013567ffffffffffffffff81111561164857611647610ea9565b5b61165486828701610fc6565b9150509250925092565b600081519050919050565b600082825260208201905092915050565b60005b8381101561169857808201518184015260208101905061167d565b60008484015250505050565b60006116af8261165e565b6116b98185611669565b93506116c981856020860161167a565b6116d281610eb8565b840191505092915050565b600060608201905081810360008301526116f781866116a4565b9050818103602083015261170b81856116a4565b905061171a60408301846111d4565b949350505050565b600060408201905061173760008301856112d0565b61174460208301846111d4565b9392505050565b6000806040838503121561176257611761610ea4565b5b600061177085828601611015565b925050602083013567ffffffffffffffff81111561179157611790610ea9565b5b61179d85828601610fc6565b9150509250929050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b60006118108261125a565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff8203611842576118416117d6565b5b600182019050919050565b600081905092915050565b60006118638261165e565b61186d818561184d565b935061187d81856020860161167a565b80840191505092915050565b60006118958284611858565b915081905092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b600060028204905060018216806118e757607f821691505b6020821081036118fa576118f96118a0565b5b50919050565b60008190508160005260206000209050919050565b60006020601f8301049050919050565b600082821b905092915050565b6000600883026119627fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff82611925565b61196c8683611925565b95508019841693508086168417925050509392505050565b6000819050919050565b60006119a96119a461199f8461125a565b611984565b61125a565b9050919050565b6000819050919050565b6119c38361198e565b6119d76119cf826119b0565b848454611932565b825550505050565b600090565b6119ec6119df565b6119f78184846119ba565b505050565b5b81811015611a1b57611a106000826119e4565b6001810190506119fd565b5050565b601f821115611a6057611a3181611900565b611a3a84611915565b81016020851015611a49578190505b611a5d611a5585611915565b8301826119fc565b50505b505050565b600082821c905092915050565b6000611a8360001984600802611a65565b1980831691505092915050565b6000611a9c8383611a72565b9150826002028217905092915050565b611ab58261165e565b67ffffffffffffffff811115611ace57611acd610ec9565b5b611ad882546118cf565b611ae3828285611a1f565b600060209050601f831160018114611b165760008415611b04578287015190505b611b0e8582611a90565b865550611b76565b601f198416611b2486611900565b60005b82811015611b4c57848901518255600182019150602085019450602081019050611b27565b86831015611b695784890151611b65601f891682611a72565b8355505b6001600288020188555050505b505050505050565b600081905092915050565b7f19457468657265756d205369676e6564204d6573736167653a0a333200000000600082015250565b6000611bbf601c83611b7e565b9150611bca82611b89565b601c82019050919050565b6000819050919050565b611bf0611beb82610ff4565b611bd5565b82525050565b6000611c0182611bb2565b9150611c0d8284611bdf565b60208201915081905092915050565b600060ff82169050919050565b611c3281611c1c565b82525050565b6000608082019050611c4d60008301876112d0565b611c5a6020830186611c29565b611c6760408301856112d0565b611c7460608301846112d0565b9594505050505056fea264697066735822122054e7b284ecf050e5f1d00855973898b681b5ff7ba7135d1c867fc0fb864bfdd464736f6c6343000811003300000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000003000000000000000000000000e252d01a37b85e2007ed3cc13797aa92496204a40000000000000000000000005f15294a1918633d4dd4ec47098a14d01c58e957000000000000000000000000d45cfd6855c6ec8f635a6f2b46c647e99c59c79d");
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
        0,
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

int contract_call(int argc, char** argv, bool more=false) {
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

    for (uint32_t i = 0; i < 100000u; ++i) {
        uint64_t* gid_int = (uint64_t*)gid.data();
        gid_int[0] = i;
        auto tx_msg_ptr = CreateTransactionWithAttr(
            security,
            gid,
            from_prikey,
            to,
            "call",
            input,
            0,
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

        if (!more) {
            break;
        }

        if (i % 100 == 0) {
            usleep(100000);
        }
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
            contract_call(argc, argv, true);
        }
    } else if (argv[1][0] == '4') {
        create_library(argc, argv);
    } else {
        one_tx_main(argc, argv);
    }

    return 0;
}
