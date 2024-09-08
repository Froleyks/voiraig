#include "ic3.hpp"

#include "aiger.hpp"
#include "cadical.hpp"
#include "ternary.hpp"
#include "utils.hpp"

#include <algorithm>
#include <limits>

typedef std::vector<unsigned> Cube;
void gate(CaDiCaL::Solver *frame, int a, int x, int y) {
  assert(a);
  frame->add(-SAT(a));
  frame->add(SAT(x));
  frame->add(0);
  frame->add(-SAT(a));
  frame->add(SAT(y));
  frame->add(0);
  frame->add(SAT(a));
  frame->add(-SAT(x));
  frame->add(-SAT(y));
  frame->add(0);
}

void gates(aiger *model, CaDiCaL::Solver *frame) {
  for (size_t i = 0; i < model->num_ands; ++i) {
    aiger_and *a = model->ands + i;
    gate(frame, a->lhs, a->rhs0, a->rhs1);
  }
}

void reset(aiger *model, CaDiCaL::Solver *frame) {
  L3 << "enforcing reset in" << frame;
  for (size_t i = 0; i < model->num_latches; ++i) {
    const aiger_symbol *latch = model->latches + i;
    const unsigned gate = latch->lit;
    const unsigned reset = latch->reset;
    if (gate == reset) continue;
    const int lit = SAT(gate);
    assert(gate ^ 1u);
    assert(lit);
    if (reset == 0) {
      frame->add(-lit);
      frame->add(0);
    } else if (reset == 1) {
      frame->add(lit);
      frame->add(0);
    } else {
      const int rLit = SAT(reset);
      frame->add(-rLit);
      frame->add(lit);
      frame->add(0);
      frame->add(rLit);
      frame->add(-lit);
      frame->add(0);
    }
  }
}

void initialize(aiger *model, CaDiCaL::Solver *frame) {
  frame->add(SAT(1));
  frame->add(0);
  gates(model, frame);
}

Cube cube(aiger *model, CaDiCaL::Solver *frame) {
  // TODO minimize via ternary
  Cube cube;
  for (unsigned i = 0; i < model->num_latches; ++i) {
    const aiger_symbol *latch = model->latches + i;
    const unsigned gate = latch->lit;
    const int lit = SAT(gate);
    const int val = frame->val(lit);
    assert(gate ^ 1u);
    assert(lit);
    cube.push_back(gate | (val < 0));
  }
  assert(std::is_sorted(cube.begin(), cube.end()));
  return cube;
}

Cube inputCube(aiger *model, CaDiCaL::Solver *frame) {
  // TODO minimize via ternary
  Cube cube;
  for (unsigned i = 0; i < model->num_inputs; ++i) {
    const aiger_symbol *input = model->inputs + i;
    const unsigned gate = input->lit;
    const int lit = SAT(gate);
    const int val = frame->val(lit);
    assert(gate ^ 1u);
    assert(lit);
    cube.push_back(gate | (val < 0));
  }
  assert(std::is_sorted(cube.begin(), cube.end()));
  return cube;
}

static const Cube bot{0};
static const Cube top{1};

// TODO move to header
class Frame {
public:
  // TODO clauses should also be added here
  // TODO optional: add activation literal to be able to identify F_inf cubes
  // TODO reduce derefs
  // Encoded as size, followed by the literals.
  // (n, l0, ..., ln), (n', l0', ..., ln')
  // -n for deleted cubes
  // Each cube is also blocked in all previous frames.
  std::vector<Cube> cubes;
  unsigned B;
  unsigned C;
  CaDiCaL::Solver *solver;
  Frame(aiger *model) {
    assert(model);
    solver = new CaDiCaL::Solver();
    // TODO only on demand
    B = output(model);
    C = conj(model, constraints(model) | lits);
    initialize(model, solver);
  }
  bool intersects(const Cube &c) {
    // TODO do I need to minimize here?
    for (unsigned g : c)
      solver->assume(SAT(g));
    const bool res{solver->solve() == 10};
    return res;
  }
};

bool subsumes(const Cube &small, const Cube &big) {
  // TODO use watch list based forward (?) subsumption?
  auto s = small.begin(), b = big.begin();
  const auto S = small.end(), B = big.end();
  while (s != S && b != B)
    s += (*s == *b++);
  return s == S;
}

void addBlockedCube(std::vector<Frame> &frames, const Cube c, unsigned k) {
  assert(k < frames.size());
  L3 << "adding to" << k << c;
  for (unsigned d = 1; d <= k; ++d) {
    std::vector<Cube> &cubes = frames[d].cubes;
    for (unsigned i = 0; i < cubes.size();) {
      if (subsumes(c, cubes[i])) {
        L3 << "subsumes at" << d << cubes[i];
        cubes[i] = std::move(cubes.back());
        cubes.pop_back();
      } else
        i++;
    }
  }
  frames[k].cubes.push_back(c);
}

