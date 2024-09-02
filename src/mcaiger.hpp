#include "aiger.hpp"

#include <utility>
#include <vector>

void mcaiger_free();
std::pair<bool, int> mcaiger(aiger *aig, unsigned simple_path);
void stimulus(int k, std::vector<std::vector<unsigned>> &cex);
