#include "init/command.h"

#include <iostream>
#include <common/log.h>
#include <string>
#include <stdio.h>
#include <termios.h> 
#include <unistd.h>

#include <iostream>
#include <memory>
#include <thread>

#include "common/split.h"
#include "common/string_utils.h"
#include "common/encode.h"
#include "common/global_info.h"
#include "common/country_code.h"
#include "common/time_utils.h"
#include "common/shell_utils.h"
#include "dht/base_dht.h"
#include "dht/dht_key.h"
#include "network/dht_manager.h"
#include "network/universal_manager.h"
#include "network/route.h"
#include "network/network_utils.h"
#include "init/init_utils.h"

namespace zjchain {

namespace init {

Command::Command() {}

Command::~Command() {
    destroy_ = true;
}

bool Command::Init(bool first_node, bool show_cmd, bool period_tick) {
    first_node_ = first_node;
    show_cmd_ = show_cmd;
    AddBaseCommands();
    return true;
}

int clear_icanon(void) {
    struct termios settings;
    int result;
    result = tcgetattr(STDIN_FILENO, &settings);
    if (result < 0) {
        perror("error in tcgetattr");
        return 0;
    }

    settings.c_lflag &= ~ICANON;
    result = tcsetattr(STDIN_FILENO, TCSANOW, &settings);
    if (result < 0) {
        perror("error in tcsetattr");
        return 0;
    }
    return 1;
}

void Command::Run() {
    Help();
    clear_icanon();
    while (!destroy_) {
        if (!show_cmd_) {
            std::this_thread::sleep_for(std::chrono::microseconds(200000ll));
            continue;
        }

        std::cout << std::endl << std::endl << "cmd > ";
        std::string cmdline;
        std::getline(std::cin, cmdline);
        ProcessCommand(cmdline);
    }
}

void Command::ProcessCommand(const std::string& cmdline) {
    if (cmdline.empty()) {
        return;
    }

    std::string cmd;
    std::vector<std::string> args;
    try {
        common::Split<> line_split(cmdline.c_str(), ' ', cmdline.size());
        cmd = "";
        for (uint32_t i = 0; i < line_split.Count(); ++i) {
            if (strlen(line_split[i]) == 0) {
                continue;
            }

            if (cmd == "") {
                cmd = line_split[i];
            } else {
                args.push_back(line_split[i]);
            }
        }
    } catch (const std::exception& e) {
        INIT_WARN("Error processing command: %s", e.what());
    }

    auto it = cmd_map_.find(cmd);
    if (it == cmd_map_.end()) {
        std::cout << "Invalid command : " << cmd << std::endl;
    } else {
        try {
            (it->second)(args);
        } catch (std::exception& e) {
            std::cout << "catch error: " << e.what() << std::endl;
        }
    }
}

void Command::AddCommand(const std::string& cmd_name, CommandFunction cmd_func) {
    assert(cmd_func);
    auto it = cmd_map_.find(cmd_name);
    if (it != cmd_map_.end()) {
        INIT_WARN("command(%s) exist and ignore new one", cmd_name.c_str());
        return;
    }
    cmd_map_[cmd_name] = cmd_func;
}

void Command::AddBaseCommands() {
    AddCommand("help", [this](const std::vector<std::string>& args) {
        Help();
    });
    AddCommand("prt", [this](const std::vector<std::string>& args) {
        if (args.size() <= 0) {
            return;
        }
        uint32_t network_id = 0;
        common::StringUtil::ToUint32(args[0], &network_id);
        PrintDht(network_id);
    });
}

void Command::PrintDht(uint32_t network_id) {
    auto base_dht = network::DhtManager::Instance()->GetDht(network_id);
    if (!base_dht) {
        base_dht = network::UniversalManager::Instance()->GetUniversal(network_id);
    }

    if (!base_dht) {
        return;
    }
    dht::DhtPtr readonly_dht = base_dht->readonly_hash_sort_dht();
    auto node = base_dht->local_node();
    std::cout << "dht nnum: " << readonly_dht->size() + 1 << std::endl;
    std::cout << "local: " << common::Encode::HexEncode(node->id) << ":" << node->id_hash
        << ", " << common::Encode::HexSubstr(node->dht_key) << ":" << node->dht_key_hash
        << ", " << node->public_ip << ":" << node->public_port << std::endl;
    for (auto iter = readonly_dht->begin(); iter != readonly_dht->end(); ++iter) {
        auto node = *iter;
        assert(node != nullptr);
        std::cout << common::Encode::HexSubstr(node->id)
            << ", " << node->dht_key_hash
            << ", " << common::Encode::HexSubstr(node->dht_key)
            << ", " << common::Encode::HexEncode(node->pubkey_str)
            << ", " << node->public_ip << ":" << node->public_port << std::endl;
    }
}

void Command::Help() {
    std::cout << "Allowed options:" << std::endl;
    std::cout << "\t-h [help]            print help info" << std::endl;
    std::cout << "\t-c [conf]            set config path" << std::endl;
    std::cout << "\t-v [version]         get bin version" << std::endl;
    std::cout << "\t-g [show_cmd]        show command" << std::endl;
    std::cout << "\t-p [peer]            bootstrap peer ip:port" << std::endl;
    std::cout << "\t-f [first]           1: first node 0: no" << std::endl;
    std::cout << "\t-a [address]         local ip" << std::endl;
    std::cout << "\t-l [listen_port]     local port" << std::endl;
    std::cout << "\t-d [db]              db path" << std::endl;
    std::cout << "\t-o [country]         country code" << std::endl;
    std::cout << "\t-n [network]         network id" << std::endl;
    std::cout << "\t-L [log]             log path" << std::endl;
}

}  // namespace init

}  // namespace zjchain
