/*
 * Adapted by D. Lemire
 * From:
 * Duc Tri Nguyen (CERG GMU)
 * From:
 * Dougall Johnson
 * https://gist.github.com/dougallj/5bafb113492047c865c0c8cfbc930155#file-m1_robsize-c-L390
 *
 * Note on newer kernels (observed on macOS 26 / M6): kpc_set_config() is
 * refused with EPERM for every configurable event ("kpc: kpc_set_config: not
 * allowed to count event ...") unless the caller is entitled.  The fixed
 * counters (0 = cycles, 1 = instructions) still work, so we fall back to the
 * KPC_CLASS_FIXED class only when the configurable class can't be programmed.
 */

#ifdef __APPLE__

#include "m1cycles.hh"
#include <dlfcn.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>

#define KPERF_LIST                                                             \
  /*  ret, name, params */                                                     \
  F(int, kpc_get_counting, void)                                               \
  F(int, kpc_force_all_ctrs_set, int)                                          \
  F(int, kpc_set_counting, uint32_t)                                           \
  F(int, kpc_set_thread_counting, uint32_t)                                    \
  F(int, kpc_set_config, uint32_t, void *)                                     \
  F(int, kpc_get_config, uint32_t, void *)                                     \
  F(int, kpc_set_period, uint32_t, void *)                                     \
  F(int, kpc_get_period, uint32_t, void *)                                     \
  F(uint32_t, kpc_get_counter_count, uint32_t)                                 \
  F(uint32_t, kpc_get_config_count, uint32_t)                                  \
  F(int, kperf_sample_get, int *)                                              \
  F(int, kpc_get_thread_counters, int, unsigned int, void *)

#define F(ret, name, ...)                                                      \
  typedef ret name##proc(__VA_ARGS__);                                         \
  static name##proc *name;
KPERF_LIST
#undef F

#define CFGWORD_EL0A32EN_MASK (0x10000)
#define CFGWORD_EL0A64EN_MASK (0x20000)
#define CFGWORD_EL1EN_MASK (0x40000)
#define CFGWORD_EL3EN_MASK (0x80000)
#define CFGWORD_ALLMODES_MASK (0xf0000)

/* M1..M4 era CPMU event numbers; not valid on the M6 ("as9") PMU. */
#define CPMU_NONE 0
#define CPMU_CORE_CYCLE 0x02
#define CPMU_INST_A64 0x8c
#define CPMU_INST_BRANCH 0x8d
#define CPMU_SYNC_DC_LOAD_MISS 0xbf
#define CPMU_SYNC_DC_STORE_MISS 0xc0
#define CPMU_SYNC_DTLB_MISS 0xc1
#define CPMU_SYNC_ST_HIT_YNGR_LD 0xc4
#define CPMU_SYNC_BR_ANY_MISP 0xcb
#define CPMU_FED_IC_MISS_DEM 0xd3
#define CPMU_FED_ITLB_MISS 0xd4

#define KPC_CLASS_FIXED (0)
#define KPC_CLASS_CONFIGURABLE (1)
#define KPC_CLASS_POWER (2)
#define KPC_CLASS_RAWPMU (3)
#define KPC_CLASS_FIXED_MASK (1u << KPC_CLASS_FIXED)
#define KPC_CLASS_CONFIGURABLE_MASK (1u << KPC_CLASS_CONFIGURABLE)
#define KPC_CLASS_POWER_MASK (1u << KPC_CLASS_POWER)
#define KPC_CLASS_RAWPMU_MASK (1u << KPC_CLASS_RAWPMU)

#define MAX_COUNTERS 32
static uint64_t g_counters[MAX_COUNTERS];
static uint64_t g_config[MAX_COUNTERS];

/* State decided at init time. */
static uint32_t g_kpc_mask = 0;      /* classes we are counting */
static uint32_t g_n_fixed = 0;       /* # fixed counters (cycles, instructions) */
static uint32_t g_n_counters = 0;    /* total counters read per sample */
static bool g_have_configurable = false;
static bool g_initialized = false;

