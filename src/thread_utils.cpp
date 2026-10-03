#include "chronos/thread_utils.h"

#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <sys/qos.h>

#include <algorithm>
#include <cstring>

namespace chronos {

bool elevate_thread_qos() noexcept {
    // relative_priority 0: no further adjustment within the class. This is
    // the highest QoS tier application code can request — the same class
    // macOS gives to direct UI event handling, which is the system's own
    // precedent for "must be scheduled with minimal latency." It's a hint
    // to XNU's scheduler, not a real-time guarantee: the kernel can still
    // preempt this thread for a higher-priority system task, but it will
    // deprioritize this thread far less readily than QOS_CLASS_DEFAULT.
    return pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) == 0;
}

bool set_thread_affinity_tag(uint32_t tag) noexcept {
    // Mach has no concept of pinning a thread to a specific logical CPU —
    // THREAD_AFFINITY_POLICY is a *grouping* hint: threads sharing a tag
    // are told to prefer running on the same L2 cache cluster (useful when
    // they communicate heavily, e.g. a producer/consumer pair around an
    // SPSC queue); threads with different tags are told to prefer distinct
    // clusters (useful when they'd otherwise evict each other's working
    // set from a shared cache). The kernel is free to ignore the hint
    // entirely under scheduling pressure.
    thread_affinity_policy_data_t policy{static_cast<integer_t>(tag)};
    const thread_t self = pthread_mach_thread_np(pthread_self());
    const kern_return_t result = thread_policy_set(
        self,
        THREAD_AFFINITY_POLICY,
        reinterpret_cast<thread_policy_t>(&policy),
        THREAD_AFFINITY_POLICY_COUNT);
    return result == KERN_SUCCESS;
}

bool set_thread_name(std::string_view name) noexcept {
    // macOS's pthread_setname_np takes no thread argument — unlike glibc's
    // version, it can only name the calling thread, which is why this
    // function must be invoked from inside the thread it names rather than
    // by a parent handing it a std::thread handle. MAXTHREADNAMESIZE is 64
    // (63 visible chars + nul); truncate explicitly instead of letting an
    // oversized name make the call fail outright.
    constexpr size_t kMaxNameLength = 63;
    char buffer[kMaxNameLength + 1];
    const size_t copy_length = std::min(name.size(), kMaxNameLength);
    std::memcpy(buffer, name.data(), copy_length);
    buffer[copy_length] = '\0';
    return pthread_setname_np(buffer) == 0;
}

}  // namespace chronos
