#ifdef __APPLE__
/* macOS placement helpers: which cpu am I on (kperf, root), and which cpus
 * belong to a cluster type (IO registry). See placement.hh. */
#include "placement.hh"
#include <dlfcn.h>
#include <pthread.h>
#include <cstdint>
#include <cstdio>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

typedef int kpc_get_cpu_counters_t(int, uint32_t, int *, void *);
static kpc_get_cpu_counters_t *kpc_get_cpu_counters = nullptr;
static bool kpc_tried = false;

int current_cpu() {
  if(!kpc_tried) {
    kpc_tried = true;
    void *kperf = dlopen(
        "/System/Library/PrivateFrameworks/kperf.framework/Versions/A/kperf",
        RTLD_LAZY);
    if(kperf) {
      kpc_get_cpu_counters = reinterpret_cast<kpc_get_cpu_counters_t *>(
          dlsym(kperf, "kpc_get_cpu_counters"));
    }
  }
  if(kpc_get_cpu_counters == nullptr) {
    return -1;
  }
  int cpu = -1;
  uint64_t buf[64];
  /* class mask 1 = fixed counters; we only want the cpu number */
  if(kpc_get_cpu_counters(0, 1, &cpu, buf) != 0) {
    return -1;
  }
  return cpu;
}

/* Measured on an M6 Mac mini (macOS 26): a lone USER_INTERACTIVE thread runs
 * on the fastest (P/Super) cluster; BACKGROUND confines a thread to the E
 * cluster; and a UTILITY thread with the P cores held by USER_INTERACTIVE
 * spinners runs on the middle (M) cluster. Equal QoS for spinners and the
 * measuring thread does not work: the scheduler rotates them. */
static bool subset_of(const std::vector<int> &cpus, char type) {
  if(cpus.empty()) return false;
  std::vector<int> t = cpus_of_cluster_type(type);
  for(int c : cpus) {
    if(!cpu_in(t, c)) return false;
  }
  return true;
}

bool pin_to_cpus(const std::vector<int> &cpus, int n_blockers) {
  qos_class_t qos = QOS_CLASS_USER_INTERACTIVE;
  if(subset_of(cpus, 'E')) {
    qos = QOS_CLASS_BACKGROUND;
  }
  else if(n_blockers > 0) {
    qos = QOS_CLASS_UTILITY;
  }
  pthread_set_qos_class_self_np(qos, 0);
  return false;
}

int default_blockers(const std::vector<int> &cpus) {
  if(cpus.empty() || subset_of(cpus, 'E')) {
    return 0;
  }
  /* target contains no P cpu but some M cpu: hold the P cores */
  std::vector<int> p = cpus_of_cluster_type('P');
  for(int c : cpus) {
    if(cpu_in(p, c)) return 0;
  }
  return static_cast<int>(p.size());
}

std::vector<int> cpus_of_cluster_type(char type) {
  std::vector<int> cpus;
  io_iterator_t it = 0;
  if(IOServiceGetMatchingServices(kIOMainPortDefault,
                                  IOServiceMatching("IOPlatformDevice"),
                                  &it) != KERN_SUCCESS) {
    return cpus;
  }
  io_object_t obj;
  while((obj = IOIteratorNext(it)) != 0) {
    CFTypeRef ct = IORegistryEntryCreateCFProperty(obj, CFSTR("cluster-type"),
                                                   kCFAllocatorDefault, 0);
    CFTypeRef id = IORegistryEntryCreateCFProperty(obj, CFSTR("logical-cpu-id"),
                                                   kCFAllocatorDefault, 0);
    if(ct && id && CFGetTypeID(ct) == CFDataGetTypeID() &&
       CFGetTypeID(id) == CFNumberGetTypeID() &&
       CFDataGetLength(static_cast<CFDataRef>(ct)) >= 1) {
      const UInt8 *b = CFDataGetBytePtr(static_cast<CFDataRef>(ct));
      int cpu = -1;
      CFNumberGetValue(static_cast<CFNumberRef>(id), kCFNumberIntType, &cpu);
      if(b[0] == static_cast<UInt8>(type) && cpu >= 0) {
        cpus.push_back(cpu);
      }
    }
    if(ct) CFRelease(ct);
    if(id) CFRelease(id);
    IOObjectRelease(obj);
  }
  IOObjectRelease(it);
  return cpus;
}
#endif
