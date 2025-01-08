#include <iostream>
#include <queue>
#include <vector>

#include "init/network_init.h"
#include "pki/param.h"
#include "pki/pki_ib_agka.h"

int main(int argc, char** argv) {
    PkiIbAgka protocol(kTypeA);
    protocol.Simulate();
    return 0;
}
