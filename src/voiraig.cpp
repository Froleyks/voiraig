#include "aiger.hpp"
#include "banner.hpp"
#include "cadical.hpp"
#include "ic3.hpp"
#include "options.hpp"

#include "utils.hpp"

int main(int argc, char *argv[]) {
  print_banner();
  options options;
  parse_options(argc, argv, &options);
  InAIG model(options.model);
  std::vector<std::vector<unsigned>> cex;
  const bool bug = ic3(*model, cex);
  if (bug) {
    write_witness(*model, cex, options.witness);
    return 10;
  } else {
    write_witness(*model, options.witness);
    return 20;
  }
}
