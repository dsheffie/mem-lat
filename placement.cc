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

bool pin_to_cpus(const std::vector<int> &cpus) {
  /* best effort: interactive QoS steers the thread toward the fastest
   * cluster, but the scheduler still chooses the core */
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
  (void)cpus;
  return false;
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
