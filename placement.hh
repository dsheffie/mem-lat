#ifndef __placement_hh__
#define __placement_hh__

#include <vector>
#include <string>
#include <cstdlib>
#include <cstring>

/* Where the latency thread runs.
 *
 * Linux: pin_to_cpus() hard-pins with sched_setaffinity and current_cpu()
 * is sched_getcpu().
 *
 * macOS: there is no affinity API on Apple silicon, so pin_to_cpus() only
 * raises the QoS class and returns false; callers verify placement with
 * current_cpu() (kperf, needs root) and retry samples that ran elsewhere.
 * A cpu spec may also be a cluster type letter (P/M/E) looked up in the IO
 * registry: on M6 "P" is the two Super cores, "M" the Performance cores,
 * "E" the Efficiency cores. On M1..M4 "P" is all performance cores. */

int current_cpu();                                   /* -1 if unknown */
/* true = hard pinned. n_blockers > 0 means the caller is filling faster
 * cores with interactive spinners, so this thread takes a lower QoS. */
bool pin_to_cpus(const std::vector<int> &cpus, int n_blockers = 0);
std::vector<int> cpus_of_cluster_type(char type);     /* macOS only, else empty */
/* how many interactive spinner threads it takes to push a thread onto
 * `cpus` (macOS: the cpu count of the clusters faster than the target) */
int default_blockers(const std::vector<int> &cpus);

/* "6,7" | "6-7" | "P" | "any" -> cpu list (empty = any) */
static inline std::vector<int> parse_cpu_spec(const char *spec) {
  std::vector<int> cpus;
  if(spec == nullptr || !strcmp(spec, "any") || !strcmp(spec, "")) {
    return cpus;
  }
  if(strlen(spec) == 1 && strchr("PMEpme", spec[0])) {
    return cpus_of_cluster_type(static_cast<char>(toupper(spec[0])));
  }
  std::string s(spec);
  size_t pos = 0;
  while(pos < s.size()) {
    size_t comma = s.find(',', pos);
    if(comma == std::string::npos) comma = s.size();
    std::string tok = s.substr(pos, comma - pos);
    size_t dash = tok.find('-');
    if(dash == std::string::npos) {
      cpus.push_back(atoi(tok.c_str()));
    }
    else {
      int lo = atoi(tok.substr(0, dash).c_str());
      int hi = atoi(tok.substr(dash + 1).c_str());
      for(int c = lo; c <= hi; c++) cpus.push_back(c);
    }
    pos = comma + 1;
  }
  return cpus;
}

static inline bool cpu_in(const std::vector<int> &cpus, int cpu) {
  for(int c : cpus) {
    if(c == cpu) return true;
  }
  return false;
}

static inline std::string cpu_list_str(const std::vector<int> &cpus) {
  std::string s;
  for(size_t i = 0; i < cpus.size(); i++) {
    s += (i ? "," : "") + std::to_string(cpus[i]);
  }
  return s.empty() ? "any" : s;
}

#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>
inline int current_cpu() { return sched_getcpu(); }
inline bool pin_to_cpus(const std::vector<int> &cpus, int) {
  if(cpus.empty()) return false;
  cpu_set_t set;
  CPU_ZERO(&set);
  for(int c : cpus) CPU_SET(c, &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;
}
inline std::vector<int> cpus_of_cluster_type(char) { return std::vector<int>(); }
inline int default_blockers(const std::vector<int> &) { return 0; }
#elif !defined(__APPLE__)
inline int current_cpu() { return -1; }
inline bool pin_to_cpus(const std::vector<int> &, int) { return false; }
inline std::vector<int> cpus_of_cluster_type(char) { return std::vector<int>(); }
inline int default_blockers(const std::vector<int> &) { return 0; }
#endif
/* __APPLE__ : see placement.cc */

#endif
