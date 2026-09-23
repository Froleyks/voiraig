#include "general.hpp"

#include "ic3.hpp"

#include <algorithm>
#include <string>
#include <unordered_map>

namespace {

// The monitor waits for one acceptance signal at a time.  Its phase moves
// lexicographically down until completing a round, which consumes a life.
// Thus an infinite accepting execution eventually exhausts any finite budget.
struct Monitor {
  std::vector<unsigned> signals;
  std::vector<unsigned> phase;
  std::vector<unsigned> lives;
};

struct Reduction {
  aiger *aig;
  std::vector<Monitor> monitors;
  std::vector<unsigned> rank;
};

unsigned land(aiger *aig, unsigned a, unsigned b) {
  if (!a || !b || a == NOT(b)) return 0;
  if (a == 1 || a == b) return b;
  if (b == 1) return a;
  return conj(aig, a, b);
}

unsigned lor(aiger *aig, unsigned a, unsigned b) {
  return NOT(land(aig, NOT(a), NOT(b)));
}

unsigned same(aiger *aig, unsigned a, unsigned b) {
  if (a == b) return 1;
  if (a == NOT(b)) return 0;
  return lor(aig, land(aig, a, b), land(aig, NOT(a), NOT(b)));
}

Reduction reduce(aiger *model, unsigned budget) {
  Reduction result{aiger_init(), {}, {}};
  aiger *aig = result.aig;
  std::vector<unsigned> map(size(model), INVALID_LIT);
  auto bind = [&](unsigned from, unsigned to) {
    map[from] = to;
    map[NOT(from)] = NOT(to);
  };
  bind(0, 0);
  for (auto &i : inputs(model))
    bind(i.lit, input(aig));
  for (auto &l : latches(model))
    bind(l.lit, latch(aig));

  result.monitors.resize(model->num_justice);
  for (unsigned j = 0; j < model->num_justice; ++j) {
    Monitor &monitor = result.monitors[j];
    for (unsigned i = 0; i < budget; ++i)
      monitor.lives.push_back(latch(aig, "remaining round"));
    unsigned count = model->num_fairness + model->justice[j].size;
    if (count > 1)
      for (unsigned i = 0; i < count; ++i)
        monitor.phase.push_back(latch(aig, "acceptance phase"));
    result.rank.insert(result.rank.end(), monitor.lives.begin(),
                       monitor.lives.end());
    result.rank.insert(result.rank.end(), monitor.phase.begin(),
                       monitor.phase.end());
  }

  for (auto &a : ands(model))
    bind(a.lhs, land(aig, map[a.rhs0], map[a.rhs1]));
  for (auto &l : latches(model)) {
    auto *copy = aiger_is_latch(aig, map[l.lit]);
    copy->next = map[l.next];
    copy->reset = map[l.reset];
  }
  for (auto &c : constraints(model))
    aiger_add_constraint(aig, map[c.lit], c.name);

  unsigned bad = 0;
  for (unsigned i = 0; i < model->num_bad; ++i)
    bad = lor(aig, bad, map[model->bad[i].lit]);

  for (unsigned j = 0; j < model->num_justice; ++j) {
    Monitor &monitor = result.monitors[j];
    for (unsigned i = 0; i < model->num_fairness; ++i)
      monitor.signals.push_back(map[model->fairness[i].lit]);
    for (unsigned i = 0; i < model->justice[j].size; ++i)
      monitor.signals.push_back(map[model->justice[j].lits[i]]);

    unsigned complete = monitor.signals.empty() ? 1 : monitor.signals.back();
    if (!monitor.phase.empty()) {
      unsigned selected = 0, multiple = 0;
      for (unsigned phase : monitor.phase) {
        multiple = lor(aig, multiple, land(aig, selected, phase));
        selected = lor(aig, selected, phase);
      }
      // Explicitly prove phase validity, which is also needed by the rank.
      bad = lor(aig, bad, lor(aig, NOT(selected), multiple));
      complete = land(aig, monitor.phase.back(), complete);
      for (unsigned i = 0; i < monitor.phase.size(); ++i) {
        unsigned previous = i ? i - 1 : monitor.phase.size() - 1;
        auto *phase = aiger_is_latch(aig, monitor.phase[i]);
        phase->reset = i == 0;
        phase->next =
            lor(aig, land(aig, monitor.phase[i], NOT(monitor.signals[i])),
                land(aig, monitor.phase[previous], monitor.signals[previous]));
      }
    }
    for (unsigned i = 0; i < monitor.lives.size(); ++i) {
      auto *life = aiger_is_latch(aig, monitor.lives[i]);
      life->reset = 1;
      life->next = land(aig, monitor.lives[i],
                        lor(aig, NOT(complete), i ? monitor.lives[i - 1] : 0));
    }
    unsigned exhausted = monitor.lives.empty() ? 1 : NOT(monitor.lives.back());
    bad = lor(aig, bad, land(aig, exhausted, complete));
  }

  // Separate aliases make the default intervention mapping unambiguous even
  // when several latches have the same next-state function or a constant one.
  for (auto &l : latches(aig))
    l.next = conj(aig, l.next, l.next);
  aiger_add_bad(aig, bad, "safety and acceptance budget");
  return result;
}

// A common lexicographic rank serves every justice group.  q_i is the negated
// AIGER justice/fairness literal.  On real transitions the rank cannot
// increase; when it is equal, each monitor waits for an acceptance signal that
// is absent. For arbitrary pairs, Q is rank(source) <= rank(target).  This
// gives closure and reverse decrease, while equality makes selected signals
// consistent.
void certificate(aiger *model, Reduction &reduction) {
  aiger *aig = reduction.aig;
  unsigned less = 0, equal = 1;
  for (unsigned bit : reduction.rank) {
    unsigned target = aiger_is_latch(aig, bit)->next;
    less = lor(aig, less, land(aig, equal, land(aig, NOT(bit), target)));
    equal = land(aig, equal, same(aig, bit, target));
  }
  std::vector<unsigned> fair_selected(model->num_fairness, 0);
  std::vector<std::vector<unsigned>> justice_selected(model->num_justice);
  for (unsigned j = 0; j < model->num_justice; ++j) {
    const Monitor &monitor = reduction.monitors[j];
    justice_selected[j].resize(model->justice[j].size);
    for (unsigned i = 0; i < monitor.signals.size(); ++i) {
      unsigned selected = monitor.phase.empty() ? 1 : monitor.phase[i];
      if (i < model->num_fairness)
        fair_selected[i] = lor(aig, fair_selected[i], selected);
      else
        justice_selected[j][i - model->num_fairness] = selected;
    }
  }
  auto rank_signal = [&](unsigned selected) {
    return NOT(lor(aig, less, land(aig, equal, selected)));
  };
  for (unsigned i = 0; i < model->num_fairness; ++i)
    aiger_add_fairness(aig, rank_signal(fair_selected[i]),
                       model->fairness[i].name);
  for (unsigned j = 0; j < model->num_justice; ++j) {
    std::vector<unsigned> signals;
    for (unsigned selected : justice_selected[j])
      signals.push_back(rank_signal(selected));
    aiger_add_justice(aig, signals.size(), signals.data(),
                      model->justice[j].name);
  }
}

// Find an original safety violation or a genuine accepting loop in a finite
// monitor counterexample.  Inputs and original latches retain their indices.
bool counterexample(aiger *model,
                    const std::vector<std::vector<unsigned>> &candidate,
                    std::vector<std::vector<unsigned>> &cex,
                    unsigned &property_index, bool &is_justice) {
  std::vector<unsigned char> values(model->maxvar + 1, 0);
  auto value = [&](unsigned lit) { return values[IDX(lit)] ^ SGN(lit); };
  for (unsigned lit : candidate[0])
    if (IDX(lit) < values.size()) values[IDX(lit)] = STV(lit);
  auto state = [&]() {
    std::string key;
    key.reserve(model->num_latches);
    for (auto &l : latches(model))
      key.push_back(value(l.lit));
    return key;
  };
  std::unordered_map<std::string, unsigned> visited;
  visited.emplace(state(), 0);
  std::vector<std::vector<unsigned>> signals(model->num_justice);
  std::vector<std::vector<int>> last_seen(model->num_justice);
  for (unsigned j = 0; j < model->num_justice; ++j) {
    for (unsigned i = 0; i < model->num_fairness; ++i)
      signals[j].push_back(model->fairness[i].lit);
    for (unsigned i = 0; i < model->justice[j].size; ++i)
      signals[j].push_back(model->justice[j].lits[i]);
    last_seen[j].assign(signals[j].size(), -1);
  }
  auto finish = [&](unsigned frame) {
    cex.assign(candidate.begin(), candidate.begin() + frame + 1);
    cex[0].resize(model->num_latches);
    return true;
  };
  for (unsigned frame = 1; frame < candidate.size(); ++frame) {
    for (unsigned lit : candidate[frame])
      values[IDX(lit)] = STV(lit);
    for (auto &a : ands(model))
      values[IDX(a.lhs)] = value(a.rhs0) & value(a.rhs1);
    for (unsigned i = 0; i < model->num_bad; ++i)
      if (value(model->bad[i].lit)) {
        property_index = i;
        is_justice = false;
        return finish(frame);
      }
    for (unsigned j = 0; j < signals.size(); ++j)
      for (unsigned i = 0; i < signals[j].size(); ++i)
        if (value(signals[j][i])) last_seen[j][i] = frame - 1;
    std::vector<unsigned char> next_state;
    for (auto &l : latches(model))
      next_state.push_back(value(l.next));
    for (unsigned i = 0; i < model->num_latches; ++i)
      values[IDX(model->latches[i].lit)] = next_state[i];
    auto [position, inserted] = visited.emplace(state(), frame);
    if (inserted) continue;
    for (unsigned j = 0; j < signals.size(); ++j)
      if (std::all_of(last_seen[j].begin(), last_seen[j].end(), [&](int seen) {
            return seen >= static_cast<int>(position->second);
          })) {
        property_index = j;
        is_justice = true;
        return finish(frame);
      }
  }
  return false;
}

} // namespace

bool general_liveness(aiger *model, aiger *&witness,
                      std::vector<std::vector<unsigned>> &cex,
                      unsigned &property_index, bool &is_justice) {
  assert(model->num_justice);
  for (unsigned budget = 0;; budget = budget ? 2 * budget : 1) {
    L2 << "generalized liveness acceptance budget" << budget;
    Reduction reduction = reduce(model, budget);
    std::vector<std::vector<unsigned>> candidate;
    if (!ic3(reduction.aig, candidate)) {
      certificate(model, reduction);
      witness = reduction.aig;
      return false;
    }
    bool found =
        counterexample(model, candidate, cex, property_index, is_justice);
    aiger_reset(reduction.aig);
    if (found) return true;
    if (budget > std::numeric_limits<unsigned>::max() / 2)
      die("generalized liveness acceptance budget overflow");
  }
}
