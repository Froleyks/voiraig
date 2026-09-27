#pragma once
#include "aiger.hpp"

bool generalized_search(aiger *model, const options &opts, aiger *&witness,
                        std::vector<std::vector<unsigned>> &cex,
                        unsigned &property_index, bool &is_justice);
