#if ENCODING == 3
#include "ternary.hpp"

#include <algorithm>
#include <deque>
#include <vector>
#include <array>
#include <stdexcept>
#include <bitset>
#include <functional>

//-------------------------CONVENIENCE FUNCTIONS------------------------------------

std::ostream& operator<<(std::ostream& os, ternary t) {
  if (t == X0) os << "0";
  else if (t == X1) os << "1";
  else if (t == X) os << "X";
  else assert(false);
  return os;
}

//--------------------------GROUND SIMULATION----------------------------------------

// The encoding of ternary values.
enum g_ternary : uint_fast8_t { g_X0, g_X1, g_X, g_XX };
// These macros depend on the encoding and are used globally so please define
// them.
// In this version there two possible values that are recognized as X.
#define g_ISX(L) ((L)&2u)
// Literal to its ternary value, only the sign bit is considered.
#define g_STX(L) (static_cast<g_ternary>(~(L)&1u))
// Ternary value to sign bit that can be or-ed to a variable to get a literal.
#define g_XTS(L) (assert(!g_ISX(L)), (~(L)) & 1u)

g_ternary operator&&(g_ternary t1, g_ternary t2){
  if (t1 == g_X0 or t2 == g_X0) {
    return g_X0;
  } else if (t1 == g_X1 and t2 == g_X1){
    return g_X1;
  } else {
    return g_X;
  }
}

g_ternary operator^(g_ternary t1, g_ternary t2){
  return static_cast<g_ternary>(static_cast<uint_fast8_t>(t1) ^ static_cast<uint_fast8_t>(t2));
}

void ground_simulate(aiger_and ands[], const unsigned num_ands,
              std::vector<g_ternary> &s) {
  L4 << "before" << s;

  for (unsigned int i = 0; i < num_ands; i++) {
    unsigned int lhs_index = IDX(ands[i].lhs);
    unsigned int rhs0_index = IDX(ands[i].rhs0);
    unsigned int rhs1_index = IDX(ands[i].rhs1);
    s[lhs_index] = (g_X1 ^ g_STX(ands[i].rhs0) ^ s[rhs0_index]) && (g_X1 ^ g_STX(ands[i].rhs1) ^ s[rhs1_index]);
  }

  L4 << "after" << s;
}

//------------------------NEW SIMULATION---------------------------------------

ternary operator&&(ternary t1, ternary t2){
  return static_cast<ternary>(static_cast<uint_fast8_t>(t1) & static_cast<uint_fast8_t>(t2));
}

ternary operator||(ternary t1, ternary t2){
  return static_cast<ternary>(static_cast<uint_fast8_t>(t1) | static_cast<uint_fast8_t>(t2));
}

ternary operator^(ternary t1, ternary t2){
  return (t1 || t2) && ~(t1 && t2); // Write XOR as expression in terms of not, and, or.
}

ternary operator~(ternary t){
  uint_fast8_t a = static_cast<uint_fast8_t>(t);
  uint_fast8_t b = a << 1;
  uint_fast8_t c = (b ^ a);
  uint_fast8_t d = c & 0b10;
  uint_fast8_t e = (d ^ (d >> 1));
  return static_cast<ternary>(e ^ a ^ 0b11);
}

ternary conditional_complement(ternary t, ternary stx) {
  uint_fast8_t a = static_cast<uint_fast8_t>(t);
  uint_fast8_t b = a << 1;
  uint_fast8_t c = (b ^ a);
  uint_fast8_t d = c & 0b10;
  uint_fast8_t e = (d ^ (d >> 1));
  return static_cast<ternary>((e | static_cast<uint_fast8_t>(stx)) ^ a ^ 0b11);
}

// Continue new simulation

