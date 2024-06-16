#pragma once

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>

#define INF INT_MAX

// clang-format off
// options have to be sorted!
// <let ((beg (progn (next-line 2) (bol))) (end (progn (forward-paragraph) (point)))) (shell-command-on-region beg end "sort -k 2" t t) (align-regexp beg end "\\(,\\s-*\\) " 1 1 t)>
//                 Name   Def Min Max Description
#define OPTIONS \
  LOGOPT(unsigned, verbosity, 2, 0, 5, "verbosity level")

// clang-format on

#if defined(LOG)
#define LOGOPT OPTION
#else
#define LOGOPT(...) /**/
#endif

#ifndef NDEBUG
#define DBGOPT OPTION
#else
#define DBGOPT(...) /**/
#endif

struct file {
  bool close;
  FILE *file;
  const char *path;
};

struct options {
  unsigned seconds;
  unsigned optimize;
  bool summarize;

#define OPTION(TYPE, NAME, DEFAULT, MIN, MAX, DESCRIPTION) TYPE NAME;
  OPTIONS
#undef OPTION
  struct file model;
  struct file witness;
};

/*------------------------------------------------------------------------*/

void parse_options(int argc, char **argv, struct options *);
const char *match_and_find_option_argument(const char *, const char *);
bool parse_option_with_value(struct options *, const char *);
void report_non_default_options(struct options *);
