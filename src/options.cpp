#include "options.hpp"

#include "banner.hpp"
#include "utils.hpp"

#include <ctype.h>
#include <string.h>

#include "usage.hpp"

static void print_usage_of_generic_options(void);

static bool has_suffix(const char *str, const char *suffix) {
  size_t len = strlen(str);
  size_t suffix_len = strlen(suffix);
  if (len < suffix_len) return 0;
  return !strcmp(str + len - suffix_len, suffix);
}

static bool is_positive_number_string(const char *arg) {
  const char *p = arg;
  int ch;
  if (!(ch = *p++)) return false;
  if (!isdigit(ch)) return false;
  while ((ch = *p++))
    if (!isdigit(ch)) return false;
  return true;
}

static bool is_number_string(const char *arg) {
  return is_positive_number_string(arg + (*arg == '-'));
}

const char *match_and_find_option_argument(const char *arg, const char *match) {
  if (arg[0] != '-') return 0;
  if (arg[1] != '-') return 0;
  const char *p = arg + 2;
  int mch;
  for (const char *q = match; (mch = *q); q++, p++) {
    int ach = *p;
    if (ach == mch) continue;
    if (mch != '_') return 0;
    if (ach == '-') continue;
    p--;
  }
  if (*p++ != '=') return 0;
  if (!strcmp(p, "false")) return p;
  if (!strcmp(p, "true")) return p;
  return is_number_string(p) ? p : 0;
}

void initialize_options(struct options *opts) {
  memset(opts, 0, sizeof *opts);
#define OPTION(TYPE, NAME, DEFAULT, MIN, MAX, DESCRIPTION) \
  ass((TYPE)MIN <= (TYPE)MAX);                             \
  opts->NAME = (TYPE)DEFAULT;
  OPTIONS
#undef OPTION
}

static bool parse_option(const char *opt, const char *name) {
  const char *o = opt, *n = name;
  char och;
  while ((och = *o++)) {
    int nch = *n++;
    if (och == nch) continue;
    if (nch != '_') return false;
    if (och == '-') continue;
    o--;
  }
  return !*n;
}

static bool parse_bool_option_value(const char *opt, const char *str,
                                    bool *value_ptr, bool min_value,
                                    bool max_value) {
  (void)min_value, (void)max_value;
  const char *arg = match_and_find_option_argument(opt, str);
  if (!arg) return false;
  if (!strcmp(arg, "0") || !strcmp(arg, "false"))
    *value_ptr = false;
  else if (!strcmp(arg, "1") || !strcmp(arg, "true"))
    *value_ptr = true;
  else
    return false;
  return true;
}

static bool parse_unsigned_option_value(const char *opt, const char *str,
                                        unsigned *value_ptr, unsigned min_value,
                                        unsigned max_value) {
  const char *arg = match_and_find_option_argument(opt, str);
  if (!arg) return false;
  unsigned tmp;
  if (sscanf(arg, "%u", &tmp) != 1) return false;
  if (tmp < min_value) return false;
  if (tmp > max_value) return false;
  *value_ptr = tmp;
  return true;
}

