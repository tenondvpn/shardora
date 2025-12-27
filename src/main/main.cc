#include <iostream>
#include <queue>
#include <vector>

#include "init/network_init.h"

static void GlobalInitSpdlog() {
    spdlog::init_thread_pool(8192, 1);

    auto logger = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
        "async_file", "log/shardora.log", true);

    spdlog::set_default_logger(logger);

    // 关键：强制设置全局 pattern
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [thread %t] %-5l [%n] %v%$");

    // 额外保险：遍历所有 sink 重新设置（防止被覆盖）
    for (auto& sink : logger->sinks()) {
        sink->set_pattern("%Y-%m-%d %H:%M:%S.%e [thread %t] %-5l [%n] %v%$");
    }

    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::err);

    spdlog::debug("init spdlog success.");
}

int main(int argc, char** argv) {
    GlobalInitSpdlog();
    shardora::init::NetworkInit init;
    if (init.Init(argc, argv) != 0) {
        SHARDORA_ERROR("init network error!");
        return 1;
    }

    init.Destroy();
    return 0;
}
