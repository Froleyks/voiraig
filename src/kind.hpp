#pragma once

#include "aiger.hpp"

#include <vector>

bool kind(aiger *model, aiger *&witness, std::vector<std::vector<unsigned>> &cex, unsigned simple_path);
