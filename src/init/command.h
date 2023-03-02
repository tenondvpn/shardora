#pragma once

#include <functional>
#include <vector>
#include <mutex>
#include <map>
#include <unordered_set>

#include "common/utils.h"
#include "common/tick.h"

namespace zjchain {

namespace init {

typedef std::function<void(const std::vector<std::string>&)> CommandFunction;

class Command {
public:
    Command();
    ~Command();

    bool Init(bool first_node, bool show_cmd, bool period_tick = false);
    void Run();
    void Destroy() { destroy_ = true; }
    void Help();

private:
    struct ConfigNodeInfo {
        std::string country;
        std::string ip;
        std::string pk;
        std::string dht_key;
        uint16_t vpn_port;
    };

    void ProcessCommand(const std::string& cmdline);
    void AddCommand(const std::string& cmd_name, CommandFunction cmd_func);
    void AddBaseCommands();
    void PrintDht(uint32_t network_id);

    static const uint32_t kTransportTestPeriod = 1000 * 1000;
    std::map<std::string, CommandFunction> cmd_map_;
    bool destroy_{ false };
    bool show_cmd_{ false };
    bool first_node_{ false };
    std::vector<ConfigNodeInfo> config_node_info_;
    std::set<std::string> config_node_ips_;
};

}  // namespace init

}  // namespace zjchain
