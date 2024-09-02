#include "mcaiger.hpp"

#include "cadical.hpp"
#include "utils.hpp"

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

CaDiCaL::Solver *s;
static aiger *model;

// mcaiger
static int ionly, bonly;
static int acs, mix;
static int ncs, dcs, rcs;
static unsigned *frames, sframes, nframes;
static unsigned nrcs;

#define picosat_ado_conflicts(...) (0u)
#define picosat_disable_ado(...) \
  do {                           \
  } while (0)
#define picosat_enable_ado(...) \
  do {                          \
  } while (0)
#define picosat_add_ado_lit(...) \
  do {                           \
  } while (0)
#define picosat_set_ado_conflict_limit(...) \
  do {                                      \
  } while (0)

static int frame(int k) {
  int res;
  res = k * model->maxvar + 2;
  if (dcs || rcs || mix) res += model->num_latches * k * (k - 1) / 2;
  return res;
}

static int lit(unsigned k, unsigned l) {
  int res;
  assert(0 <= l && l <= 2 * model->maxvar + 1);
  res = (l <= 1) ? 1 : frame(k) + (int)((l - 2) / 2);
  if (l & 1) res = -res;
  return res;
}

static int input(unsigned k, unsigned i) {
  assert(0 <= i && i < model->num_inputs);
  return lit(k, model->inputs[i].lit);
}

static int latch(unsigned k, unsigned i) {
  assert(0 <= i && i < model->num_latches);
  return lit(k, model->latches[i].lit);
}

static int next(unsigned k, unsigned i) {
  assert(0 <= i && i < model->num_latches);
  return lit(k, model->latches[i].next);
}

static int reset(unsigned i) {
  assert(0 <= i && i < model->num_latches);
  return model->latches[i].reset;
}

static int output(unsigned k, unsigned i) {
  // using output from aiger.hpp can deal with a bit more
  assert(i == 0);
  // assert(0 <= i && i < model->num_outputs);
  return lit(k, output(model));
}

static int constraint(unsigned k, unsigned i) {
  assert(0 <= i && i < model->num_constraints);
  return lit(k, model->constraints[i].lit);
}

static void unary(int a) {
  assert(a);
  s->add(a);
  s->add(0);
}

static void binary(int a, int b) {
  assert(a);
  s->add(a);
  assert(b);
  s->add(b);
  s->add(0);
}

static void ternary(int a, int b, int c) {
  assert(a);
  s->add(a);
  assert(b);
  s->add(b);
  assert(c);
  s->add(c);
  s->add(0);
}

static void gate(int lhs, int rhs0, int rhs1) {
  binary(-lhs, rhs0);
  binary(-lhs, rhs1);
  ternary(lhs, -rhs0, -rhs1);
}

static void eq(int lhs, int rhs) {
  binary(-lhs, rhs);
  binary(lhs, -rhs);
}

static void connect(unsigned k) {
  unsigned i;

  if (!k) return;

  for (i = 0; i < model->num_latches; i++)
    eq(next(k - 1, i), latch(k, i));

  L3 << k << "connect";
}

static void encode(unsigned k) {
  aiger_and *a;
  aiger_symbol *c;
  unsigned i;

  if (!k) unary(lit(k, 1)); /* true */

  for (i = 0; i < model->num_ands; i++) {
    a = model->ands + i;
    gate(lit(k, a->lhs), lit(k, a->rhs0), lit(k, a->rhs1));
  }

  for (i = 0; i < model->num_constraints; i++) {
    unary(constraint(k, i));
  }

  if (k) {
    // TODO What is this? All-different-from-frame-0-constraint assuming zero
    // reset?
    if (0 && model->num_latches > 0) {
      for (i = 0; i < model->num_latches; i++)
        s->add(latch(k, i));

      s->add(0);
    }
    unary(-output(k - 1, 0));
  }
  L3 << k << "encode";
}

static void ado(unsigned k) {
  unsigned i;
  if (model->num_latches > 0) {
    for (i = 0; i < model->num_latches; i++)
      picosat_add_ado_lit(ps, latch(k, i));
    picosat_add_ado_lit(ps, 0);
  }
  L3 << k << "ado";
}

static int diff(int k, int l, int i) {
  assert(0 <= i && i < model->num_latches);
  assert(l < k);
  return frame(k + 1) - i - l * model->num_latches - 1;
}

static void diffs(unsigned k, unsigned l) {
  unsigned i, tmp;
  assert(k != l);
  if (l > k) {
    tmp = k;
    k = l;
    l = tmp;
  }
  for (i = 0; i < model->num_latches; i++) {
    ternary(latch(l, i), latch(k, i), -diff(k, l, i));
    ternary(-latch(l, i), -latch(k, i), -diff(k, l, i));
  }
  if (model->num_latches > 0) {
    for (i = 0; i < model->num_latches; i++)
      s->add(diff(k, l, i));
    s->add(0);
  }
  L3 << "diffs" << l << k;
}