void propagate(aiger_and ands[], const unsigned num_ands,
              std::vector<ternary> &s) {
#if ALGORITHM != 0
 #error Invalid ENCODING / ALGORITHM combination
#endif
  L4 << "before" << s;

  for (unsigned int i = 0; i < num_ands; i++) {
    unsigned int lhs_index = IDX(ands[i].lhs);
    unsigned int rhs0_index = IDX(ands[i].rhs0);
    unsigned int rhs1_index = IDX(ands[i].rhs1);
    s[lhs_index] = static_cast<ternary>(
            conditional_complement(s[rhs0_index], STX(ands[i].rhs0)) &
            conditional_complement(s[rhs1_index], STX(ands[i].rhs1)));
  }

  // Check for correctness:
/*
  // Convert to ground truth ternary
  std::vector<g_ternary> g_s(s.size());
  std::vector<g_ternary> g_s2(s.size());
  for (unsigned int i = 0; i < s.size(); i++) {
    if (s[i] == X0) {
      g_s[i] = g_X0;
      g_s2[i] = g_X0;
    } else if (s[i] == X1) {
      g_s[i] = g_X1;
      g_s2[i] = g_X1;
    } else if (s[i] == X) {
      g_s[i] = g_X;
      g_s2[i] = g_X;
    } else {
      assert(false);
    }
  }

  ground_simulate(ands, num_ands, g_s);

  // Compare with ground truth
  for (unsigned int i = 0; i < s.size(); i++) {
    assert(g_s[i] == g_s2[i]);
  }
  */

  L4 << "after" << s;
}

//----------------------BATCH SIMULATION-------------------------

#define batch_STX(L) BatchTernary(STX(L)).ternaries

class BatchTernary {
public:
    uint_fast64_t ternaries = 0;

    BatchTernary(uint_fast64_t ternaries){
      this->ternaries = ternaries;
    }

    // Make BatchTernary from copies from ternary s
    BatchTernary(ternary t){
      uint_fast64_t a = static_cast<uint_fast64_t>(t);
      for (unsigned i = 0; i < 5; i++) {
        a = a ^ (a << (2 << i));
        this->ternaries = a;
      }
    }

    // Index must be between 0 and 31.
    void write(std::size_t idx, ternary t){
      //assert(idx < 32);
      //std::cerr << "Write at " << idx << " " << t << "\n";
      //std::cerr << std::bitset<64>(ternaries) << "\n";
      ternaries = (ternaries & ~(3ll << 2*idx)) | (static_cast<uint_fast64_t>(t) << 2*idx);
      //std::cerr << std::bitset<64>(ternaries) << "\n";
    }

    // Index must be between 0 and 31.
    ternary read(std::size_t idx){
      //assert(idx < 32);
      //std::cerr << "Extract from index " << idx << "\n";
      //std::cerr << std::bitset<64>(ternaries) << "\n";
      //std::cerr << static_cast<ternary>((ternaries & (3ll << 2*idx)) >> 2*idx) << "\n";
      return static_cast<ternary>((ternaries & (3ll << 2*idx)) >> 2*idx);
    }

    BatchTernary conditional_complement(uint_fast64_t batch_stx) {
      uint_fast64_t a = ternaries;
      uint_fast64_t b = a << 1;
      uint_fast64_t c = (b ^ a);
      uint_fast64_t d = c & 0b1010101010101010101010101010101010101010101010101010101010101010;
      uint_fast64_t e = (d ^ (d >> 1));
      return BatchTernary((e | batch_stx) ^ a ^ 0b1111111111111111111111111111111111111111111111111111111111111111);
    }

    BatchTernary operator~() {
      uint_fast64_t a = ternaries;
      uint_fast64_t b = a << 1;
      uint_fast64_t c = (b ^ a);
      uint_fast64_t d = c & 0b1010101010101010101010101010101010101010101010101010101010101010;
      uint_fast64_t e = (d ^ (d >> 1));
      return BatchTernary(e ^ a ^ 0b1111111111111111111111111111111111111111111111111111111111111111);
    }

    BatchTernary operator&&(const BatchTernary other) {
      return BatchTernary(ternaries & other.ternaries);
    }

    BatchTernary operator||(const BatchTernary other) {
      return BatchTernary(ternaries | other.ternaries);
    }

    BatchTernary operator^(const BatchTernary other) {
      return (*this || other) && ~(*this && other);
    }
};

std::ostream& operator<<(std::ostream& os, BatchTernary t) {
  os << "[ ";
  for (unsigned i = 0; i < 32; i++) {
    os << t.read(i) << " ";
  }
  os << "]\n";
  return os;
}

