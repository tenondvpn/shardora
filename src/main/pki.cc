#include <iostream>
#include <queue>
#include <vector>

#include "contract/contract_cpabe.h"
#include "common/parse_args.h"
#include "pki/param.h"
#include "pki/pki_ib_agka.h"

using namespace shardora;

int test_pki(int argc, char** argv) {
    common::ParserArgs parser_arg;
    parser_arg.AddArgType('h', "help", common::kNoValue);
    parser_arg.AddArgType('t', "type", common::kMaybeValue);
    std::string tmp_params = "";
    for (int i = 1; i < argc; i++) {
        if (strlen(argv[i]) == 0) {
            tmp_params += static_cast<char>(31);
        }
        else {
            tmp_params += argv[i];
        }
        tmp_params += " ";
    }

    std::string err_pos;
    if (parser_arg.Parse(tmp_params, err_pos) != common::kParseSuccess) {
        printf("parse params failed!\n");
        return 1;
    }

    int type;
    parser_arg.Get("t", type);
    std::cout << "type: " << type << std::endl;
    pki::PkiIbAgka protocol(
        pki::kTypeA, 
        "2f8175e95fb7fe128355fce11b1e6a2e4633c284", 
        "7fb3c66433c155c9258475362a92beca9827075a753980e9ff9dde68eea6195676246529939cd086ece99a902dc16ecb275c3b20e6be0b00e470d4dd012a5acd9fd7d4df606f7f3525bb13affe4036e7196366c8c047a73036f68354cd1c611e4eeda601b93abb68888f2f2191f01216c984aaa86b4ade36b84d7bdfca4ffcf2");
    protocol.Simulate();
    return 0;
}

int test_cpabe(int argc, char** argv) {
    common::ParserArgs parser_arg;
    parser_arg.AddArgType('t', "type", common::kMaybeValue);
    parser_arg.AddArgType('d', "des_file", common::kMaybeValue);
    parser_arg.AddArgType('p', "public key", common::kMaybeValue);
    parser_arg.AddArgType('o', "policy", common::kMaybeValue);
    parser_arg.AddArgType('a', "plain text", common::kMaybeValue);
    parser_arg.AddArgType('c', "cipher text", common::kMaybeValue);
    std::string tmp_params = "";
    for (int i = 1; i < argc; i++) {
        if (strlen(argv[i]) == 0) {
            tmp_params += static_cast<char>(31);
        }
        else {
            tmp_params += argv[i];
        }
        tmp_params += " ";
    }

    std::string err_pos;
    if (parser_arg.Parse(tmp_params, err_pos) != common::kParseSuccess) {
        printf("parse params failed: %s\n", tmp_params.c_str());
        return 1;
    }

    std::string des_file;
    parser_arg.Get("d", des_file);
    int type = 0;
    parser_arg.Get("t", type);
    std::cout << "des file: " << des_file << ", type: " << type << std::endl;

    contract::ContractCpabe cpabe;
    if (type == 0) {
        cpabe.test_cpabe(des_file);
    }

    if (type == 1) {
        cpabe.generate_private_and_public_key(des_file);
    }

    if (type == 2) {
        std::string pulic_key;
        parser_arg.Get("p", pulic_key);
        std::string policy;
        parser_arg.Get("o", policy);
        std::string plain_text;
        parser_arg.Get("a", plain_text);
        cpabe.encrypt(des_file, pulic_key, policy, plain_text);
    }

    if (type == 3) {
        std::string cipher_text;
        parser_arg.Get("c", cipher_text);
        cpabe.decrypt(des_file, cipher_text);
    }
    
    return 0;
}

int main(int argc, char** argv) {
    // test_pki(argc, argv);
    test_cpabe(argc, argv);
    return 0;
}