Cube bad(aiger *model, Frame &f, bool minimize = true) {
  L3 << "searching for bad";
  f.solver->assume(SAT(f.B));
  f.solver->assume(SAT(f.C));
  const int res = f.solver->solve();
  if (res == 20) return bot;
  assert(res == 10);
  if (!minimize) return cube(model, f.solver);
  std::vector<ternary> s(model->maxvar + 1);
  assert(aiger_is_reencoded(model));
  for (unsigned i = 0; i < model->num_inputs + model->num_latches + 1; ++i)
    s[i] = f.solver->val(i + 1) > 0 ? X1 : X0;
#ifndef NDEBUG
  L3 << "sanity check simulation";
  propagate(model->ands, model->num_ands, s);
  assert(s[IDX(f.B)] == STX(f.B));
#endif

  Cube b = reduce(model, {f.B}, s);

  L3 << "return" << b;
#ifndef NDEBUG
  const Cube fullB = cube(model, f.solver);
  for (auto &l : b)
    assert(std::find(fullB.begin(), fullB.end(), l) != fullB.end());
#endif
  return b;
}

template <bool constrain = true>
Cube predecessor(aiger *model, Frame &f, Cube &b, Frame &f0, bool minA = true) {
  // TODO if cadical only reconstructs the model on val, it might be benefical
  // to split the return of a from the SAT query.
  std::vector<unsigned> bNext;
  bNext.reserve(b.size());
  for (unsigned g : b) {
    const int i = (g - model->latches[0].lit) >> 1;
    const aiger_symbol *latch = model->latches + i;
    assert(latch->lit == (g & ~1u));
    const unsigned lit = latch->next ^ (g & 1u);
    // if lit == 0
    // TODO should work, seems to loop
    // if (!lit) {
    //   b = {g};
    //   return bot;
    // }
    // if (lit == 1) continue;
    // TODO bNext only in SAT case
    if (lit > 1) bNext.push_back(lit);
    const int sat = SAT(lit);
    f.solver->assume(sat);
    if (constrain) f.solver->constrain(SAT(NOT(g)));
  }
  f.solver->assume(SAT(f.C));
  if (constrain && b.size()) f.solver->constrain(0);
  assert(bNext.size() <= model->num_latches);
  const int res = f.solver->solve();
  if (res == 20) {
    L3 << "no prdecessor for" << b;
    const Cube save = b;
    // TODO use bNext
    b.erase(std::remove_if(b.begin(), b.end(),
                           [&f, model](unsigned g) {
                             // TODO take care not to intersect inital states
                             // TODO OR perform witness backwarding at the end!
                             const int i = (g - model->latches[0].lit) >> 1;
                             const aiger_symbol *latch = model->latches + i;
                             assert(latch->lit == (g & ~1u));
                             const int lit =
                                 SAT(latch->next) * (g & 1u ? -1 : 1);
                             const bool failed = f.solver->failed(lit);
                             // L3 << "next latch" << latch->lit
                             //     << (failed ? "failed" : "not failed");
                             return !failed;
                           }),
            b.end());

    // TODO be a bit more conservative here
    if (f0.intersects(b)) b = std::move(save);
    L3 << "shrunk b to" << b;
    return bot;
  }
  assert(res == 10);
  L3 << "found predecessor" << cube(model, f.solver) << "of" << b;
  if (!minA) return cube(model, f.solver);
  // TODO use global ternary state

  std::vector<ternary> s(model->maxvar + 1);
  assert(aiger_is_reencoded(model));
  // assert(a.size() == model->num_latches);
  for (unsigned i = 0; i < model->num_inputs + model->num_latches + 1; ++i)
    s[i] = f.solver->val(i + 1) > 0 ? X1 : X0;
  assert(s[0] == X0);
#ifndef NDEBUG
  L3 << "a" << cube(model, f.solver) << "b" << b << "/" << bNext;
  L3 << "sanity check simulation";
  propagate(model->ands, model->num_ands, s);
  // L3 << "b" << b << "to" << bNext;
  // L3 << cube(model, f.solver);
  assert(std::all_of(bNext.begin(), bNext.end(),
                     [&s](auto l) { return s[IDX(l)] == STX(l); }));
#endif
  // her
  Cube a = reduce(model, bNext, s);
  L3 << "return" << a;
#ifndef NDEBUG
  const Cube fullB = cube(model, f.solver);
  for (auto &l : a)
    assert(std::find(fullB.begin(), fullB.end(), l) != fullB.end());
#endif
  return a;
}

void generalize(aiger *model, Frame &f, Frame &f0, std::vector<unsigned> &b) {
  L3 << "generalizing" << b;
  std::vector<unsigned> c{b};
  unsigned d = std::numeric_limits<unsigned>::max();
  bool covered = true;
  for (int i = c.size(); i--;) {
    if (covered) {
      i = std::min((size_t)i, c.size() - 1);
      while (c[i] > d)
        if (!i--) break;
      if (i < 0) return;
      d = c[i];
      c.erase(c.begin() + i);
    } else {
      assert(c[i] < d);
      std::swap(d, c[i]);
    }
    // TODO another expensive reset intersection
    if ((covered = (!f0.intersects(c) &&
                    bot == predecessor(model, f, c, f0, false)))) {
      L3 << "reduced to" << c;
      b = c;
    }
  }
}

