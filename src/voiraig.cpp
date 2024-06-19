#include "banner.hpp"
#include "options.hpp"
#include "utils.hpp"

int main(int argc, char *argv[]) {
  print_banner();
  options options;
  parse_options(argc, argv, &options);
}