static bool configure_counting(uint32_t mask) {
  if (kpc_force_all_ctrs_set(1)) {
    fprintf(stderr, "kpc_force_all_ctrs_set failed (run as root?)\n");
    return false;
  }
  if (kpc_set_counting(mask)) {
    fprintf(stderr, "kpc_set_counting failed\n");
    return false;
  }
  if (kpc_set_thread_counting(mask)) {
    fprintf(stderr, "kpc_set_thread_counting failed\n");
    return false;
  }
  return true;
}

static void init_rdtsc() {
  void *kperf = dlopen(
      "/System/Library/PrivateFrameworks/kperf.framework/Versions/A/kperf",
      RTLD_LAZY);
  if (!kperf) {
    fprintf(stderr, "kperf = %p\n", kperf);
    return;
  }
#define F(ret, name, ...)                                                      \
  name = (name##proc *)(dlsym(kperf, #name));                                  \
  if (!name) {                                                                 \
    fprintf(stderr, "%s = %p\n", #name, (void *)name);                         \
    return;                                                                    \
  }
  KPERF_LIST
#undef F

  g_n_fixed = kpc_get_counter_count(KPC_CLASS_FIXED_MASK);
  uint32_t n_cfg = kpc_get_config_count(KPC_CLASS_CONFIGURABLE_MASK);
  uint32_t n_all = kpc_get_counter_count(KPC_CLASS_FIXED_MASK |
                                         KPC_CLASS_CONFIGURABLE_MASK);
  if (g_n_fixed < 2 || n_all > MAX_COUNTERS) {
    fprintf(stderr, "unexpected kpc layout: fixed=%u configurable=%u\n",
            g_n_fixed, n_cfg);
    return;
  }

  /* First try the full M1-style configuration (cycles, branches, mispredicts,
   * instructions on configurable counters). */
  g_have_configurable = false;
  if (n_cfg >= 6) {
    for (uint32_t i = 0; i < n_cfg; i++) {
      g_config[i] = 0;
    }
    g_config[0] = CPMU_CORE_CYCLE | CFGWORD_EL0A64EN_MASK;
    g_config[3] = CPMU_INST_BRANCH | CFGWORD_EL0A64EN_MASK;
    g_config[4] = CPMU_SYNC_BR_ANY_MISP | CFGWORD_EL0A64EN_MASK;
    g_config[5] = CPMU_INST_A64 | CFGWORD_EL0A64EN_MASK;
    uint32_t mask = KPC_CLASS_CONFIGURABLE_MASK | KPC_CLASS_FIXED_MASK;
    if (kpc_set_config(mask, g_config) == 0) {
      g_have_configurable = true;
      g_kpc_mask = mask;
      g_n_counters = n_all;
    }
  }

  if (!g_have_configurable) {
    /* Kernel refused the configurable events (event allowlist); use the fixed
     * counters only: [0] = cycles, [1] = instructions. */
    fprintf(stderr, "kpc: configurable counters unavailable, "
                    "using fixed counters only (cycles, instructions)\n");
    g_kpc_mask = KPC_CLASS_FIXED_MASK;
    g_n_counters = g_n_fixed;
  }

  if (!configure_counting(g_kpc_mask)) {
    return;
  }
  g_initialized = true;
}

void setup_performance_counters(void) {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  int test_high_perf_cores = 1;
  if (test_high_perf_cores) {
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  } else {
    pthread_set_qos_class_self_np(QOS_CLASS_BACKGROUND, 0);
  }
  init_rdtsc();
}

extern performance_counters get_counters(void) {
  static bool warned = false;
  if (!g_initialized ||
      kpc_get_thread_counters(0, g_n_counters, g_counters)) {
    if (!warned) {
      fprintf(stderr, "kpc_get_thread_counters failed, run as sudo?\n");
      warned = true;
    }
    return performance_counters(0.0);
  }
  if (g_have_configurable) {
    // configurable counter i lives at g_counters[g_n_fixed + i]
    // g_counters[f + 5] gives you the number of instructions 'decoded'
    // whereas g_counters[1] might give you the number of instructions 'retired'.
    const uint32_t f = g_n_fixed;
    return performance_counters{g_counters[f + 0], g_counters[f + 3],
                                g_counters[f + 4], g_counters[f + 5]};
  }
  return performance_counters{g_counters[0], (uint64_t)0, (uint64_t)0,
                              g_counters[1]};
}

#endif