static void diffsk(unsigned k) {
  unsigned l;
  if (!k) return;
  for (l = 0; l < k; l++)
    diffs(k, l);
  L3 << k << "diffsk";
}

static void simple(unsigned k) {
  if (dcs)
    diffsk(k);
  else if (acs)
    ado(k);
  else
    assert(rcs || ncs);
}

static void bad(unsigned k) {
  // using output from aiger.hpp can deal with a bit more
  // assert(model->num_outputs == 1);
  s->assume(output(k, 0));
  L3 << k << "bad";
}

static void init(unsigned k) {
  unsigned i;
  int l, r;

  if (bonly && k) return;

  for (i = 0; i < model->num_latches; i++) {
    r = reset(i);
    if (r > 1) continue; // uninitialized
    l = latch(0, i) * (r ? 1 : -1);
    if (bonly)
      unary(l);
    else
      s->assume(l);
  }

  L3 << k << "init";
}

static int cmp_frames(const void *p, const void *q) {
  unsigned k = *(unsigned *)p;
  unsigned l = *(unsigned *)q;
  int a, b, res;
  unsigned i;

  for (i = 0; i < model->num_latches; i++) {
    a = s->val(latch(k, i));
    b = s->val(latch(l, i));
    res = a - b;
    if (res) return res;
  }

  return 0;
}

static int sat(unsigned k) {
  unsigned i;
  int res;

  if (rcs || mix) {
    if (k == nframes) {
      assert(k == nframes);

      if (k >= sframes) {
        sframes = sframes ? 2 * sframes : 1;
        frames = (unsigned *)realloc(frames, sframes * sizeof frames[0]);
      }

      assert(nframes < sframes);
      frames[nframes++] = k;
    }

    assert(nframes == k + 1);
  }

RESTART:
  res = s->solve();

  if (res == 20) return res;

  if (res == 10 && !rcs) return res;

  if (!res) {
    assert(mix);
    assert(!rcs);
    assert(acs);
    rcs = 1;
    acs = 0;
    picosat_disable_ado(ps);
    goto RESTART;
  }

  assert(rcs);
  assert(res == 10);

  if (model->num_latches) {
    qsort(frames, k + 1, sizeof frames[0], cmp_frames);
    for (i = 0; i < k; i++)
      if (!cmp_frames(frames + i, frames + i + 1)) {
        diffs(frames[i], frames[i + 1]);
        nrcs++;
        bad(k);
        goto RESTART;
      }

    assert(i == k); /* all different */
  }

  return 10;
}

static int step(unsigned k) {
  int res;
  if (mix && acs)
    picosat_set_ado_conflict_limit(ps, picosat_ado_conflicts(ps) + 1000);
  bad(k);
  L2 << k << "step";
  res = (sat(k) == 20);

  return res;
}

static int base(unsigned k) {
  int res;
  if (acs) picosat_disable_ado(ps);
  init(k);
  bad(k);
  L2 << k << "base";
  res = (sat(k) == 10);
  if (acs) picosat_enable_ado(ps);
  return res;
}

void mcaiger_free() { delete s; }

std::pair<bool, int> mcaiger(aiger *aig, unsigned simple_path) {
  const char *name = 0, *err;
  unsigned k, maxk = UINT_MAX;
  int i, cs;
  double delta;
  bool bug{};
  s = new CaDiCaL::Solver();
  if (simple_path == 0)
    ncs = 1;
  else if (simple_path == 1)
    dcs = 1;
  else if (simple_path == 2)
    rcs = 1;
  else
    assert(false);
  model = aig;
  for (k = 0; k <= maxk; k++) {
    if (mix && acs && picosat_ado_conflicts(ps) >= 10000) {
      acs = 0;
      rcs = 1;
      picosat_disable_ado(ps);
    }
    connect(k);
    encode(k);
    simple(k);
    if (step(k)) {
      L1 << k << "inductive";
      bug = false;
      break;
    }
    if (base(k)) {
      L1 << k << "reachable";
      bug = true;
      break;
    }
  }
  if (rcs || mix) { L2 << nrcs << "refinements of simple path constraints"; }
  return {bug, k};
}

void stimulus(int k, std::vector<std::vector<unsigned>> &cex) {
  assert(s->status() == 10);
  assert(cex.empty());
  cex.reserve(k + 2);
  unsigned i, j;
  int lit, l, v;
  std::vector<unsigned> frame;
  for (i = 0; i < model->num_latches; i++) {
    lit = latch(0, i);
    v = s->val(lit) < 0;
    l = model->latches[i].lit;
    frame.push_back(l + v);
  }
  cex.push_back(frame);
  for (i = 0; i <= k; i++) {
    frame.clear();
    for (j = 0; j < model->num_inputs; j++) {
      lit = input(i, j);
      LV5(i, j, l, s->val(lit));
      v = s->val(lit) < 0;
      l = model->inputs[j].lit;
      frame.push_back(l + v);
    }
    cex.push_back(frame);
  }
  for (auto &c : cex)
    L5 << c;
}
