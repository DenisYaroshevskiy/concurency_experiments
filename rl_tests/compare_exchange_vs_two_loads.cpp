#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>

std::atomic<std::uint64_t> generation = 1;

void asymmetric_thread_fence_light() { asm volatile("" : : : "memory"); }

struct Reader1 {
    void enter();
    void exit();

    void notify();

    std::atomic<std::uint64_t> counter;
    std::uint32_t nested_readers;
    std::atomic<bool> waiting;
};

void Reader1::enter() {
    std::uint64_t g = generation.load(std::memory_order_relaxed);
    std::uint64_t cur = counter.load(std::memory_order_relaxed);

    if (cur) [[unlikely]] {
        ++nested_readers;
        return;
    }

    counter.store(g, std::memory_order_relaxed);
    asymmetric_thread_fence_light();
}

void Reader1::exit() {
    if (nested_readers) [[unlikely]] {
        --nested_readers;
        return;
    }

    asymmetric_thread_fence_light();
    counter.store(0, std::memory_order_relaxed);
    asymmetric_thread_fence_light();

    if (waiting.load(std::memory_order_relaxed)) [[unlikely]] {
        notify();
    }
}

struct Reader2 {
    void enter();
    void exit();

    void unusual_exit();

    static constexpr std::uint64_t unusual_bit = (std::uint64_t)1 << 63;

    std::uint64_t saved_gen;
    std::atomic<std::uint64_t> counter;
    std::uint64_t nested_count = 0;
};

void Reader2::enter() {
    std::uint64_t g = generation.load(std::memory_order_relaxed);
    std::uint64_t cur = counter.load(std::memory_order_relaxed);

    if (cur) [[unlikely]] {
        counter.store(cur | unusual_bit, std::memory_order_relaxed);
        ++nested_count;
        return;
    }

    saved_gen = g;
    counter.store(g, std::memory_order_relaxed);
    asymmetric_thread_fence_light();
}

void Reader2::exit() {
    std::uint64_t val = saved_gen;
    if (counter.compare_exchange_strong(val, 0, std::memory_order_relaxed)) [[likely]] {
        return;
    }
    unusual_exit();
}

void Reader1::notify() { benchmark::DoNotOptimize(this); }
void Reader2::unusual_exit() { benchmark::DoNotOptimize(this); }

[[gnu::noinline]] void reader1_enter_exit(Reader1& r) {
    r.enter();
    r.exit();
}

[[gnu::noinline]] void reader2_enter_exit(Reader2& r) {
    r.enter();
    r.exit();
}

static void BM_Reader1(benchmark::State& state) {
    Reader1 r{.counter = 0, .nested_readers = 0, .waiting = false};
    for (auto _ : state) {
        reader1_enter_exit(r);
    }
    benchmark::DoNotOptimize(r);
}
BENCHMARK(BM_Reader1);

static void BM_Reader2(benchmark::State& state) {
    Reader2 r{.saved_gen = 0, .counter = 0, .nested_count = 0};
    for (auto _ : state) {
        reader2_enter_exit(r);
    }
    benchmark::DoNotOptimize(r);
}
BENCHMARK(BM_Reader2);

BENCHMARK_MAIN();
