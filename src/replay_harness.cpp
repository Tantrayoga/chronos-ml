// Market data replay harness: pre-generates a deterministic ITCH-style feed of
// packed big-endian WireOrder messages, streams it from an ingress thread
// through the SPSC queue into a matching-engine thread, and renders a
// terminal dashboard every kDashboardInterval messages. Per-message latency
// (parse + route + match) is written to CSV for scripts/plot_latency.py.

#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "chronos/binary_parser.h"
#include "chronos/ml_inference.h"
#include "chronos/order_book.h"
#include "chronos/spsc_queue.h"
#include "chronos/telemetry.h"
#include "chronos/thread_utils.h"

namespace {

constexpr size_t kMessageCount = 1'000'000;
constexpr size_t kDashboardInterval = 100'000;
constexpr size_t kQueueCapacity = 1 << 16;
constexpr size_t kMaxPriceLevels = 4096;
// Sized above kMessageCount so the pool cannot exhaust even if no order ever
// crosses — a replay that silently rejects orders would report flattering
// latencies for work it never did.
constexpr size_t kMaxOrders = 1 << 20;

constexpr uint64_t kTicksPerUnit = 10'000;  // matches Order::price fixed-point scale
constexpr uint64_t kStartMid = 100 * kTicksPerUnit;
constexpr uint64_t kMidDriftLimit = 256;
constexpr uint64_t kPriceBand = 64;

using Packet = std::array<uint8_t, chronos::kWireOrderSize>;
using ReplayBook = chronos::OrderBook<kMaxPriceLevels, kMaxOrders>;
using PacketQueue = chronos::SPSCQueue<Packet, kQueueCapacity>;

// xorshift64*: a few integer ops per draw, no state beyond one word, and
// fully deterministic, so two runs replay a byte-identical feed and latency
// differences between runs reflect the system, not the input.
struct XorShift64 {
    uint64_t state;

