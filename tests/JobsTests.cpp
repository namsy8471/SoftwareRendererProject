#include "Utils/Jobs.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

#include "TestHarness.h"
#include "TestSupport.h"

using namespace sr::test;

namespace
{
template <sr::jobs::Split split, class Index> void checkCoverage(Index begin, Index end, Index grain)
{
    const auto count = static_cast<std::size_t>(end > begin ? end - begin : 0);
    std::vector<std::atomic<int>> visits(count);
    std::atomic<bool> chunkTooLarge = false;
    sr::jobs::ParallelFor<split>(begin, end, grain,
                                 [&](Index first, Index last)
                                 {
                                     if constexpr (split == sr::jobs::Split::Fixed)
                                         if (last - first > grain && sr::jobs::MaxConcurrency() > 1)
                                             chunkTooLarge = true;
                                     for (Index index = first; index != last; ++index)
                                         visits[static_cast<std::size_t>(index - begin)].fetch_add(1);
                                 });
    for (const auto &visit : visits)
        check(visit.load() == 1, "every index runs exactly once");
    check(!chunkTooLarge.load(), "fixed split never exceeds the grain");
}

void coverageTests()
{
    for (const std::size_t count : std::initializer_list<std::size_t>{0, 1, 2, 3, 7, 64, 1000, 8160, 100003})
        for (const std::size_t grain : std::initializer_list<std::size_t>{1, 4, 16, 2048})
        {
            checkCoverage<sr::jobs::Split::Fixed>(std::size_t{0}, count, grain);
            checkCoverage<sr::jobs::Split::Adaptive>(std::size_t{0}, count, grain);
        }
    checkCoverage<sr::jobs::Split::Adaptive>(-37, 1080, 1);
    checkCoverage<sr::jobs::Split::Fixed>(1, 1079, 16);
    checkCoverage<sr::jobs::Split::Fixed>(5, 5, 1);
    checkCoverage<sr::jobs::Split::Fixed>(9, 3, 1);
}

void exceptionTests()
{
    for (int round = 0; round < 50; ++round)
    {
        std::atomic<int> ran = 0;
        bool caught = false;
        try
        {
            sr::jobs::ParallelFor<sr::jobs::Split::Fixed>(0, 512, 1,
                                                          [&](int first, int)
                                                          {
                                                              ran.fetch_add(1);
                                                              if (first == 100)
                                                                  throw std::runtime_error("chunk 100");
                                                          });
        }
        catch (const std::runtime_error &error)
        {
            caught = std::string_view(error.what()) == "chunk 100";
        }
        check(caught, "a chunk's exception reaches the caller");
        check(ran.load() >= 1 && ran.load() <= 512, "chunks after a failure may be skipped");
    }
    // 예외 뒤에도 같은 풀이 정상 동작한다.
    std::atomic<std::size_t> sum = 0;
    sr::jobs::ParallelFor(std::size_t{0}, std::size_t{10000},
                          [&](std::size_t first, std::size_t last)
                          {
                              for (auto index = first; index != last; ++index)
                                  sum.fetch_add(index);
                          });
    check(sum.load() == std::size_t{10000} * std::size_t{9999} / 2, "pool works after an exception");
}

void nestedAndConcurrentTests()
{
    // 스키닝처럼 바깥 조각 안에서 다시 ParallelFor를 부른다.
    std::atomic<std::size_t> total = 0;
    sr::jobs::ParallelFor(0, 64,
                          [&](int first, int last)
                          {
                              for (int outer = first; outer != last; ++outer)
                                  sr::jobs::ParallelFor(std::size_t{0}, std::size_t{3000}, std::size_t{64},
                                                        [&](std::size_t a, std::size_t b)
                                                        { total.fetch_add(b - a); });
                          });
    check(total.load() == std::size_t{64} * std::size_t{3000}, "nested parallel loops cover every item");

    // 여러 스레드가 동시에 작업을 낸다(테스트의 두 renderer 동시 렌더와 같은 모양).
    std::vector<std::thread> callers;
    std::atomic<std::size_t> concurrent = 0;
    for (int caller = 0; caller < 4; ++caller)
        callers.emplace_back(
            [&]
            {
                for (int round = 0; round < 200; ++round)
                    sr::jobs::ParallelFor<sr::jobs::Split::Fixed>(
                        std::size_t{0}, std::size_t{257}, std::size_t{4},
                        [&](std::size_t a, std::size_t b) { concurrent.fetch_add(b - a); });
            });
    for (auto &caller : callers)
        caller.join();
    check(concurrent.load() == std::size_t{4} * std::size_t{200} * std::size_t{257},
          "concurrent callers each complete their own loops");
}

std::size_t distinctThreads(std::size_t items)
{
    std::mutex mutex;
    std::set<std::thread::id> threads;
    sr::jobs::ParallelFor<sr::jobs::Split::Fixed>(std::size_t{0}, items, std::size_t{1},
                                                  [&](std::size_t, std::size_t)
                                                  {
                                                      // 조각이 겹쳐 돌도록 잠깐 머문다.
                                                      std::this_thread::sleep_for(
                                                          std::chrono::microseconds(200));
                                                      const std::scoped_lock lock(mutex);
                                                      threads.insert(std::this_thread::get_id());
                                                  });
    return threads.size();
}

void limitTests()
{
    const auto defaultThreads = sr::jobs::MaxConcurrency();
    check(defaultThreads >= 1, "at least the calling thread");
    {
        sr::jobs::ConcurrencyLimit single(1);
        check(sr::jobs::MaxConcurrency() == 1, "limit 1 is visible");
        const auto caller = std::this_thread::get_id();
        bool onCaller = true;
        sr::jobs::ParallelFor(0, 1000, [&](int, int) { onCaller &= std::this_thread::get_id() == caller; });
        check(onCaller, "limit 1 runs everything on the calling thread");
        {
            sr::jobs::ConcurrencyLimit wider(8);
            check(sr::jobs::MaxConcurrency() == 1, "the smallest active limit wins");
        }
        check(sr::jobs::MaxConcurrency() == 1, "removing a wider limit keeps the narrow one");
    }
    check(sr::jobs::MaxConcurrency() == defaultThreads, "limits restore the default");
    if (defaultThreads >= 2)
    {
        sr::jobs::ConcurrencyLimit two(2);
        check(distinctThreads(64) <= 2, "limit 2 uses at most two threads");
    }
}

void isolatedTests()
{
    sr::jobs::IsolatedRunner isolated;
    const auto caller = std::this_thread::get_id();
    bool onCaller = true;
    const int result = isolated.Run(
        [&]
        {
            sr::jobs::ParallelFor(0, 4096,
                                  [&](int, int) { onCaller &= std::this_thread::get_id() == caller; });
            return 42;
        });
    check(result == 42, "isolated run returns the function's value");
    check(onCaller, "isolated loops stay on the calling thread");
}

void perWorkerTests()
{
    sr::jobs::PerWorker<std::size_t> counts;
    sr::jobs::ParallelFor<sr::jobs::Split::Fixed>(std::size_t{0}, std::size_t{20000}, std::size_t{7},
                                                  [&](std::size_t first, std::size_t last)
                                                  { counts.Local() += last - first; });
    std::size_t total = 0, workers = 0;
    for (const auto count : counts)
    {
        total += count;
        ++workers;
    }
    check(total == std::size_t{20000}, "per-worker values sum to the item count");
    check(workers >= 1 && workers <= sr::jobs::MaxConcurrency() + 1, "one value per participating thread");
    auto &first = counts.Local();
    check(&first == &counts.Local(), "Local returns the same object on the same thread");
    std::size_t *other = nullptr;
    std::thread([&] { other = &counts.Local(); }).join();
    check(other != &first, "another thread gets its own object");
}

void finishTests()
{
    // finish는 조각을 맡은 스레드마다 그 스레드의 마지막 조각 뒤에 불린다. 그때 본 개수가 최종 개수와 같아야 한다.
    struct Counts
    {
        std::size_t items = 0, seenAtFinish = 0, finishes = 0;
    };
    for (int repeat = 0; repeat < 20; ++repeat)
    {
        sr::jobs::PerWorker<Counts> counts;
        std::atomic<bool> finishWithoutChunks = false;
        sr::jobs::ParallelFor<sr::jobs::Split::Fixed>(
            std::size_t{0}, std::size_t{5000}, std::size_t{3},
            [&](std::size_t first, std::size_t last) { counts.Local().items += last - first; },
            [&]
            {
                auto *local = counts.Find();
                if (!local)
                    finishWithoutChunks = true;
                else
                {
                    local->seenAtFinish = local->items;
                    ++local->finishes;
                }
            });
        std::size_t total = 0;
        for (const auto &count : counts)
        {
            total += count.items;
            check(count.finishes >= 1 && count.seenAtFinish == count.items,
                  "finish runs after the thread's last chunk");
        }
        check(total == std::size_t{5000}, "finish overload still covers every item");
        check(!finishWithoutChunks.load(), "finish runs only on a thread that ran chunks");
    }
    bool finished = false;
    sr::jobs::ParallelFor(0, 1, 1, [](int, int) {}, [&] { finished = true; });
    check(finished, "a single inline chunk also calls finish");
    sr::jobs::PerWorker<int> unused;
    int *found = &unused.Local();
    std::thread([&] { found = unused.Find(); }).join();
    check(found == nullptr, "Find does not create a value for a new thread");
}
} // namespace

namespace sr::test
{
void RegisterJobsTests(TestRegistry &registry)
{
    registry.Add("jobs.coverage", coverageTests);
    registry.Add("jobs.exceptions", exceptionTests);
    registry.Add("jobs.nested_concurrent", nestedAndConcurrentTests);
    registry.Add("jobs.limits", limitTests);
    registry.Add("jobs.isolated", isolatedTests);
    registry.Add("jobs.per_worker", perWorkerTests);
    registry.Add("jobs.finish", finishTests);
}
} // namespace sr::test