bool operator==(const BatchTernary t1, const BatchTernary t2) {
  return t1.ternaries == t2.ternaries;
}

void batch_simulate(aiger_and ands[], const unsigned num_ands,
              std::vector<BatchTernary> &s) {

  for (unsigned int i = 0; i < num_ands; i++) {
    unsigned int lhs_index = IDX(ands[i].lhs);
    unsigned int rhs0_index = IDX(ands[i].rhs0);
    unsigned int rhs1_index = IDX(ands[i].rhs1);
    s[lhs_index] =
            s[rhs0_index].conditional_complement(batch_STX(ands[i].rhs0)) &&
            s[rhs1_index].conditional_complement(batch_STX(ands[i].rhs1));
  }
}

class BatchSimulator {
private:
    aiger_and* ands;
    unsigned num_ands;
    std::vector<std::function<void(BatchSimulator*, std::vector<BatchTernary>&, unsigned)>> call_list;
    std::vector<unsigned> indices;

    // Methods are static because of non-static methods seemingly can't be put into a vector
    static void do_and(BatchSimulator *sim, std::vector<BatchTernary> &s, unsigned i) {
      unsigned int lhs_index = sim->indices[3*i];
      unsigned int rhs0_index = sim->indices[3*i+1];
      unsigned int rhs1_index = sim->indices[3*i+2];
      s[lhs_index] = s[rhs0_index] && s[rhs1_index];
    }

    static void do_and_first_neg(BatchSimulator *sim, std::vector<BatchTernary> &s, unsigned i) {
      unsigned int lhs_index = sim->indices[3*i];
      unsigned int rhs0_index = sim->indices[3*i+1];
      unsigned int rhs1_index = sim->indices[3*i+2];
      s[lhs_index] = ~(s[rhs0_index]) && s[rhs1_index];
    }

    static void do_and_second_neg(BatchSimulator *sim, std::vector<BatchTernary> &s, unsigned i) {
      unsigned int lhs_index = sim->indices[3*i];
      unsigned int rhs0_index = sim->indices[3*i+1];
      unsigned int rhs1_index = sim->indices[3*i+2];
      s[lhs_index] = s[rhs0_index] && ~(s[rhs1_index]);
    }

    static void do_and_both_neg(BatchSimulator *sim, std::vector<BatchTernary> &s, unsigned i) {
      unsigned int lhs_index = sim->indices[3*i];
      unsigned int rhs0_index = sim->indices[3*i+1];
      unsigned int rhs1_index = sim->indices[3*i+2];
      s[lhs_index] = ~(s[rhs0_index] || s[rhs1_index]);
    }

public:
    BatchSimulator(aiger_and ands[], const unsigned num_ands, const unsigned maxvar) {
      this->ands = ands;
      this->num_ands = num_ands;
      this->call_list.reserve(num_ands);
      this->indices.resize(3*maxvar);
      for (unsigned i = 0; i < num_ands; i++) {
        indices[3*i] = IDX(ands[i].lhs);
        indices[3*i+1] = IDX(ands[i].rhs0);
        indices[3*i+2] = IDX(ands[i].rhs1);
        if (STX(ands[i].rhs0) == X1 and STX(ands[i].rhs1) == X1) {
          this->call_list.push_back(do_and);
        } else if (STX(ands[i].rhs0) == X0 and STX(ands[i].rhs1) == X1) {
          this->call_list.push_back(do_and_first_neg);
        } else if (STX(ands[i].rhs0) == X1 and STX(ands[i].rhs1) == X0) {
          this->call_list.push_back(do_and_second_neg);
        } else if (STX(ands[i].rhs0) == X0 and STX(ands[i].rhs1) == X0) {
          this->call_list.push_back(do_and_both_neg);
        } else assert(false);
      }
    }

    void simulate(std::vector<BatchTernary> &s) {
      for (unsigned i = 0; i < num_ands; i++) {
        call_list[i](this, s, i);
      }
    }
};

//---------------------------HIT GENERATION WITH BATCH SIMULATION-----------------------------