int forwardCubes(aiger *model, std::vector<Frame> &frames) {
  L3 << "forwarding";
  for (unsigned k = 1; k < frames.size() - 1; k++) {
    // TODO use consecutive representation with mark/delete
    // instead of doing unecessary work by coping here, because addBlocked
    // modifies
    auto cubes = frames[k].cubes;
    L3 << "testing cubes at" << k;
    for (auto &c : cubes)
      L3 << c;
    for (auto &b : cubes) {
      L3 << "consider at " << k << b;
      // TODO shrinking the cube here needs carful consideration
      Cube a = predecessor<false>(model, frames[k], b, frames[0]);
      if (a == bot) {
        for (unsigned g : b)
          frames[k + 1].solver->add(SAT(NOT(g)));
        frames[k + 1].solver->add(0);
        addBlockedCube(frames, b, k + 1);
      }
    }
    if (frames[k].cubes.empty()) {
      L3 << "found empty frame" << k;
      // TODO should I continue the propagation?
      // TODO can an other frame be appended to generate smaller invariants?

      L3 << "final frames";
      for (unsigned i = 1; i < frames.size(); ++i) {
        L3 << "frame" << i;
        for (auto &c : frames[i].cubes) {
          L3 << c;
        }
      }

      return k + 1;
    }
  }
  L3 << "Finished forwarding";
  for (unsigned i = 1; i < frames.size(); ++i) {
    L3 << "frame" << i;
    for (auto &c : frames[i].cubes) {
      L3 << c;
    }
  }

  return 0;
}

bool ic3(aiger *model, std::vector<std::vector<unsigned>> &cex) {
  std::vector<Frame> frames;
  L2 << "appending frame" << frames.size();
  frames.emplace_back(model);
  reset(model, frames[0].solver);
  while (true) {
    Cube b = bad(model, frames.back(), frames.size() > 1);
    if (b == bot) {
      const int converged = forwardCubes(model, frames);
      if (converged) {
        L3 << "Proven safety at" << frames.size() - 1;
        // cubes.insert(cubes.end(), frames.rbegin()[1].cubes.begin(),
        // frames.rbegin()[1].cubes.end());
        unsigned badCubes = 0;
        for (unsigned i = converged; i < frames.size(); ++i)
          badCubes += frames[i].cubes.size();
        std::vector<unsigned> bs;
        bs.reserve(badCubes);
        for (unsigned i = converged; i < frames.size(); ++i)
          for (auto &c : frames[i].cubes)
            bs.push_back(conj(model, c));
        if (model->num_bad)
          model->bad->lit = disj(model, bs);
        else if (model->num_outputs)
          model->outputs->lit = disj(model, bs);
        else
          aiger_add_output(model, disj(model, bs), "");
        // TODO move this to uniqueptr
        for (auto &f : frames)
          delete f.solver;
        return false;
      }
      L2 << "appending frame" << frames.size();
      frames.emplace_back(model);
      continue;
    }
    std::vector<Cube> obligations{b},
        inputs{inputCube(model, frames.back().solver)};
    L3 << "found bad" << b << "at frame" << frames.size() - 1;
    while (obligations.size()) {
      const size_t k = frames.size() - obligations.size();
      if (!k) {
        L3 << "found CEX";
        cex.reserve(inputs.size() + 1);
        cex.emplace_back(std::move(obligations.back()));
        for (int i = inputs.size(); i--;)
          cex.emplace_back(std::move(inputs[i]));
        for (auto &f : frames)
          delete f.solver;
        return true;
      }
      Cube &b = obligations.back();
      // TODO (isBlocked(b)) obligations.pop(), continue;
      L3 << "checking for predecessor of" << b << "in" << k - 1;
      assert(k > 0);
      Cube a = predecessor(model, frames[k - 1], b, frames[0], k > 1);
      if (a == bot) {
        if (k > 1) generalize(model, frames[k - 1], frames[0], b);
        L3 << "block cube" << b << "in" << k;
        // TODO should we do subsumption over all Frames here?
        // TODO should I really add weaker clauses to previous frames?
        for (unsigned j = 1; j <= k; ++j) {
          for (unsigned g : b)
            frames[j].solver->add(SAT(NOT(g)));
          frames[j].solver->add(0);
        }
        addBlockedCube(frames, b, k);
        obligations.pop_back();
        if (inputs.size()) // last obligation popped so no input
          inputs.pop_back();
      } else {
        L3 << "adding obligation" << a << "to" << k - 1;
        obligations.push_back(std::move(a));
        inputs.push_back(inputCube(model, frames[k - 1].solver));
      }
    }
  }
  return 0;
}
