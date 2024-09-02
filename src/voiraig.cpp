#include "aiger.hpp"
#include "banner.hpp"
#include "cadical.hpp"
#include "ic3.hpp"
#include "kind.hpp"
#include "options.hpp"

#include "utils.hpp"

int main(int argc, char *argv[]) {
  options options;
  parse_options(argc, argv, &options);
  print_banner();
  Logging::init(&options);
  InAIG model(options.model, &options);
  std::vector<std::vector<unsigned>> cex;
  bool bug;
  aiger *witness{};
  if (options.kind)
    bug = kind(*model, witness, cex, options.paths, options.unique);
  else
    bug = ic3(*model, cex);
  if (bug) {
    if (options.trace) write_witness(*model, cex, options.witness);
    L0 << "exit 10\n";
    return 10;
  } else {
    if (options.certificate) {
      if (witness)
        write_witness(witness, options.witness);
      else
        write_witness(*model, options.witness);
    }
    if (witness && witness != *model) aiger_reset(witness);
    L0 << "exit 20\n";
    return 20;
  }
}
