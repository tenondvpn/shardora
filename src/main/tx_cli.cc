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

    transport::TcpTransport::Instance()->SetMessageHash(msg, 0);
    auto tx_hash = pools::GetTxMessageHash(*new_tx);
    std::string sign;
    if (security->Sign(tx_hash, &sign) != security::kSecuritySuccess) {
        assert(false);
        return nullptr;
    }

//     std::string test_sign = common::Encode::HexDecode("24e113bf95efa71f9ac4fe941a00091a241cd55dfba119675aa5e48adf680c60") +
//         common::Encode::HexDecode("4d74b3e55148ef7ecc4404f3514c94b0bd4a9ac9d207e7b5a9085e070db8231c") + "\0";
//     std::string test_pk = common::Encode::HexDecode("04847869854f544cb4bdc20a32a2cb4c284e8f6eb43d3fd4fbe6c0fa130202e4dee60a2969442beadc9cf5dd8464ef98873a8cba950b9134058e6e1224de7b8d57");
//     if (security->Verify(tx_hash, test_pk, test_sign) != security::kSecuritySuccess) {
//         std::cout << "verify test sign failed!" << std::endl;
//         assert(false);
//         return nullptr;
//     }

    std::cout << "tx gid: " << common::Encode::HexEncode(new_tx->gid())
        << " tx pukey: " << common::Encode::HexEncode(new_tx->pubkey())
        << " tx to: " << common::Encode::HexEncode(new_tx->to())
        << " tx hash: " << common::Encode::HexEncode(tx_hash)
        << " tx sign: " << common::Encode::HexEncode(sign)
        << std::endl;
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
    std::string prikey = common::Encode::HexDecode("263960455acf4ad92ce0911de10d2f995c806e3e6d1c931f085752e65aca496c");
    std::string to = common::Encode::HexDecode("d9ec5aff3001dece14e1f4a35a39ed506bd6274b");
    uint32_t prikey_pos = 0;
    auto from_prikey = prikey;
    security->SetPrivateKey(from_prikey);
    uint64_t now_tm_us = common::TimeUtils::TimestampUs();
    uint32_t count = 0;
    for (; pos < common::kInvalidUint64 && !global_stop; ++pos) {
        uint64_t* gid_int = (uint64_t*)gid.data();
        gid_int[0] = pos;
//         if (addrs_map[from_prikey] == to) {
//             ++prikey_pos;
//             from_prikey = prikeys[prikey_pos % prikeys.size()];
//             security->SetPrivateKey(from_prikey);
//             continue;
//         }
/*
        uint32_t* tmp_data = (uint32_t*)to.c_str();
        if (common::Random::RandomInt32() % 10 < 3) {
            tmp_data[0] = common::Random::RandomInt16();
        }
*/
        std::cout << "tttt: " << std::endl;
        gid = common::Encode::HexDecode("263960455acf4ad92ce0911de10d2f995c806e3e6d1c931f085752e65aca496c");
        auto tx_msg_ptr = CreateTransactionWithAttr(
            security,
            gid,
            from_prikey,
            to,
            "",
            "",
            1980,
            10000,
            1,
            3);
        break;
//         if (transport::TcpTransport::Instance()->Send(0, "127.0.0.1", 23001, tx_msg_ptr->header) != 0) {
//             std::cout << "send tcp client failed!" << std::endl;
//             return 1;
//         }
//         if (transport::TcpTransport::Instance()->Send(0, "10.101.20.29", 21001, tx_msg_ptr->header) != 0) {
//             std::cout << "send tcp client failed!" << std::endl;
//             return 1;
//         }
// 
//         if (transport::TcpTransport::Instance()->Send(0, "10.101.20.30", 22001, tx_msg_ptr->header) != 0) {
//             std::cout << "send tcp client failed!" << std::endl;
//             return 1;
//         }
// 
//         if (transport::TcpTransport::Instance()->Send(0, "10.101.20.31", 23001, tx_msg_ptr->header) != 0) {
//             std::cout << "send tcp client failed!" << std::endl;
//             return 1;
//         }

//         std::cout << "from private key: " << common::Encode::HexEncode(from_prikey) << ", to: " << common::Encode::HexEncode(to) << ", tx hash: " << tx_msg_ptr->header.hash64() << std::endl;
        if (pos % 1 == 0) {
            ++prikey_pos;
            from_prikey = prikeys[prikey_pos % prikeys.size()];
            security->SetPrivateKey(from_prikey);
            usleep(10000);
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
//     if (transport::TcpTransport::Instance()->Send(0, "10.101.20.29", 21001, tx_msg_ptr->header) != 0) {
//         std::cout << "send tcp client failed!" << std::endl;
//         return 1;
//     }
// 
//     if (transport::TcpTransport::Instance()->Send(0, "10.101.20.30", 22001, tx_msg_ptr->header) != 0) {
//         std::cout << "send tcp client failed!" << std::endl;
//         return 1;
//     }
// 
//     if (transport::TcpTransport::Instance()->Send(0, "10.101.20.31", 23001, tx_msg_ptr->header) != 0) {
//         std::cout << "send tcp client failed!" << std::endl;
//         return 1;
//     }


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
    std::string bytescode = common::Encode::HexDecode("60806040523480156200001157600080fd5b506040516200201a3803806200201a833981810160405281019062000037919062000334565b80600090805190602001906200004f92919062000098565b5033600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055505062000385565b82805482825590600052602060002090810192821562000114579160200282015b82811115620001135782518260006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555091602001919060010190620000b9565b5b50905062000123919062000127565b5090565b5b808211156200014257600081600090555060010162000128565b5090565b6000604051905090565b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b620001aa826200015f565b810181811067ffffffffffffffff82111715620001cc57620001cb62000170565b5b80604052505050565b6000620001e162000146565b9050620001ef82826200019f565b919050565b600067ffffffffffffffff82111562000212576200021162000170565b5b602082029050602081019050919050565b600080fd5b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000620002558262000228565b9050919050565b620002678162000248565b81146200027357600080fd5b50565b60008151905062000287816200025c565b92915050565b6000620002a46200029e84620001f4565b620001d5565b90508083825260208201905060208402830185811115620002ca57620002c962000223565b5b835b81811015620002f75780620002e2888262000276565b845260208401935050602081019050620002cc565b5050509392505050565b600082601f8301126200031957620003186200015a565b5b81516200032b8482602086016200028d565b91505092915050565b6000602082840312156200034d576200034c62000150565b5b600082015167ffffffffffffffff8111156200036e576200036d62000155565b5b6200037c8482850162000301565b91505092915050565b611c8580620003956000396000f3fe608060405234801561001057600080fd5b50600436106100f55760003560e01c80636b83e05911610097578063ae0ad9d911610066578063ae0ad9d9146102c1578063b89e70b5146102f1578063e9c7feda14610321578063fdba8f1114610351576100f5565b80636b83e059146102105780637e280378146102405780638da5cb5b14610272578063a51f8aac14610290576100f5565b80634819544b116100d35780634819544b146101765780635f13d6ee146101a85780636189d2df146101d8578063691b3463146101f4576100f5565b806308ad872a146100fa5780630ef403f81461011657806329c1cb4b14610146575b600080fd5b610114600480360381019061010f91906110e2565b61036d565b005b610130600480360381019061012b919061116d565b6104d5565b60405161013d91906111b5565b60405180910390f35b610160600480360381019061015b91906111d0565b610502565b60405161016d91906111b5565b60405180910390f35b610190600480360381019061018b9190611262565b610549565b60405161019f939291906112c0565b60405180910390f35b6101c260048036038101906101bd91906112f7565b61059d565b6040516101cf9190611365565b60405180910390f35b6101f260048036038101906101ed9190611506565b6105dc565b005b61020e600480360381019061020991906115a5565b61078c565b005b61022a600480360381019061022591906111d0565b61086b565b60405161023791906111b5565b60405180910390f35b61025a6004803603810190610255919061116d565b6108b0565b604051610269939291906116af565b60405180910390f35b61027a6109f7565b6040516102879190611365565b60405180910390f35b6102aa60048036038101906102a5919061116d565b610a1d565b6040516102b89291906116f4565b60405180910390f35b6102db60048036038101906102d6919061171d565b610a4e565b6040516102e891906111b5565b60405180910390f35b61030b6004803603810190610306919061116d565b610a93565b60405161031891906111b5565b60405180910390f35b61033b600480360381019061033691906111d0565b610b1a565b60405161034891906111b5565b60405180910390f35b61036b600480360381019061036691906110e2565b610c33565b005b80516000805490501461037f57600080fd5b60005b815181101561043f576000818154811061039f5761039e611779565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1661040c6103ec85610d9b565b8484815181106103ff576103fe611779565b5b6020026020010151610dcb565b73ffffffffffffffffffffffffffffffffffffffff161461042c57600080fd5b8080610437906117d7565b915050610382565b506001600384604051610452919061185b565b9081526020016040518091039020600084815260200190815260200160002060006101000a81548160ff021916908315150217905550600160046000848152602001908152602001600020846040516104ab919061185b565b908152602001604051809103902060006101000a81548160ff021916908315150217905550505050565b60006002600083815260200190815260200160002060020160009054906101000a900460ff169050919050565b6000600383604051610514919061185b565b9081526020016040518091039020600083815260200190815260200160002060009054906101000a900460ff16905092915050565b6005602052816000526040600020818154811061056557600080fd5b9060005260206000209060030201600091509150508060000154908060010154908060020160009054906101000a900460ff16905083565b600081815481106105ad57600080fd5b906000526020600020016000915054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b3373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff161461063657600080fd5b805182511461064457600080fd5b604051806040016040528084815260200160011515815250600660008681526020019081526020016000206000820151816000015560208201518160010160006101000a81548160ff02191690831515021790555090505060008251905060005b81811015610784576005600086815260200190815260200160002060405180606001604052808684815181106106de576106dd611779565b5b602002602001015181526020018584815181106106fe576106fd611779565b5b60200260200101518152602001600115158152509080600181540180825580915050600190039060005260206000209060030201600090919091909150600082015181600001556020820151816001015560408201518160020160006101000a81548160ff0219169083151502179055505050808061077c906117d7565b9150506106a5565b505050505050565b3373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16146107e657600080fd5b60405180606001604052808381526020018281526020016001151581525060026000858152602001908152602001600020600082015181600001908161082c9190611a7e565b5060208201518160010190816108429190611a7e565b5060408201518160020160006101000a81548160ff021916908315150217905550905050505050565b6003828051602081018201805184825260208301602085012081835280955050505050506020528060005260406000206000915091509054906101000a900460ff1681565b60026020528060005260406000206000915090508060000180546108d3906118a1565b80601f01602080910402602001604051908101604052809291908181526020018280546108ff906118a1565b801561094c5780601f106109215761010080835404028352916020019161094c565b820191906000526020600020905b81548152906001019060200180831161092f57829003601f168201915b505050505090806001018054610961906118a1565b80601f016020809104026020016040519081016040528092919081815260200182805461098d906118a1565b80156109da5780601f106109af576101008083540402835291602001916109da565b820191906000526020600020905b8154815290600101906020018083116109bd57829003601f168201915b5050505050908060020160009054906101000a900460ff16905083565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60066020528060005260406000206000915090508060000154908060010160009054906101000a900460ff16905082565b6004602052816000526040600020818051602081018201805184825260208301602085012081835280955050505050506000915091509054906101000a900460ff1681565b60003373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1614610aef57600080fd5b6006600083815260200190815260200160002060010160009054906101000a900460ff169050919050565b6000806005600084815260200190815260200160002080549050905060008111610b4357600080fd5b60005b81811015610c265760046000600560008781526020019081526020016000208381548110610b7757610b76611779565b5b906000526020600020906003020160000154815260200190815260200160002085604051610ba5919061185b565b908152602001604051809103902060009054906101000a900460ff168015610c03575042600560008681526020019081526020016000208281548110610bee57610bed611779565b5b90600052602060002090600302016001015410155b15610c1357600192505050610c2d565b8080610c1e906117d7565b915050610b46565b5060009150505b92915050565b805160008054905014610c4557600080fd5b60005b8151811015610d055760008181548110610c6557610c64611779565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16610cd2610cb285610d9b565b848481518110610cc557610cc4611779565b5b6020026020010151610dcb565b73ffffffffffffffffffffffffffffffffffffffff1614610cf257600080fd5b8080610cfd906117d7565b915050610c48565b506001600384604051610d18919061185b565b9081526020016040518091039020600084815260200190815260200160002060006101000a81548160ff02191690831515021790555060016004600084815260200190815260200160002084604051610d71919061185b565b908152602001604051809103902060006101000a81548160ff021916908315150217905550505050565b600081604051602001610dae9190611bc8565b604051602081830303815290604052805190602001209050919050565b600080600080610dda85610e3a565b92509250925060018684848460405160008152602001604052604051610e039493929190611c0a565b6020604051602081039080840390855afa158015610e25573d6000803e3d6000fd5b50505060206040510351935050505092915050565b60008060006041845114610e4d57600080fd5b6020840151915060408401519050606084015160001a92509193909250565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b610ed382610e8a565b810181811067ffffffffffffffff82111715610ef257610ef1610e9b565b5b80604052505050565b6000610f05610e6c565b9050610f118282610eca565b919050565b600067ffffffffffffffff821115610f3157610f30610e9b565b5b610f3a82610e8a565b9050602081019050919050565b82818337600083830152505050565b6000610f69610f6484610f16565b610efb565b905082815260208101848484011115610f8557610f84610e85565b5b610f90848285610f47565b509392505050565b600082601f830112610fad57610fac610e80565b5b8135610fbd848260208601610f56565b91505092915050565b6000819050919050565b610fd981610fc6565b8114610fe457600080fd5b50565b600081359050610ff681610fd0565b92915050565b600067ffffffffffffffff82111561101757611016610e9b565b5b602082029050602081019050919050565b600080fd5b600061104061103b84610ffc565b610efb565b9050808382526020820190506020840283018581111561106357611062611028565b5b835b818110156110aa57803567ffffffffffffffff81111561108857611087610e80565b5b8086016110958982610f98565b85526020850194505050602081019050611065565b5050509392505050565b600082601f8301126110c9576110c8610e80565b5b81356110d984826020860161102d565b91505092915050565b6000806000606084860312156110fb576110fa610e76565b5b600084013567ffffffffffffffff81111561111957611118610e7b565b5b61112586828701610f98565b935050602061113686828701610fe7565b925050604084013567ffffffffffffffff81111561115757611156610e7b565b5b611163868287016110b4565b9150509250925092565b60006020828403121561118357611182610e76565b5b600061119184828501610fe7565b91505092915050565b60008115159050919050565b6111af8161119a565b82525050565b60006020820190506111ca60008301846111a6565b92915050565b600080604083850312156111e7576111e6610e76565b5b600083013567ffffffffffffffff81111561120557611204610e7b565b5b61121185828601610f98565b925050602061122285828601610fe7565b9150509250929050565b6000819050919050565b61123f8161122c565b811461124a57600080fd5b50565b60008135905061125c81611236565b92915050565b6000806040838503121561127957611278610e76565b5b600061128785828601610fe7565b92505060206112988582860161124d565b9150509250929050565b6112ab81610fc6565b82525050565b6112ba8161122c565b82525050565b60006060820190506112d560008301866112a2565b6112e260208301856112b1565b6112ef60408301846111a6565b949350505050565b60006020828403121561130d5761130c610e76565b5b600061131b8482850161124d565b91505092915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b600061134f82611324565b9050919050565b61135f81611344565b82525050565b600060208201905061137a6000830184611356565b92915050565b600067ffffffffffffffff82111561139b5761139a610e9b565b5b602082029050602081019050919050565b60006113bf6113ba84611380565b610efb565b905080838252602082019050602084028301858111156113e2576113e1611028565b5b835b8181101561140b57806113f78882610fe7565b8452602084019350506020810190506113e4565b5050509392505050565b600082601f83011261142a57611429610e80565b5b813561143a8482602086016113ac565b91505092915050565b600067ffffffffffffffff82111561145e5761145d610e9b565b5b602082029050602081019050919050565b600061148261147d84611443565b610efb565b905080838252602082019050602084028301858111156114a5576114a4611028565b5b835b818110156114ce57806114ba888261124d565b8452602084019350506020810190506114a7565b5050509392505050565b600082601f8301126114ed576114ec610e80565b5b81356114fd84826020860161146f565b91505092915050565b600080600080608085870312156115205761151f610e76565b5b600061152e87828801610fe7565b945050602061153f87828801610fe7565b935050604085013567ffffffffffffffff8111156115605761155f610e7b565b5b61156c87828801611415565b925050606085013567ffffffffffffffff81111561158d5761158c610e7b565b5b611599878288016114d8565b91505092959194509250565b6000806000606084860312156115be576115bd610e76565b5b60006115cc86828701610fe7565b935050602084013567ffffffffffffffff8111156115ed576115ec610e7b565b5b6115f986828701610f98565b925050604084013567ffffffffffffffff81111561161a57611619610e7b565b5b61162686828701610f98565b9150509250925092565b600081519050919050565b600082825260208201905092915050565b60005b8381101561166a57808201518184015260208101905061164f565b60008484015250505050565b600061168182611630565b61168b818561163b565b935061169b81856020860161164c565b6116a481610e8a565b840191505092915050565b600060608201905081810360008301526116c98186611676565b905081810360208301526116dd8185611676565b90506116ec60408301846111a6565b949350505050565b600060408201905061170960008301856112a2565b61171660208301846111a6565b9392505050565b6000806040838503121561173457611733610e76565b5b600061174285828601610fe7565b925050602083013567ffffffffffffffff81111561176357611762610e7b565b5b61176f85828601610f98565b9150509250929050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b60006117e28261122c565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff8203611814576118136117a8565b5b600182019050919050565b600081905092915050565b600061183582611630565b61183f818561181f565b935061184f81856020860161164c565b80840191505092915050565b6000611867828461182a565b915081905092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b600060028204905060018216806118b957607f821691505b6020821081036118cc576118cb611872565b5b50919050565b60008190508160005260206000209050919050565b60006020601f8301049050919050565b600082821b905092915050565b6000600883026119347fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff826118f7565b61193e86836118f7565b95508019841693508086168417925050509392505050565b6000819050919050565b600061197b6119766119718461122c565b611956565b61122c565b9050919050565b6000819050919050565b61199583611960565b6119a96119a182611982565b848454611904565b825550505050565b600090565b6119be6119b1565b6119c981848461198c565b505050565b5b818110156119ed576119e26000826119b6565b6001810190506119cf565b5050565b601f821115611a3257611a03816118d2565b611a0c846118e7565b81016020851015611a1b578190505b611a2f611a27856118e7565b8301826119ce565b50505b505050565b600082821c905092915050565b6000611a5560001984600802611a37565b1980831691505092915050565b6000611a6e8383611a44565b9150826002028217905092915050565b611a8782611630565b67ffffffffffffffff811115611aa057611a9f610e9b565b5b611aaa82546118a1565b611ab58282856119f1565b600060209050601f831160018114611ae85760008415611ad6578287015190505b611ae08582611a62565b865550611b48565b601f198416611af6866118d2565b60005b82811015611b1e57848901518255600182019150602085019450602081019050611af9565b86831015611b3b5784890151611b37601f891682611a44565b8355505b6001600288020188555050505b505050505050565b600081905092915050565b7f19457468657265756d205369676e6564204d6573736167653a0a333200000000600082015250565b6000611b91601c83611b50565b9150611b9c82611b5b565b601c82019050919050565b6000819050919050565b611bc2611bbd82610fc6565b611ba7565b82525050565b6000611bd382611b84565b9150611bdf8284611bb1565b60208201915081905092915050565b600060ff82169050919050565b611c0481611bee565b82525050565b6000608082019050611c1f60008301876112a2565b611c2c6020830186611bfb565b611c3960408301856112a2565b611c4660608301846112a2565b9594505050505056fea2646970667358221220af772f8f628a31b95d5465394487798bab0bfb8f8e9598e9ec648b4c37fef45464736f6c6343000811003300000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000003000000000000000000000000e252d01a37b85e2007ed3cc13797aa92496204a40000000000000000000000005f15294a1918633d4dd4ec47098a14d01c58e957000000000000000000000000d45cfd6855c6ec8f635a6f2b46c647e99c59c79d");
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
