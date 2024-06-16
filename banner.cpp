#include "banner.hpp"
#include <iostream>

void print_banner(void) {
  std::cout << "Voiraig - Certifying Bit Level Model Checker\n";
  std::cout << "Copyright (c) 2024- Nils Froleyks Johannes Kepler University\n";
  std::cout << "Version " << VERSION << " Commit " << GITID << "\n";
}

void print_version(void) { std::cout << VERSION; }

void print_id(void) { std::cout << GITID; }