void assert_batch_coverage_check_matches_old_coverage_check(const std::vector<unsigned> &obligations,
                                                            std::vector<BatchTernary> &s,
                                                            bool covered,
                                                            unsigned i) {
  std::vector <ternary> s_old;
  s_old.resize(s.size());
  for (unsigned j = 0; j < s.size(); j++) {
    s_old[j] = s[j].read(i);
  }
  const bool old_covered = std::none_of(obligations.begin(), obligations.end(),
                                  [&s_old](const unsigned l) { return ISX(s_old[IDX(l)]); });
  assert(covered == old_covered);
}

std::deque <std::vector<unsigned>> generate_hits(BatchSimulator simulator,
                                                const std::vector<unsigned> &obligations,
                                                std::vector<BatchTernary> &s,
                                                std::deque<std::vector<unsigned>> &candidates) {
  //asert(not candidates.empty());
  std::deque <std::vector<unsigned>> hits;
  const unsigned candidate_size = candidates.front().size();
  //assert(candidate_size > 0);
  std::vector<std::vector<unsigned>> used_candidates(32);
  std::vector<ternary> v(32*candidate_size);

  while (not candidates.empty()) {
    const unsigned num_candidates = std::min((size_t) 32, candidates.size());
    for (unsigned i = 0; i < num_candidates; i++) {
      used_candidates[i] = std::move(candidates.front());
      candidates.pop_front();
      std::vector<unsigned> &candidate = used_candidates[i];

      // Mutate state
      for (unsigned j = 0; j < candidate.size(); j++) {
        unsigned idx = candidate[j];
        v[32*j + i] = s[idx].read(i);
        s[idx].write(i, X);
      }
    }
    simulator.simulate(s);

    for (unsigned i = 0; i < num_candidates; i++) {
      //Check coverage
      std::vector<unsigned> &candidate = used_candidates[i];
      //assert(candidate.size() > 0);
      bool covered = std::none_of(obligations.begin(), obligations.end(),
                                  [&s, i](const unsigned l) { return ISX(s[IDX(l)].read(i)); });
      //assert_batch_coverage_check_matches_old_coverage_check(obligations, s, covered, i);

      // Revert to initial state
      for (unsigned j = 0; j < candidate.size(); j++) {
        unsigned idx = candidate[j];
        s[idx].write(i, v[32*j + i]);
      }
      if (covered) hits.push_back(std::move(candidate));
    }
  }
  return hits;
}

//-------------------------REDUCE-------------------------------

/*
 * We aim to replace the original DFS-like search that takes the first deepest path it found by a more sophisticated search.
 *
 * Using a full BFS is a first step to considering the full search space but for N latches,
 * there are 2^N different possible combinations. Due to anti-monotonicity properties of the problem
 * (for X-ing at indices a, b, c to make sense, X-ing at a,b; b,c and a,c must have been successful)
 * we can use an apriori-like algorithm for this search space.
 *
 * We first generate all candidates consisting of only a single Xed latch. All the successors are then combined
 * to candidates of 2 latches each. Of those some can be eliminated by simulation. Of those successful combinations (hit)
 * only those that share a latch can be reasonably combined. Due to the anti-monotonicity, one can always keep
 * them sorted by their index and only combine those that match in the first element.
 * for a combination of m latches this generalizes to the m-1 first latches being equal.
 *
 * Even if the combinations do not grow deep, it should still be a more efficient way to generate new combinations than
 * less sophisticated ones.
 *
 * Additionally, one should be able to return all maximal combinations of reducible latches, if needed.
 *
 */

// Generates all those candidates that have a single latch replaced by an X.
std::deque<std::vector<unsigned>> generate_initial_candidates(unsigned latches_begin, unsigned num_latches) {
  std::deque<std::vector<unsigned>> candidates;
  for (unsigned int i = latches_begin; i < latches_begin + num_latches; i++) {
    std::vector<unsigned> candidate;
    candidate.reserve(num_latches);
    candidate.push_back(i);
    candidates.push_back(candidate);
  }
  return candidates;
}

void assert_vector_is_sorted(std::vector<unsigned> candidate) {
  std::vector test(candidate.begin(), candidate.end());
  std::sort(test.begin(), test.end());
  assert(candidate == test);
}

