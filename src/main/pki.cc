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
    parser_arg.Get("i", type);
    std::cout << "type: " << type << std::endl;
    PkiIbAgka protocol(kTypeA);
    protocol.Simulate();
    return 0;
}
