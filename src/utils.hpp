#pragma once

#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <signal.h>
#include <string>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

inline void die(const char *fmt, ...) {
  fputs("error: ", stderr);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
#ifndef LOG
  fputs("Configure with cmake -DLOG=ON for more information\n", stderr);
#endif // LOG
  fflush(stderr);
  exit(1);
}

template <typename T>
std::ostream &operator<<(std::ostream &out, const std::vector<T> &v) {
  out << "[";
  copy(v.begin(), v.end(), std::ostream_iterator<T>(out, " "));
  if (v.size()) out << "\b";
  out << "]";
  return out;
}

template <typename T1, typename T2>
std::ostream &operator<<(std::ostream &out, const std::pair<T1, T2> &p) {
  out << "(" << p.first << ", " << p.second << ")";
  return out;
}

// Base case for the recursive call
template <std::size_t N, typename... T>
typename std::enable_if<(N >= sizeof...(T)), void>::type
print_tuple(std::ostream &out, const std::tuple<T...> &tup) {}

// Recursive case
template <std::size_t N, typename... T>
typename std::enable_if<(N < sizeof...(T)), void>::type
print_tuple(std::ostream &out, const std::tuple<T...> &tup) {
  if (N != 0) out << ", ";
  out << std::get<N>(tup);
  print_tuple<N + 1>(out, tup);
}

// Operator<< overload for tuple
template <typename... T>
std::ostream &operator<<(std::ostream &out, const std::tuple<T...> &tup) {
  out << "(";
  print_tuple<0>(out, tup);
  out << ")";
  return out;
}

namespace Logging {
#ifdef LOG
constexpr bool DISABLED = false;
#else
constexpr bool DISABLED = true;
#endif // LOG

inline unsigned &verbose() {
  static unsigned verbose = 4;
  return verbose;
}

inline bool &use_location() {
  static bool use_location = false;
  return use_location;
}

inline double totalTime() {
  double res;
  struct rusage u;
  if (getrusage(RUSAGE_SELF, &u)) return 0;
  res = u.ru_utime.tv_sec + 1e-6 * u.ru_utime.tv_usec;  // user time
  res += u.ru_stime.tv_sec + 1e-6 * u.ru_stime.tv_usec; // + system time
  return res;
}

inline double relative(double a, double b) { return b ? a / b : 0; }
inline double percent(double a, double b) { return relative(100 * a, b); }

struct Logger {
  std::ostringstream os;
  bool empty = true;
  bool print = true;
  Logger(const std::string_view file, const int line) {
    assert(line);
    if (use_location())
      os << std::setw(10) << std::fixed << file << ":" << line << ":";
    else
      os << std::setw(13) << std::fixed << std::setprecision(6) << totalTime()
         << ":";
  }
  ~Logger() {
    if (print) std::cout << (empty ? "" : os.str()) << std::endl;
  }
  template <class T> Logger &operator<<(T const &rhs) {
    empty = false;
    os << " " << rhs;
    return *this;
  }
};

struct Timer {
  Logger logger;
  unsigned level;
  const double start;
  double *accumulator; // Pointer to an external double variable
  std::string_view accumulator_name;
  Timer(const std::string_view file, const int line, unsigned level,
        double &accumulator, const std::string_view accumulator_name)
      : logger(file, line), level(level), start(totalTime()),
        accumulator(&accumulator), accumulator_name(accumulator_name) {}
  Timer(const std::string_view file, const int line, unsigned level)
      : logger(file, line), level(level), start(totalTime()), accumulator(0) {}
  ~Timer() {
    auto end = totalTime();
    double duration = end - start;
    if (accumulator) *accumulator += duration;
    if (verbose() >= level) {
      logger << "Timer =" << duration << "seconds" << std::setprecision(2)
             << percent(duration, end) << "\b% of current total";
      if (accumulator)
        logger << "\n"
               << std::setw(13) << std::setprecision(6) << accumulator_name
               << "increased =" << *accumulator << "seconds"
               << std::setprecision(2) << percent(duration, end)
               << "\b% of current total";
    } else
      logger.print = false;
  }
  template <class T> Timer &operator<<(T const &rhs) {
    logger << rhs;
    return *this;
  }
};

template <class... Ts>
void log_values(const std::string_view file, const int line, std::string names,
                Ts &&...vars) {
  std::stringstream ss(names);
  std::string name;
  (
      [&] {
        std::getline(ss, name, ',');
        if (name[0] == ' ') name = name.erase(0, 1);
        Logger(file, line) << name << "=" << vars;
      }(),
      ...);
}

} // namespace Logging

#define L0 std::cout << "Voiraig: "

#ifdef LOG

// Logging code
#define LX(...) __VA_ARGS__

#define LI1(CONDITION)              \
  if (!(CONDITION))                 \
    ;                               \
  else if (Logging::verbose() >= 1) \
  Logging::Logger(__FILE__, __LINE__)
#define LV1(...)                                                          \
  do {                                                                    \
    if (Logging::verbose() >= 1)                                          \
      Logging::log_values(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__); \
  } while (0)