bool parse_option_with_value(struct options *options, const char *str) {
#define OPTION(TYPE, NAME, DEFAULT, MIN, MAX, DESCRIPTION)               \
  if (parse_##TYPE##_option_value(str, #NAME, &options->NAME, MIN, MAX)) \
    return true;
  OPTIONS
#undef OPTION
  return false;
}

static void print_embedded_options(void) {
#define OPTION(TYPE, NAME, DEFAULT, MIN, MAX, DESCRIPTION) \
  printf("c --%s=%d\n", #NAME, (int)DEFAULT);
  OPTIONS
#undef OPTION
}

static void print_option_ranges(void) {
#define OPTION(TYPE, NAME, DEFAULT, MIN, MAX, DESCRIPTION) \
  printf("%s %d %d %d\n", #NAME, (int)DEFAULT, (int)MIN, (int)MAX);
  OPTIONS
#undef OPTION
}

void parse_options(int argc, char **argv, struct options *opts) {
  initialize_options(opts);
  for (int i = 1; i != argc; i++) {
    const char *opt = argv[i], *arg;
    if (!strcmp(opt, "-h") || !strcmp(opt, "--help")) {
      printf("\nOptions:\n");
      print_usage_of_generic_options();
      printf("\n");
      // clang-format off
	      fputs(
"which can also be used in the form '--<name>' and '--no-<name>'\n"
"for '<bool>' options instead of '--<name>=true' or '--<name>=false'\n"
"where 'true' / 'false' can be replaced by '1' / '0' as well.\n",
		stdout);
      // clang-format on

      exit(0);
    } else if (!strcmp(opt, "-i") || !strcmp(opt, "--id")) {
      print_id();
      exit(0);
    } else if (!strcmp(opt, "-V") || !strcmp(opt, "--version")) {
      print_version();
      exit(0);
    } else if ((arg = match_and_find_option_argument(opt, "time"))) {
      if (opts->seconds)
        die("multiple '--time=%u' and '%s'", opts->seconds, opt);
      if (sscanf(arg, "%u", &opts->seconds) != 1)
        die("invalid argument in '%s'", opt);
      if (!opts->seconds) die("invalid zero argument in '%s'", opt);
    }
#define OPTION(TYPE, NAME, DEFAULT, MIN, MAX, DESCRIPTION)                     \
  else if (opt[0] == '-' && opt[1] == '-' && opt[2] == 'n' && opt[3] == 'o' && \
           opt[4] == '-' && parse_option(opt + 5, #NAME)) opts->NAME = false;
    OPTIONS
#undef OPTION
#define OPTION(TYPE, NAME, DEFAULT, MIN, MAX, DESCRIPTION)             \
  else if (opt[0] == '-' && opt[1] == '-' && !strcmp(#TYPE, "bool") && \
           parse_option(opt + 2, #NAME)) opts->NAME = true;
    OPTIONS
#undef OPTION
    else if (parse_option_with_value(opts, opt));
    else if (!strcmp(opt, "--embedded")) print_embedded_options(), exit(0);
    else if (!strcmp(opt, "--range")) print_option_ranges(), exit(0);
    else if (opt[0] == '-' && opt[1])
        die("invalid option '%s' (try '-h')", opt);
    else if (opts->witness.file) die("too many arguments");
    else if (opts->model.file) {
      if (!strcmp(opt, "-")) {
        opts->witness.path = "";
        opts->witness.file = stdout;
      } else if (!(opts->witness.file = fopen(opt, "w")))
        die("can not open and write to '%s'", opt);
      else {
        opts->witness.path = opt;
        opts->witness.close = true;
      }
    }
    else {
      if (!strcmp(opt, "-")) {
        opts->model.path = "<stdin>";
        opts->model.file = stdin;
      } else if (has_suffix(opt, ".bz2") || has_suffix(opt, ".gz") ||
                 has_suffix(opt, ".xz")) {
        die("can not handle compressed file '%s'", opt);
      } else {
        opts->model.file = fopen(opt, "r");
        opts->model.close = true;
      }
      if (!opts->model.file) die("can not open and read from '%s'", opt);
      opts->model.path = opt;
    }
  }

  if (!opts->model.file) {
    opts->model.path = "<stdin>";
    opts->model.file = stdin;
  }
}

static const char *bool_to_string(bool value) {
  return value ? "true" : "false";
}

// TODO report non default options
[[maybe_unused]] static void
report_non_default_bool_option(const char *name, bool actual_value,
                               bool default_value) {
  ass(actual_value != default_value);
  const char *actual_string = bool_to_string(actual_value);
  const char *default_string = bool_to_string(default_value);
  printf("c non-default option '--%s=%s' (default '--%s=%s')\n", name,
         actual_string, name, default_string);
}

static void unsigned_to_string(unsigned value, char *res) {
  sprintf(res, "%u", value);
}

[[maybe_unused]] static void
report_non_default_unsigned_option(const char *name, unsigned actual_value,
                                   unsigned default_value) {
  ass(actual_value != default_value);
  char actual_string[32];
  char default_string[32];
  unsigned_to_string(actual_value, actual_string);
  unsigned_to_string(default_value, default_string);
  ass(strlen(actual_string) < sizeof actual_string);
  ass(strlen(default_string) < sizeof default_string);
  printf("c non-default option '--%s=%s' (default '--%s=%s')\n", name,
         actual_string, name, default_string);
}

void report_non_default_options(struct options *options) {
#ifdef LOG

  unsigned reported = 0;
#define OPTION(TYPE, NAME, DEFAULT, MIN, MAX, DESCRIPTION)                   \
  do {                                                                       \
    if (options->NAME == (TYPE)DEFAULT) break;                               \
    if (!reported++) fputs("c\n", stdout);                                   \
    report_non_default_##TYPE##_option(#NAME, options->NAME, (TYPE)DEFAULT); \
  } while (0);
  OPTIONS
#undef OPTION
#else
  (void)options;
#endif // LOG
}

static void print_usage_of_generic_options(void) {
  char buffer[80];
#define OPTION(TYPE, NAME, DEFAULT, MIN, MAX, DESCRIPTION)           \
  do {                                                               \
    (void)strcpy(buffer, "  --");                                    \
    char *b = buffer + strlen(buffer);                               \
    for (const char *p = #NAME; *p; p++)                             \
      *b++ = (*p == '_') ? '-' : *p;                                 \
    *b++ = '=';                                                      \
    if (!strcmp(#TYPE, "bool"))                                      \
      strcpy(b, "<bool>");                                           \
    else {                                                           \
      if ((int)MAX != (int)INF)                                      \
        sprintf(b, "%u..%u", (unsigned)MIN, (unsigned)MAX);          \
      else                                                           \
        sprintf(b, "%u...", (unsigned)MIN);                          \
    }                                                                \
    size_t len = strlen(buffer);                                     \
    ass(len < sizeof buffer);                                        \
    fputs(buffer, stdout);                                           \
    while (len++ < 32)                                               \
      fputc(' ', stdout);                                            \
    fputs(DESCRIPTION, stdout);                                      \
    if (!strcmp(#TYPE, "bool"))                                      \
      printf(" (default '%s')", (bool)(DEFAULT) ? "true" : "false"); \
    else                                                             \
      printf(" (default '%u')", (unsigned)DEFAULT);                  \
    fputc('\n', stdout);                                             \
  } while (0);
  OPTIONS
#undef OPTION
}
