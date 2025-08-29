#include <iostream>
#include <queue>
#include <vector>

#include "common/parse_args.h"
#include "pki/param.h"
#include "pki/pki_ib_agka.h"

using namespace shardora;

int main(int argc, char** argv) {
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