    uint64_t next() noexcept {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
};

std::vector<Packet> generate_feed() {
    std::vector<Packet> feed(kMessageCount);
    XorShift64 rng{0x9E3779B97F4A7C15ULL};
    uint64_t mid = kStartMid;

    for (size_t i = 0; i < kMessageCount; ++i) {
        // Bounded random walk on the mid keeps the number of live price
        // levels well under kMaxPriceLevels while still moving the book.
        const uint64_t drift = rng.next() % 3;
        if (drift == 0 && mid > kStartMid - kMidDriftLimit) {
            --mid;
        } else if (drift == 1 && mid < kStartMid + kMidDriftLimit) {
            ++mid;
        }

        const bool is_buy = (rng.next() & 1) == 0;
        const uint64_t offset = rng.next() % kPriceBand;
        // ~30% of flow is aggressive (priced through the mid) so the feed
        // exercises the crossing path, not just resting inserts.
        const bool aggressive = (rng.next() % 10) < 3;

        chronos::WireOrder order{};
        order.message_type = chronos::WireMessageType::NewOrder;
        order.order_id = i + 1;
        order.quantity = static_cast<uint32_t>(1 + rng.next() % 100);
        order.side = is_buy ? chronos::WireSide::Buy : chronos::WireSide::Sell;
        if (is_buy) {
            order.price = aggressive ? mid + offset : mid - 1 - offset;
        } else {
            order.price = aggressive ? mid - offset : mid + 1 + offset;
        }

        chronos::encode_wire_order(order, feed[i].data());
    }
    return feed;
}

const char* signal_label(chronos::PricePrediction prediction) {
    switch (prediction) {
        case chronos::PricePrediction::Up:
            return "\033[32mUP  \033[0m";
        case chronos::PricePrediction::Down:
            return "\033[31mDOWN\033[0m";
        case chronos::PricePrediction::Flat:
            return "\033[33mFLAT\033[0m";
    }
    return "?";
}

// Runs on the matching thread itself: the book is single-writer and not
// safe to read from another thread, so the dashboard samples it in place.
// Rendering sits outside the per-message timing window, so terminal I/O
// never lands in the recorded latency distribution.
void render_dashboard(const ReplayBook& book, size_t processed, size_t rejected, uint64_t elapsed_ns,
                      bool interactive) {
    const auto tob = book.top_of_book();
    const chronos::BookFeatures features = book.extract_top_of_book_features();
    const float obi = chronos::InferenceEngine::compute_obi(features);
    const chronos::PricePrediction prediction = book.inference_engine.predict(features);

    constexpr int kBarWidth = 40;
    const int filled = static_cast<int>((processed * kBarWidth) / kMessageCount);
    std::string bar(static_cast<size_t>(filled), '#');
    bar.append(static_cast<size_t>(kBarWidth - filled), '.');

    const double seconds = static_cast<double>(elapsed_ns) / 1e9;
    const double throughput_m = seconds > 0.0 ? (static_cast<double>(processed) / seconds) / 1e6 : 0.0;

    if (interactive) {
        std::printf("\033[2J\033[H");  // clear screen + home cursor: redraw in place
    }
    std::printf("\033[1mCHRONOS-ML :: market replay\033[0m\n");
    std::printf("progress   [%s] %zu / %zu\n", bar.c_str(), processed, kMessageCount);
    if (tob.has_bid) {
        std::printf("best bid   %10.4f  x %-8llu\n", static_cast<double>(tob.bid_price) / kTicksPerUnit,
                    static_cast<unsigned long long>(tob.bid_qty));
    } else {
        std::printf("best bid   %10s\n", "--");
    }
    if (tob.has_ask) {
        std::printf("best ask   %10.4f  x %-8llu\n", static_cast<double>(tob.ask_price) / kTicksPerUnit,
                    static_cast<unsigned long long>(tob.ask_qty));
    } else {
        std::printf("best ask   %10s\n", "--");
    }
    std::printf("spread     %llu ticks\n", static_cast<unsigned long long>(features.spread));
    std::printf("OBI(top%zu) %+.4f   signal %s\n", chronos::kObiDepth, obi, signal_label(prediction));
    std::printf("levels     %zu   rejected %zu   throughput %.2f M msg/s\n\n", book.active_price_level_count,
                rejected, throughput_m);
    std::fflush(stdout);
}

struct Options {
    std::string csv_path = "latency.csv";
    unsigned pace_ms = 0;
};

void print_usage(const char* program) {
    std::fprintf(stderr,
                 "usage: %s [--pace MS] [CSV_PATH]\n"
                 "  --pace MS   pause MS milliseconds after each dashboard frame so the\n"
                 "              replay can be watched; paused time is excluded from\n"
                 "              throughput and from every latency sample\n"
                 "  CSV_PATH    per-message latency output (default: latency.csv)\n",
                 program);
}

bool parse_options(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            return false;
        }
        if (arg == "--pace") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "error: --pace needs a value in milliseconds\n");
                return false;
            }
            const std::string value = argv[++i];
            char* end = nullptr;
            const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
            if (value.empty() || *end != '\0' || parsed > 60'000) {
                std::fprintf(stderr, "error: --pace expects 0-60000 ms, got '%s'\n", value.c_str());
                return false;
            }
            options.pace_ms = static_cast<unsigned>(parsed);
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
            return false;
        } else {
            options.csv_path = arg;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 2;
    }
    const std::string& csv_path = options.csv_path;
    const bool interactive = isatty(STDOUT_FILENO) != 0;

    std::printf("generating %zu wire messages...\n", kMessageCount);
    const std::vector<Packet> feed = generate_feed();

    // All large structures are allocated up front, before either thread
    // starts. Nothing on the per-message path below touches the heap.
    auto book = std::make_unique<ReplayBook>();
    auto queue = std::make_unique<PacketQueue>();
    std::vector<uint64_t> latencies_ns(kMessageCount, 0);

    size_t rejected = 0;
    const uint64_t replay_start_ns = chronos::now_ns();
    uint64_t replay_end_ns = replay_start_ns;
    uint64_t paused_ns = 0;  // written only by the matcher, read after join

    std::thread ingress([&]() {
        chronos::set_thread_name("chronos-ingress");
        chronos::elevate_thread_qos();
        chronos::set_thread_affinity_tag(1);

        for (const Packet& packet : feed) {
            // A full queue means the matching thread is momentarily behind
            // a micro-burst; yield instead of blocking on a kernel primitive
            // so the ingress side recovers the moment space frees up.
            while (!queue->push(packet)) {
                std::this_thread::yield();
            }
        }
    });

    std::thread matcher([&]() {
        chronos::set_thread_name("chronos-matcher");
        chronos::elevate_thread_qos();
        chronos::set_thread_affinity_tag(2);

        size_t processed = 0;
        while (processed < kMessageCount) {
            Packet* packet = queue->front();
            if (packet == nullptr) {
                std::this_thread::yield();
                continue;
            }

            const uint64_t t_start = chronos::now_ns();

            // Parse straight out of the queue slot via front(): the packet
            // is never copied out of the ring before being decoded.
            chronos::WireOrder order{};
            const bool parsed = chronos::parse_wire_order(packet->data(), packet->size(), order);
            queue->pop_front();

            if (parsed && order.message_type == chronos::WireMessageType::NewOrder) {
                const chronos::Side side =
                    order.side == chronos::WireSide::Buy ? chronos::Side::Buy : chronos::Side::Sell;
                if (book->add_order(order.order_id, side, order.price, order.quantity) == chronos::kInvalidIndex) {
                    ++rejected;
                }
            } else {
                ++rejected;
            }

            latencies_ns[processed] = chronos::now_ns() - t_start;
            ++processed;

            if (processed % kDashboardInterval == 0) {
                render_dashboard(*book, processed, rejected, chronos::now_ns() - replay_start_ns - paused_ns,
                                 interactive);
                if (options.pace_ms > 0 && processed < kMessageCount) {
                    // Sleeping here never lands inside a latency sample (the
                    // next t_start is taken after we wake), but it does let
                    // caches and the core's clock speed cool down, so the
                    // first few messages after each pause can run slower.
                    // Use an unpaced run for the published benchmark numbers.
                    const uint64_t pause_start = chronos::now_ns();
                    std::this_thread::sleep_for(std::chrono::milliseconds(options.pace_ms));
                    paused_ns += chronos::now_ns() - pause_start;
                }
            }
        }
        replay_end_ns = chronos::now_ns();
    });

    ingress.join();
    matcher.join();

    // CSV keeps arrival order; written before the stats pass sorts in place.
    std::ofstream csv(csv_path);
    csv << "sequence,latency_ns\n";
    for (size_t i = 0; i < latencies_ns.size(); ++i) {
        csv << i << ',' << latencies_ns[i] << '\n';
    }
    csv.close();

    const chronos::LatencyStats stats = chronos::compute_latency_stats(latencies_ns);
    const double seconds = static_cast<double>(replay_end_ns - replay_start_ns - paused_ns) / 1e9;

    std::printf("replay complete: %zu messages in %.3f s (%.2f M msg/s), %zu rejected\n", kMessageCount, seconds,
                static_cast<double>(kMessageCount) / seconds / 1e6, rejected);
    std::printf("per-message latency (ns): mean=%.1f median=%.1f p99=%.1f\n", stats.mean_ns, stats.median_ns,
                stats.p99_ns);
    std::printf("latency samples written to %s\n", csv_path.c_str());
    return 0;
}
