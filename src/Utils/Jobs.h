#pragma once
// 병렬 작업 API. 렌더러/스키닝/로더는 이 헤더만 쓰고 작업 스케줄러(oneTBB)를 직접 include하지 않는다.
// 스케줄러를 바꿀 때 호출 측 코드를 고치지 않도록 실제로 쓰는 기능만 둔다.
#include <cstddef>
#include <type_traits>
#include <utility>
#include <tbb/blocked_range.h>
#include <tbb/enumerable_thread_specific.h>
#include <tbb/global_control.h>
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>

namespace sr::jobs
{
// 범위를 나누는 방식.
// Adaptive: grain은 최소 조각 크기다. 스케줄러가 스레드 수에 맞춰 조각을 키운다.
// Fixed: 조각을 grain 이하로 고정한다. 항목마다 비용 차이가 큰 작업(타일, 명령)에 쓴다.
enum class Split
{
    Adaptive,
    Fixed
};

// [begin, end)를 조각으로 나눠 body(first, last)를 병렬로 부른다. 모든 조각이 끝난 뒤 반환하며,
// 조각에서 난 예외는 남은 조각을 취소하고 호출자에게 다시 던진다.
template <Split split = Split::Adaptive, class Index, class Body>
void ParallelFor(Index begin, std::type_identity_t<Index> end, std::type_identity_t<Index> grain, Body &&body)
{
    const tbb::blocked_range<Index> range(begin, end, grain);
    const auto run = [&body](const tbb::blocked_range<Index> &part) { body(part.begin(), part.end()); };
    if constexpr (split == Split::Fixed)
        tbb::parallel_for(range, run, tbb::simple_partitioner());
    else
        tbb::parallel_for(range, run);
}

template <class Index, class Body> void ParallelFor(Index begin, std::type_identity_t<Index> end, Body &&body)
{
    ParallelFor<Split::Adaptive>(begin, end, Index{1}, std::forward<Body>(body));
}

// 스레드마다 하나씩 만드는 작업 공간. Local()은 처음 부른 스레드에서 기본 생성한다.
// 순회(begin/end)는 병렬 작업이 모두 끝난 뒤에만 한다.
template <class T> class PerWorker
{
  public:
    T &Local() { return m_values.local(); }
    auto begin() { return m_values.begin(); }
    auto end() { return m_values.end(); }
    auto begin() const { return m_values.begin(); }
    auto end() const { return m_values.end(); }

  private:
    tbb::enumerable_thread_specific<T> m_values;
};

// 살아 있는 동안 병렬 작업에 참여하는 스레드 수를 제한한다(호출 스레드 포함). 여러 개면 가장 작은 값이
// 적용된다.
class ConcurrencyLimit
{
  public:
    explicit ConcurrencyLimit(std::size_t threads)
        : m_control(tbb::global_control::max_allowed_parallelism, threads)
    {
    }

  private:
    tbb::global_control m_control;
};

// 지금 병렬 작업에 참여할 수 있는 최대 스레드 수(호출 스레드 포함).
inline std::size_t MaxConcurrency()
{
    return tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism);
}

// 이 객체로 실행한 함수 안의 ParallelFor는 호출 스레드 하나에서만 돈다. 백그라운드 로더가
// 렌더러의 worker를 빼앗지 않도록 쓴다.
class IsolatedRunner
{
  public:
    template <class F> decltype(auto) Run(F &&function) { return m_arena.execute(std::forward<F>(function)); }

  private:
    tbb::task_arena m_arena{1, 1, tbb::task_arena::priority::low};
};
} // namespace sr::jobs
