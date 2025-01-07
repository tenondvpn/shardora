#include "param.hh"
#include "pki_ib_agka.hh"

int main(int /*argc*/, char** /*argv[]*/) {
  PkiIbAgka protocol(kTypeA);

  protocol.Simulate();

  return 0;
}