#define LI2(CONDITION)              \
  if (!(CONDITION))                 \
    ;                               \
  else if (Logging::verbose() >= 2) \
  Logging::Logger(__FILE__, __LINE__)
#define LV2(...)                                                          \
  do {                                                                    \
    if (Logging::verbose() >= 2)                                          \
      Logging::log_values(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__); \
  } while (0)

#define LI3(CONDITION)              \
  if (!(CONDITION))                 \
    ;                               \
  else if (Logging::verbose() >= 3) \
  Logging::Logger(__FILE__, __LINE__)
#define LV3(...)                                                          \
  do {                                                                    \
    if (Logging::verbose() >= 3)                                          \
      Logging::log_values(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__); \
  } while (0)

#define LI4(CONDITION)              \
  if (!(CONDITION))                 \
    ;                               \
  else if (Logging::verbose() >= 4) \
  Logging::Logger(__FILE__, __LINE__)
#define LV4(...)                                                          \
  do {                                                                    \
    if (Logging::verbose() >= 4)                                          \
      Logging::log_values(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__); \
  } while (0)

#define LI5(CONDITION)              \
  if (!(CONDITION))                 \
    ;                               \
  else if (Logging::verbose() >= 5) \
  Logging::Logger(__FILE__, __LINE__)
#define LV5(...)                                                          \
  do {                                                                    \
    if (Logging::verbose() >= 5)                                          \
      Logging::log_values(__FILE__, __LINE__, #__VA_ARGS__, __VA_ARGS__); \
  } while (0)

#define TIME(LEVEL)                                      \
  Logging::Timer LOCAL_TIMER(__FILE__, __LINE__, LEVEL); \
  LOCAL_TIMER

#define ADD_TIME(LEVEL, TIMER)                                         \
  Logging::Timer LOCAL_TIMER_##TIMER(__FILE__, __LINE__, LEVEL, TIMER, \
                                     #TIMER);                          \
  LOCAL_TIMER_##TIMER

#else // Default

#define LX(...) \
  do {          \
  } while (0)

#define DUMMY_STREAM \
  if (true)          \
    ;                \
  else               \
    L0

#define LI1(CONDITION) DUMMY_STREAM
#define LV1(...) LX
#define LI2(CONDITION) DUMMY_STREAM
#define LV2(...) LX
#define LI3(CONDITION) DUMMY_STREAM
#define LV3(...) LX
#define LI4(CONDITION) DUMMY_STREAM
#define LV4(...) LX
#define LI5(CONDITION) DUMMY_STREAM
#define LV5(...) LX
#define TIME(LEVEL) DUMMY_STREAM
#define ADD_TIME(LEVEL, TIMER) DUMMY_STREAM

#endif // LOG

#define L1 LI1(true)
#define L2 LI2(true)
#define L3 LI3(true)
#define L4 LI4(true)
#define L5 LI5(true)

#define LIB if constexpr (!Logging::DISABLED)

inline int cmd(const std::string &cmd, std::string &output) {
  L5 << "Running command: " << cmd;
  std::array<char, 128> buffer;
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) throw std::runtime_error("popen() failed!");
  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
    output += buffer.data();
  return WEXITSTATUS(pclose(pipe));
}

inline int cmd(const std::string &cmd) {
  L5 << "Running command: " << cmd;
  std::string silent = cmd + " > /dev/null";
  return WEXITSTATUS(system(silent.c_str()));
}

inline std::string tempFile() {
  static std::vector<std::string> tempFiles;
  if (tempFiles.empty()) {
    auto cleanup = []() {
      for (const auto &file : tempFiles)
        std::remove(file.c_str());
    };
    auto cleanupOnSig = [](int signum) {
      for (const auto &file : tempFiles)
        std::remove(file.c_str());

      signal(signum, SIG_DFL);
      raise(signum);
    };
    atexit(cleanup);
    for (int sig : {SIGINT, SIGSEGV, SIGABRT, SIGTERM, SIGBUS})
      signal(sig, cleanupOnSig);

    const char tmpDir[] = "/tmp/froleyks-Voiraig";
    mkdir(tmpDir, 0777);
  }
  char path[] = "/tmp/froleyks-Voiraig/XXXXXX";
  int fd = mkstemp(path);
  assert(fd != -1);
  close(fd); // Close the file descriptor
  tempFiles.push_back(path);
  return path;
}

#ifndef NDEBUG
#define ass(expr) assert(expr)
#else

# ifdef __has_cpp_attribute
#  if __has_cpp_attribute(assume)
#   define ass(expr) [[assume(expr)]]
#  else
#   define ass(expr) (void)sizeof(expr)
#  endif
# else
#  define ass(expr) (void)sizeof(expr)
# endif
#endif

// #ifndef NDEBUG
// #define ass(expr) assert(expr)
// #elif __has_cpp_attribute(assume)
// #define ass(expr) [[assume(expr)]]
// #else
// #define ass(expr) (void)sizeof(expr)
// #endif // NDEBUG
