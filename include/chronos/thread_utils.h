#pragma once

#include <cstdint>
#include <string_view>

namespace chronos {

// macOS/Apple Silicon has no equivalent to Linux's sched_setaffinity —
// the XNU/Mach scheduler doesn't expose hard CPU-core pinning to
// userspace at all. What it exposes instead is QoS classes (a priority
// hint to the scheduler) and thread affinity *tags* (a grouping hint, not
// a guarantee) via Mach's thread_policy_set. Both functions below are
// best-effort scheduler hints, not hard guarantees — see the educational
// summary for why that's an intentional OS design choice, not a gap.

// Raises the calling thread's QoS to QOS_CLASS_USER_INTERACTIVE, the
// highest class available to application code. This tells the scheduler
// to treat the thread as needing immediate CPU time and minimal
// scheduling latency — the same class the OS gives to UI event handling,
// which is the actual precedent for "must not be made to wait."
// Returns true on success.
bool elevate_thread_qos() noexcept;

// Applies a Mach THREAD_AFFINITY_POLICY tag to the calling thread. Threads
// that share the same tag are hinted to the scheduler as benefiting from
// running on the same L2 cache cluster (cheap to communicate with each
// other); threads with distinct tags are hinted as preferring distinct
// clusters (minimizing cache contention between unrelated hot loops).
// Returns true if the policy call succeeded — success only means the
// kernel accepted the hint, not that it will always honor it.
bool set_thread_affinity_tag(uint32_t tag) noexcept;

// Sets the calling thread's name (visible in Instruments, lldb, and
// Activity Monitor) to ease profiling multi-threaded runs. macOS's
// pthread_setname_np, unlike Linux's, only operates on the *calling*
// thread — there is no cross-thread variant — so this must be called from
// inside the thread being named, not by a parent thread on its behalf.
// `name` is truncated to the platform limit (63 chars on macOS) if longer.
bool set_thread_name(std::string_view name) noexcept;

}  // namespace chronos
