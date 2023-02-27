#include <iostream>
#include <queue>
#include <vector>

#include "init/network_init.h"

int main(int argc, char** argv) {
    log4cpp::PropertyConfigurator::configure("./conf/log4cpp.properties");
    zjchain::init::NetworkInit init;
    if (init.Init(argc, argv) != 0) {
        ZJC_ERROR("init network error!");
        return 1;
    }

    init.Destroy();
    return 0;
}
