#include <iostream>
#include <queue>
#include <vector>

#include "init/network_init.h"

static void GlobalInitSpdlog() {
    spdlog::init_thread_pool(65536, 2);
    auto max_size = 1024LL * 1024 * 1024;
    auto max_files = 60;
    auto logger = spdlog::create_async<spdlog::sinks::rotating_file_sink_mt>(
        "async_file",
        "log/shardora.log",
        max_size,
        max_files);
    // auto logger = spdlog::basic_logger_mt("sync_file", "log/shardora.log", false);
    spdlog::set_default_logger(logger);
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%e [thread %t] %-5l [%n] %v%$");
    for (auto& sink : logger->sinks()) {
        sink->set_pattern("%Y-%m-%d %H:%M:%S.%e [thread %t] %-5l [%n] %v%$");
    }

    spdlog::set_level(spdlog::level::debug);
    spdlog::flush_on(spdlog::level::err);
    spdlog::debug("init spdlog success: %d", 1);
}

int main(int argc, char** argv) {
    GlobalInitSpdlog();
    shardora::common::SignalRegister();
    shardora::init::NetworkInit init;
    if (init.Init(argc, argv) != 0) {
        SHARDORA_ERROR("init network error!");
        return 1;
    }

    init.Destroy();
    spdlog::shutdown();
    return 0;
}