// Returns true if candidate is covered by obligations, false otherwise
bool candidate_is_covered(aiger *model,
                          const std::vector<unsigned> &obligations,
                          std::vector<ternary> &s,
                          std::vector<unsigned> &candidate) {
  //assert_vector_is_sorted(candidate);
  std::vector<ternary> v; // stores original values of s temporarily
  v.resize(model->num_latches);
  // Replace the affected latches
  for (unsigned i = 0; i < candidate.size(); i++) {
    unsigned idx = candidate[i];
    v[i] = s[idx];
    s[idx] = X;
  }

  // Simulate and check coverage
  propagate(model->ands, model->num_ands, s);
  const bool covered =
          std::none_of(obligations.begin(), obligations.end(),
                       [&s](const unsigned l) { return ISX(s[IDX(l)]); });

  // Revert to previous state
  for (unsigned i = 0; i < candidate.size(); i++) {
    unsigned idx = candidate[i];
    s[idx] = v[i];
  }
  return covered;
}

std::deque<std::vector<unsigned>> generate_new_candidates(std::deque<std::vector<unsigned>> &hits) {
  std::deque<std::vector<unsigned>> candidates;
  while (not hits.empty()) {
    select_next_hit:
    std::vector<unsigned> next = std::move(hits.front());
    hits.pop_front();
    //assert(next.size() > 0);
    for (const auto &comp: hits) {
      //assert(comp.size() == next.size());
      for (unsigned i = 0; i < next.size() - 1; i++) {
        if (next[i] != comp[i]) {
          goto select_next_hit;
        }
      }
      std::vector<unsigned> vec(next.size() + 1);
      for (unsigned i = 0; i < next.size(); i++) {
        vec[i] = next[i];
      }
      vec[next.size()] = comp.back();
      candidates.push_back(std::move(vec));
    }
  }
  return candidates;
}

std::vector<unsigned> generate_cube(unsigned latches_begin,
                                    unsigned num_latches,
                                    std::vector<unsigned> &best_hit,
                                    std::vector<ternary> &s) {
  std::vector<unsigned> cube;
  cube.reserve(num_latches);
  for (unsigned i = latches_begin; i < latches_begin + num_latches; ++i) {
    if (std::all_of(best_hit.begin(), best_hit.end(), [&i](const unsigned j) { return i != j; })) {
      cube.push_back(VAR(i) | XTS(s[i]));
    }
  }
  //assert(cube.size() + best_hit.size() == num_latches);
  return cube;
}

std::vector<unsigned> reduce(aiger *model,
                             const std::vector<unsigned> &obligations,
                             std::vector<ternary> &s) {
#ifndef NDEBUG
  L4 << "checking that obligations are fulfilled before reduction";
  propagate(model->ands, model->num_ands, s);
  const bool covered =
          std::none_of(obligations.begin(), obligations.end(),
                       [&s](const unsigned l) { return ISX(s[IDX(l)]); });
  assert(covered);
#endif

  // Store candidates as vectors of the mutated latch index
  const unsigned latches_begin = model->num_inputs + 1;
  std::deque <std::vector<unsigned>> candidates = generate_initial_candidates(latches_begin, model->num_latches);
  std::deque <std::vector<unsigned>> hits;
  std::vector<unsigned> best_hit;
  best_hit.reserve(model->num_latches);

  // Construct the BatchTernary vector
  std::vector<BatchTernary> batch_s;
  batch_s.reserve(s.size());
  for (auto &t : s) {
    batch_s.push_back(BatchTernary(t));
  }

  // Construct Simulator
  BatchSimulator simulator(model->ands, model->num_ands, model->maxvar);

  // Generate candidates and hits in an alternating manner
  while (not candidates.empty()) {
    hits = generate_hits(simulator, obligations, batch_s, candidates);
    assert(candidates.empty());

    if (not hits.empty()){
      best_hit = hits.front();
      candidates = generate_new_candidates(hits);
      assert(hits.empty());
    }
  }
  //assert(candidate_is_covered(model, obligations, s, best_hit));
  return generate_cube(latches_begin, model->num_latches, best_hit, s);
}
#endif
