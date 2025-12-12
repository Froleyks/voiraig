#include "aiger.hpp"
#include "banner.hpp"
#include "cadical.hpp"
#include "ic3.hpp"
#include "kind.hpp"
#include "k_liveness.hpp"
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


  if ((*model)->num_justice)
    bug = k_liveness(*model, witness, cex);
  else if (options.kind)
    bug = kind(*model, witness, cex, options.paths, options.unique);
  else
    bug = ic3(*model, cex);

  if (bug) {
    if (options.trace) write_witness(*model, cex, options.witness_sat);
    std::cout << "sat\n";
  } else {
    if (options.certificate) {
      if (witness)
        write_witness(witness, options.witness_uns);
      else
        write_witness(*model, options.witness_uns);
    }
    if (witness && witness != *model) aiger_reset(witness);
    std::cout << "unsat\n";
  }
}
