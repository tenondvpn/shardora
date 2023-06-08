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
    std::string bytescode = common::Encode::HexDecode("60806040523480156200001157600080fd5b506040516200205b3803806200205b833981810160405281019062000037919062000334565b80600090805190602001906200004f92919062000098565b5033600160006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff1602179055505062000385565b82805482825590600052602060002090810192821562000114579160200282015b82811115620001135782518260006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555091602001919060010190620000b9565b5b50905062000123919062000127565b5090565b5b808211156200014257600081600090555060010162000128565b5090565b6000604051905090565b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b620001aa826200015f565b810181811067ffffffffffffffff82111715620001cc57620001cb62000170565b5b80604052505050565b6000620001e162000146565b9050620001ef82826200019f565b919050565b600067ffffffffffffffff82111562000212576200021162000170565b5b602082029050602081019050919050565b600080fd5b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b6000620002558262000228565b9050919050565b620002678162000248565b81146200027357600080fd5b50565b60008151905062000287816200025c565b92915050565b6000620002a46200029e84620001f4565b620001d5565b90508083825260208201905060208402830185811115620002ca57620002c962000223565b5b835b81811015620002f75780620002e2888262000276565b845260208401935050602081019050620002cc565b5050509392505050565b600082601f8301126200031957620003186200015a565b5b81516200032b8482602086016200028d565b91505092915050565b6000602082840312156200034d576200034c62000150565b5b600082015167ffffffffffffffff8111156200036e576200036d62000155565b5b6200037c8482850162000301565b91505092915050565b611cc680620003956000396000f3fe608060405234801561001057600080fd5b50600436106100ea5760003560e01c80637e2803781161008c578063ae0ad9d911610066578063ae0ad9d914610286578063b89e70b5146102b6578063e9c7feda146102e6578063fdba8f1114610316576100ea565b80637e280378146102055780638da5cb5b14610237578063a51f8aac14610255576100ea565b80635f13d6ee116100c85780635f13d6ee1461016d5780636189d2df1461019d578063691b3463146101b95780636b83e059146101d5576100ea565b806308ad872a146100ef57806329c1cb4b1461010b5780634819544b1461013b575b600080fd5b61010960048036038101906101049190611123565b610332565b005b610125600480360381019061012091906111ae565b61049a565b6040516101329190611225565b60405180910390f35b61015560048036038101906101509190611276565b6104e1565b604051610164939291906112d4565b60405180910390f35b6101876004803603810190610182919061130b565b610535565b6040516101949190611379565b60405180910390f35b6101b760048036038101906101b2919061151a565b610574565b005b6101d360048036038101906101ce91906115b9565b610752565b005b6101ef60048036038101906101ea91906111ae565b61085f565b6040516101fc9190611225565b60405180910390f35b61021f600480360381019061021a9190611644565b6108a4565b60405161022e939291906116f0565b60405180910390f35b61023f6109eb565b60405161024c9190611379565b60405180910390f35b61026f600480360381019061026a9190611644565b610a11565b60405161027d929190611735565b60405180910390f35b6102a0600480360381019061029b919061175e565b610a42565b6040516102ad9190611225565b60405180910390f35b6102d060048036038101906102cb9190611644565b610a87565b6040516102dd9190611225565b60405180910390f35b61030060048036038101906102fb91906111ae565b610b0e565b60405161030d9190611225565b60405180910390f35b610330600480360381019061032b9190611123565b610c27565b005b80516000805490501461034457600080fd5b60005b81518110156104045760008181548110610364576103636117ba565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166103d16103b185610ddc565b8484815181106103c4576103c36117ba565b5b6020026020010151610e0c565b73ffffffffffffffffffffffffffffffffffffffff16146103f157600080fd5b80806103fc90611818565b915050610347565b506001600384604051610417919061189c565b9081526020016040518091039020600084815260200190815260200160002060006101000a81548160ff02191690831515021790555060016004600084815260200190815260200160002084604051610470919061189c565b908152602001604051809103902060006101000a81548160ff021916908315150217905550505050565b60006003836040516104ac919061189c565b9081526020016040518091039020600083815260200190815260200160002060009054906101000a900460ff16905092915050565b600560205281600052604060002081815481106104fd57600080fd5b9060005260206000209060030201600091509150508060000154908060010154908060020160009054906101000a900460ff16905083565b6000818154811061054557600080fd5b906000526020600020016000915054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b3373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16146105ce57600080fd5b6006600085815260200190815260200160002060010160009054906101000a900460ff16156105fc57600080fd5b805182511461060a57600080fd5b604051806040016040528084815260200160011515815250600660008681526020019081526020016000206000820151816000015560208201518160010160006101000a81548160ff02191690831515021790555090505060008251905060005b8181101561074a576005600086815260200190815260200160002060405180606001604052808684815181106106a4576106a36117ba565b5b602002602001015181526020018584815181106106c4576106c36117ba565b5b60200260200101518152602001600115158152509080600181540180825580915050600190039060005260206000209060030201600090919091909150600082015181600001556020820151816001015560408201518160020160006101000a81548160ff0219169083151502179055505050808061074290611818565b91505061066b565b505050505050565b3373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16146107ac57600080fd5b6002600084815260200190815260200160002060020160009054906101000a900460ff16156107da57600080fd5b6040518060600160405280838152602001828152602001600115158152506002600085815260200190815260200160002060008201518160000190816108209190611abf565b5060208201518160010190816108369190611abf565b5060408201518160020160006101000a81548160ff021916908315150217905550905050505050565b6003828051602081018201805184825260208301602085012081835280955050505050506020528060005260406000206000915091509054906101000a900460ff1681565b60026020528060005260406000206000915090508060000180546108c7906118e2565b80601f01602080910402602001604051908101604052809291908181526020018280546108f3906118e2565b80156109405780601f1061091557610100808354040283529160200191610940565b820191906000526020600020905b81548152906001019060200180831161092357829003601f168201915b505050505090806001018054610955906118e2565b80601f0160208091040260200160405190810160405280929190818152602001828054610981906118e2565b80156109ce5780601f106109a3576101008083540402835291602001916109ce565b820191906000526020600020905b8154815290600101906020018083116109b157829003601f168201915b5050505050908060020160009054906101000a900460ff16905083565b600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60066020528060005260406000206000915090508060000154908060010160009054906101000a900460ff16905082565b6004602052816000526040600020818051602081018201805184825260208301602085012081835280955050505050506000915091509054906101000a900460ff1681565b60003373ffffffffffffffffffffffffffffffffffffffff16600160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff1614610ae357600080fd5b6006600083815260200190815260200160002060010160009054906101000a900460ff169050919050565b6000806005600084815260200190815260200160002080549050905060008111610b3757600080fd5b60005b81811015610c1a5760046000600560008781526020019081526020016000208381548110610b6b57610b6a6117ba565b5b906000526020600020906003020160000154815260200190815260200160002085604051610b99919061189c565b908152602001604051809103902060009054906101000a900460ff168015610bf7575042600560008681526020019081526020016000208281548110610be257610be16117ba565b5b90600052602060002090600302016001015410155b15610c0757600192505050610c21565b8080610c1290611818565b915050610b3a565b5060009150505b92915050565b805160008054905014610c3957600080fd5b60001515600384604051610c4d919061189c565b9081526020016040518091039020600084815260200190815260200160002060009054906101000a900460ff16151514610c8657600080fd5b60005b8151811015610d465760008181548110610ca657610ca56117ba565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16610d13610cf385610ddc565b848481518110610d0657610d056117ba565b5b6020026020010151610e0c565b73ffffffffffffffffffffffffffffffffffffffff1614610d3357600080fd5b8080610d3e90611818565b915050610c89565b506001600384604051610d59919061189c565b9081526020016040518091039020600084815260200190815260200160002060006101000a81548160ff02191690831515021790555060016004600084815260200190815260200160002084604051610db2919061189c565b908152602001604051809103902060006101000a81548160ff021916908315150217905550505050565b600081604051602001610def9190611c09565b604051602081830303815290604052805190602001209050919050565b600080600080610e1b85610e7b565b92509250925060018684848460405160008152602001604052604051610e449493929190611c4b565b6020604051602081039080840390855afa158015610e66573d6000803e3d6000fd5b50505060206040510351935050505092915050565b60008060006041845114610e8e57600080fd5b6020840151915060408401519050606084015160001a92509193909250565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b610f1482610ecb565b810181811067ffffffffffffffff82111715610f3357610f32610edc565b5b80604052505050565b6000610f46610ead565b9050610f528282610f0b565b919050565b600067ffffffffffffffff821115610f7257610f71610edc565b5b610f7b82610ecb565b9050602081019050919050565b82818337600083830152505050565b6000610faa610fa584610f57565b610f3c565b905082815260208101848484011115610fc657610fc5610ec6565b5b610fd1848285610f88565b509392505050565b600082601f830112610fee57610fed610ec1565b5b8135610ffe848260208601610f97565b91505092915050565b6000819050919050565b61101a81611007565b811461102557600080fd5b50565b60008135905061103781611011565b92915050565b600067ffffffffffffffff82111561105857611057610edc565b5b602082029050602081019050919050565b600080fd5b600061108161107c8461103d565b610f3c565b905080838252602082019050602084028301858111156110a4576110a3611069565b5b835b818110156110eb57803567ffffffffffffffff8111156110c9576110c8610ec1565b5b8086016110d68982610fd9565b855260208501945050506020810190506110a6565b5050509392505050565b600082601f83011261110a57611109610ec1565b5b813561111a84826020860161106e565b91505092915050565b60008060006060848603121561113c5761113b610eb7565b5b600084013567ffffffffffffffff81111561115a57611159610ebc565b5b61116686828701610fd9565b935050602061117786828701611028565b925050604084013567ffffffffffffffff81111561119857611197610ebc565b5b6111a4868287016110f5565b9150509250925092565b600080604083850312156111c5576111c4610eb7565b5b600083013567ffffffffffffffff8111156111e3576111e2610ebc565b5b6111ef85828601610fd9565b925050602061120085828601611028565b9150509250929050565b60008115159050919050565b61121f8161120a565b82525050565b600060208201905061123a6000830184611216565b92915050565b6000819050919050565b61125381611240565b811461125e57600080fd5b50565b6000813590506112708161124a565b92915050565b6000806040838503121561128d5761128c610eb7565b5b600061129b85828601611028565b92505060206112ac85828601611261565b9150509250929050565b6112bf81611007565b82525050565b6112ce81611240565b82525050565b60006060820190506112e960008301866112b6565b6112f660208301856112c5565b6113036040830184611216565b949350505050565b60006020828403121561132157611320610eb7565b5b600061132f84828501611261565b91505092915050565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b600061136382611338565b9050919050565b61137381611358565b82525050565b600060208201905061138e600083018461136a565b92915050565b600067ffffffffffffffff8211156113af576113ae610edc565b5b602082029050602081019050919050565b60006113d36113ce84611394565b610f3c565b905080838252602082019050602084028301858111156113f6576113f5611069565b5b835b8181101561141f578061140b8882611028565b8452602084019350506020810190506113f8565b5050509392505050565b600082601f83011261143e5761143d610ec1565b5b813561144e8482602086016113c0565b91505092915050565b600067ffffffffffffffff82111561147257611471610edc565b5b602082029050602081019050919050565b600061149661149184611457565b610f3c565b905080838252602082019050602084028301858111156114b9576114b8611069565b5b835b818110156114e257806114ce8882611261565b8452602084019350506020810190506114bb565b5050509392505050565b600082601f83011261150157611500610ec1565b5b8135611511848260208601611483565b91505092915050565b6000806000806080858703121561153457611533610eb7565b5b600061154287828801611028565b945050602061155387828801611028565b935050604085013567ffffffffffffffff81111561157457611573610ebc565b5b61158087828801611429565b925050606085013567ffffffffffffffff8111156115a1576115a0610ebc565b5b6115ad878288016114ec565b91505092959194509250565b6000806000606084860312156115d2576115d1610eb7565b5b60006115e086828701611028565b935050602084013567ffffffffffffffff81111561160157611600610ebc565b5b61160d86828701610fd9565b925050604084013567ffffffffffffffff81111561162e5761162d610ebc565b5b61163a86828701610fd9565b9150509250925092565b60006020828403121561165a57611659610eb7565b5b600061166884828501611028565b91505092915050565b600081519050919050565b600082825260208201905092915050565b60005b838110156116ab578082015181840152602081019050611690565b60008484015250505050565b60006116c282611671565b6116cc818561167c565b93506116dc81856020860161168d565b6116e581610ecb565b840191505092915050565b6000606082019050818103600083015261170a81866116b7565b9050818103602083015261171e81856116b7565b905061172d6040830184611216565b949350505050565b600060408201905061174a60008301856112b6565b6117576020830184611216565b9392505050565b6000806040838503121561177557611774610eb7565b5b600061178385828601611028565b925050602083013567ffffffffffffffff8111156117a4576117a3610ebc565b5b6117b085828601610fd9565b9150509250929050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b600061182382611240565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff8203611855576118546117e9565b5b600182019050919050565b600081905092915050565b600061187682611671565b6118808185611860565b935061189081856020860161168d565b80840191505092915050565b60006118a8828461186b565b915081905092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052602260045260246000fd5b600060028204905060018216806118fa57607f821691505b60208210810361190d5761190c6118b3565b5b50919050565b60008190508160005260206000209050919050565b60006020601f8301049050919050565b600082821b905092915050565b6000600883026119757fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff82611938565b61197f8683611938565b95508019841693508086168417925050509392505050565b6000819050919050565b60006119bc6119b76119b284611240565b611997565b611240565b9050919050565b6000819050919050565b6119d6836119a1565b6119ea6119e2826119c3565b848454611945565b825550505050565b600090565b6119ff6119f2565b611a0a8184846119cd565b505050565b5b81811015611a2e57611a236000826119f7565b600181019050611a10565b5050565b601f821115611a7357611a4481611913565b611a4d84611928565b81016020851015611a5c578190505b611a70611a6885611928565b830182611a0f565b50505b505050565b600082821c905092915050565b6000611a9660001984600802611a78565b1980831691505092915050565b6000611aaf8383611a85565b9150826002028217905092915050565b611ac882611671565b67ffffffffffffffff811115611ae157611ae0610edc565b5b611aeb82546118e2565b611af6828285611a32565b600060209050601f831160018114611b295760008415611b17578287015190505b611b218582611aa3565b865550611b89565b601f198416611b3786611913565b60005b82811015611b5f57848901518255600182019150602085019450602081019050611b3a565b86831015611b7c5784890151611b78601f891682611a85565b8355505b6001600288020188555050505b505050505050565b600081905092915050565b7f19457468657265756d205369676e6564204d6573736167653a0a333200000000600082015250565b6000611bd2601c83611b91565b9150611bdd82611b9c565b601c82019050919050565b6000819050919050565b611c03611bfe82611007565b611be8565b82525050565b6000611c1482611bc5565b9150611c208284611bf2565b60208201915081905092915050565b600060ff82169050919050565b611c4581611c2f565b82525050565b6000608082019050611c6060008301876112b6565b611c6d6020830186611c3c565b611c7a60408301856112b6565b611c8760608301846112b6565b9594505050505056fea264697066735822122042eae2a3fa679efeebf80ec802b15c8df8fe811c3a2cd518d39a71cffa56a9d264736f6c6343000811003300000000000000000000000000000000000000000000000000000000000000200000000000000000000000000000000000000000000000000000000000000003000000000000000000000000e252d01a37b85e2007ed3cc13797aa92496204a40000000000000000000000005f15294a1918633d4dd4ec47098a14d01c58e957000000000000000000000000d45cfd6855c6ec8f635a6f2b46c647e99c59c79d");
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
