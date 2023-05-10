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
            new_tx->set_step(pools::protobuf::kContractUserCreateCall);
            new_tx->set_contract_code(val);
            new_tx->set_contract_prepayment(9000000000lu);
        } else if (key == "prepayment") {
            new_tx->set_step(pools::protobuf::kContractUserCall);
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

        std::cout << "from private key: " << common::Encode::HexEncode(from_prikey) << ", to: " << common::Encode::HexEncode(to) << std::endl;
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
    std::string bytescode = common::Encode::HexDecode("6101be610053600b82828239805160001a607314610046577f4e487b7100000000000000000000000000000000000000000000000000000000600052600060045260246000fd5b30600052607381538281f3fe73000000000000000000000000000000000000000030146080604052600436106100355760003560e01c8063771602f71461003a575b600080fd5b610054600480360381019061004f91906100bb565b61006a565b604051610061919061010a565b60405180910390f35b600081836100789190610154565b905092915050565b600080fd5b6000819050919050565b61009881610085565b81146100a357600080fd5b50565b6000813590506100b58161008f565b92915050565b600080604083850312156100d2576100d1610080565b5b60006100e0858286016100a6565b92505060206100f1858286016100a6565b9150509250929050565b61010481610085565b82525050565b600060208201905061011f60008301846100fb565b92915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b600061015f82610085565b915061016a83610085565b925082820190508082111561018257610181610125565b5b9291505056fea26469706673582212209a306fb9cc8dd120671cd2a85a5321b243260137113c947729c018ae009993da64736f6c63430008110033");
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
    std::string bytescode = common::Encode::HexDecode("6080604052604051610817380380610817833981810160405281019061002591906100b6565b816001819055508060028190555033600360006101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555050506100f6565b600080fd5b6000819050919050565b61009381610080565b811461009e57600080fd5b50565b6000815190506100b08161008a565b92915050565b600080604083850312156100cd576100cc61007b565b5b60006100db858286016100a1565b92505060206100ec858286016100a1565b9150509250929050565b610712806101056000396000f3fe6080604052600436106100345760003560e01c806302d05d3f146100395780634162d68f14610064578063d1e94e5714610080575b600080fd5b34801561004557600080fd5b5061004e6100bd565b60405161005b9190610344565b60405180910390f35b61007e600480360381019061007991906104b9565b6100e3565b005b34801561008c57600080fd5b506100a760048036038101906100a2919061056e565b6102b5565b6040516100b49190610344565b60405180910390f35b600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b60006003826040516100f5919061061f565b602060405180830381855afa158015610112573d6000803e3d6000fd5b5050506040515160601b6bffffffffffffffffffffffff19169050600080828152602001908152602001600020339080600181540180825580915050600190039060005260206000200160009091909190916101000a81548173ffffffffffffffffffffffffffffffffffffffff021916908373ffffffffffffffffffffffffffffffffffffffff16021790555060015460008083815260200190815260200160002080549050106102b15760005b6001548110156102755760008083815260200190815260200160002081815481106101ef576101ee610636565b5b9060005260206000200160009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff166108fc6002549081150290604051600060405180830381858888f19350505050158015610261573d6000803e3d6000fd5b50808061026d90610694565b9150506101c1565b50600360009054906101000a900473ffffffffffffffffffffffffffffffffffffffff1673ffffffffffffffffffffffffffffffffffffffff16ff5b5050565b600060205281600052604060002081815481106102d157600080fd5b906000526020600020016000915091509054906101000a900473ffffffffffffffffffffffffffffffffffffffff1681565b600073ffffffffffffffffffffffffffffffffffffffff82169050919050565b600061032e82610303565b9050919050565b61033e81610323565b82525050565b60006020820190506103596000830184610335565b92915050565b6000604051905090565b600080fd5b600080fd5b600080fd5b600080fd5b6000601f19601f8301169050919050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052604160045260246000fd5b6103c68261037d565b810181811067ffffffffffffffff821117156103e5576103e461038e565b5b80604052505050565b60006103f861035f565b905061040482826103bd565b919050565b600067ffffffffffffffff8211156104245761042361038e565b5b61042d8261037d565b9050602081019050919050565b82818337600083830152505050565b600061045c61045784610409565b6103ee565b90508281526020810184848401111561047857610477610378565b5b61048384828561043a565b509392505050565b600082601f8301126104a05761049f610373565b5b81356104b0848260208601610449565b91505092915050565b6000602082840312156104cf576104ce610369565b5b600082013567ffffffffffffffff8111156104ed576104ec61036e565b5b6104f98482850161048b565b91505092915050565b6000819050919050565b61051581610502565b811461052057600080fd5b50565b6000813590506105328161050c565b92915050565b6000819050919050565b61054b81610538565b811461055657600080fd5b50565b60008135905061056881610542565b92915050565b6000806040838503121561058557610584610369565b5b600061059385828601610523565b92505060206105a485828601610559565b9150509250929050565b600081519050919050565b600081905092915050565b60005b838110156105e25780820151818401526020810190506105c7565b60008484015250505050565b60006105f9826105ae565b61060381856105b9565b93506106138185602086016105c4565b80840191505092915050565b600061062b82846105ee565b915081905092915050565b7f4e487b7100000000000000000000000000000000000000000000000000000000600052603260045260246000fd5b7f4e487b7100000000000000000000000000000000000000000000000000000000600052601160045260246000fd5b600061069f82610538565b91507fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff82036106d1576106d0610665565b5b60018201905091905056fea2646970667358221220d11b498d34c527944b8940125c731cb3ac6c9e8c120b6f0e12763b1914a8615a64736f6c6343000811003300000000000000000000000000000000000000000000000000000000000000030000000000000000000000000000000000000000000000000000000005f5e100");
    if (argc > 2) {
        std::string library_addr = argv[2];
        bytescode = std::string("608060405234801561001057600080fd5b5061023b806100206000396000f3fe608060405234801561001057600080fd5b506004361061002b5760003560e01c80638e86b12514610030575b600080fd5b61004a60048036038101906100459190610121565b610060565b6040516100579190610170565b60405180910390f35b60008273") +
            library_addr +
            "63771602f79091846040518363ffffffff1660e01b815260040161009d92919061019a565b602060405180830381865af41580156100ba573d6000803e3d6000fd5b505050506040513d601f19601f820116820180604052508101906100de91906101d8565b905092915050565b600080fd5b6000819050919050565b6100fe816100eb565b811461010957600080fd5b50565b60008135905061011b816100f5565b92915050565b60008060408385031215610138576101376100e6565b5b60006101468582860161010c565b92505060206101578582860161010c565b9150509250929050565b61016a816100eb565b82525050565b60006020820190506101856000830184610161565b92915050565b610194816100eb565b82525050565b60006040820190506101af600083018561018b565b6101bc602083018461018b565b9392505050565b6000815190506101d2816100f5565b92915050565b6000602082840312156101ee576101ed6100e6565b5b60006101fc848285016101c3565b9150509291505056fea2646970667358221220074683d195c9f57f63628278e7777c37f9554ee2e066edbc327a403687aeef0964736f6c63430008110033";
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
