#include <iostream>
#include <queue>
#include <vector>

#include "init/network_init.h"

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
