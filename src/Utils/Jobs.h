#pragma once
// 병렬 작업 API. 렌더러/스키닝/로더는 이 헤더만 쓰고 작업 스케줄러를 직접 include하지 않는다.
// 스케줄러를 바꿀 때 호출 측 코드를 고치지 않도록 실제로 쓰는 기능만 둔다.
//
// 구현은 자체 스레드 풀(Jobs.cpp)이다.
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace sr::jobs
{
// 범위를 나누는 방식.
// Adaptive: grain은 최소 조각 크기다. 스레드 수에 맞춰 조각을 키운다.
// Fixed: 조각을 grain 이하로 고정한다. 항목마다 비용 차이가 큰 작업(타일, 명령)에 쓴다.
enum class Split
{
    Adaptive,
    Fixed
};

#if defined(_MSC_VER)
// C4324: alignas(64)로 캐시 줄을 나눈 구조체가 채워졌다는 안내. 의도한 배치다.
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
namespace detail
{
// 한 작업에 참여하는 스레드 수의 상한(호출 스레드 포함). 참여자마다 64B 범위 칸을 스택에 둔다.
inline constexpr std::size_t maximumParticipants = 64;
// 동시에 살아 있는 스레드(풀 worker + 호출 스레드들)의 PerWorker 칸 수.
inline constexpr std::size_t maximumThreadSlots = 256;

// 참여자 하나가 맡은 아직 시작하지 않은 조각 구간 [first, last)를 64비트 하나(하위 32비트 first)에 담는다.
// 주인은 앞에서 하나씩 가져가고, 자기 구간이 빈 참여자는 남은 것이 가장 많은 구간의 뒤쪽 절반을 훔친다.
// 처음에는 참여자마다 연속 구간을 주므로 binning처럼 이웃 명령이 캐시를 공유하는 작업도 TBB처럼 연속으로
// 돈다.
struct alignas(64) ChunkRange
{
    std::atomic<std::uint64_t> bounds{0};
};

struct Job
{
    using RunFunction = void (*)(void *body, std::size_t first, std::size_t last);
    RunFunction run = nullptr;
    void *body = nullptr;
    std::size_t itemCount = 0, chunkSize = 1;
    std::uint32_t chunkCount = 0, participants = 1;
    alignas(64) std::atomic<std::size_t> done{0};
    std::atomic<std::uint32_t> helpers{0};
    std::atomic<bool> failed{false}, ownerWaiting{false};
    std::exception_ptr error;
    ChunkRange ranges[maximumParticipants];
};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// 호출 스레드가 지금 쓸 수 있는 스레드 수. IsolatedRunner 안이면 1이다.
std::size_t AvailableConcurrency() noexcept;
// 조각을 풀에 공개하고 호출 스레드도 참여한 뒤, 모든 조각이 끝나면 첫 예외를 다시 던진다.
void Execute(Job &job, std::size_t threads);
// 살아 있는 스레드마다 다른 번호(0..maximumThreadSlots-1). 스레드가 끝나면 번호를 다시 쓴다.
std::size_t ThreadSlot();
bool &IsolatedFlag() noexcept;
} // namespace detail

// [begin, end)를 조각으로 나눠 body(first, last)를 병렬로 부른다. 모든 조각이 끝난 뒤 반환하며,
// 조각에서 난 예외는 남은 조각을 건너뛰고 호출자에게 다시 던진다(첫 예외 하나).
template <Split split = Split::Adaptive, class Index, class Body>
void ParallelFor(Index begin, std::type_identity_t<Index> end, std::type_identity_t<Index> grain, Body &&body)
{
    if (!(begin < end))
        return;
    const auto itemCount = static_cast<std::size_t>(end - begin);
    const std::size_t minimumChunk = grain > Index{0} ? static_cast<std::size_t>(grain) : 1;
    const std::size_t threads = detail::AvailableConcurrency();
    // 한 스레드뿐이거나 한 조각이면 작업을 공개하지 않고 바로 부른다.
    if (threads <= 1 || itemCount <= minimumChunk)
    {
        body(begin, end);
        return;
    }
    std::size_t chunkSize = minimumChunk;
    if constexpr (split == Split::Adaptive)
        // 참여자당 8조각 정도면 훔치기로 끝을 맞출 여지가 있고 조각마다의 준비 비용은 작다.
        chunkSize = std::max(minimumChunk, itemCount / (threads * 8));
    // 조각 번호는 32비트에 담는다.
    chunkSize = std::max(chunkSize, (itemCount - 1) / std::numeric_limits<std::uint32_t>::max() + 1);

    struct Bound
    {
        std::remove_reference_t<Body> *body;
        Index begin;
    } bound{&body, begin};
    detail::Job job;
    job.run = [](void *context, std::size_t first, std::size_t last)
    {
        const auto &self = *static_cast<Bound *>(context);
        (*self.body)(static_cast<Index>(self.begin + static_cast<Index>(first)),
                     static_cast<Index>(self.begin + static_cast<Index>(last)));
    };
    job.body = &bound;
    job.itemCount = itemCount;
    job.chunkSize = chunkSize;
    job.chunkCount = static_cast<std::uint32_t>((itemCount - 1) / chunkSize + 1);
    detail::Execute(job, threads);
}

// 스레드마다 하나씩 만드는 작업 공간. Local()은 처음 부른 스레드에서 기본 생성한다.
// 칸은 스레드 번호로 바로 찾는다(해시 없음). 순회(begin/end)는 생성 순서이며, 병렬 작업이 모두 끝난 뒤에만
// 한다.
template <class T> class PerWorker
{
    // 서로 다른 스레드의 값이 한 캐시 줄을 나눠 쓰지 않게 칸마다 64B 정렬, 64B 배수 크기로 따로 할당한다.
    struct Cell
    {
        T value{};

        static void *operator new(std::size_t size)
        {
            return ::operator new(roundedSize(size), std::align_val_t{cacheLine});
        }
        static void operator delete(void *pointer) noexcept
        {
            ::operator delete(pointer, std::align_val_t{cacheLine});
        }
        static constexpr std::size_t cacheLine = 64;
        static constexpr std::size_t roundedSize(std::size_t size) noexcept
        {
            return (size + cacheLine - 1) / cacheLine * cacheLine;
        }
    };

    template <class Value> class Iterator
    {
      public:
        using value_type = std::remove_const_t<Value>;
        using difference_type = std::ptrdiff_t;
        Iterator() = default;
        explicit Iterator(typename std::vector<Cell *>::const_iterator position) : m_position(position) {}
        Value &operator*() const { return (*m_position)->value; }
        Value *operator->() const { return &(*m_position)->value; }
        Iterator &operator++()
        {
            ++m_position;
            return *this;
        }
        Iterator operator++(int)
        {
            auto copy = *this;
            ++m_position;
            return copy;
        }
        bool operator==(const Iterator &) const = default;

      private:
        typename std::vector<Cell *>::const_iterator m_position{};
    };

  public:
    PerWorker() : m_cells(new std::atomic<Cell *>[detail::maximumThreadSlots]()) {}
    PerWorker(const PerWorker &) = delete;
    PerWorker &operator=(const PerWorker &) = delete;
    ~PerWorker()
    {
        for (auto *cell : m_order)
            delete cell;
    }

    T &Local()
    {
        auto &entry = m_cells[detail::ThreadSlot()];
        if (auto *cell = entry.load(std::memory_order_acquire)) [[likely]]
            return cell->value;
        return create(entry);
    }
    Iterator<T> begin() { return Iterator<T>(m_order.cbegin()); }
    Iterator<T> end() { return Iterator<T>(m_order.cend()); }
    Iterator<const T> begin() const { return Iterator<const T>(m_order.cbegin()); }
    Iterator<const T> end() const { return Iterator<const T>(m_order.cend()); }

  private:
    T &create(std::atomic<Cell *> &entry)
    {
        auto cell = std::make_unique<Cell>();
        const std::scoped_lock lock(m_mutex);
        m_order.reserve(m_order.size() + 1);
        auto *raw = cell.release();
        m_order.push_back(raw);
        entry.store(raw, std::memory_order_release);
        return raw->value;
    }

    std::unique_ptr<std::atomic<Cell *>[]> m_cells;
    std::vector<Cell *> m_order;
    std::mutex m_mutex;
};

// 살아 있는 동안 병렬 작업에 참여하는 스레드 수를 제한한다(호출 스레드 포함). 여러 개면 가장 작은 값이
// 적용된다. 이미 시작한 작업에는 영향이 없다.
class ConcurrencyLimit
{
  public:
    explicit ConcurrencyLimit(std::size_t threads);
    ~ConcurrencyLimit();
    ConcurrencyLimit(const ConcurrencyLimit &) = delete;
    ConcurrencyLimit &operator=(const ConcurrencyLimit &) = delete;

  private:
    std::size_t m_threads;
};

// 지금 병렬 작업에 참여할 수 있는 최대 스레드 수(호출 스레드 포함).
std::size_t MaxConcurrency() noexcept;

// 이 객체로 실행한 함수 안의 ParallelFor는 호출 스레드 하나에서만 돈다. 백그라운드 로더가
// 렌더러의 worker를 빼앗지 않도록 쓴다.
class IsolatedRunner
{
  public:
    template <class F> decltype(auto) Run(F &&function)
    {
        struct Restore
        {
            bool previous;
            ~Restore() { detail::IsolatedFlag() = previous; }
        } restore{std::exchange(detail::IsolatedFlag(), true)};
        return std::forward<F>(function)();
    }
};

// grain 1, Adaptive.
template <class Index, class Body> void ParallelFor(Index begin, std::type_identity_t<Index> end, Body &&body)
{
    ParallelFor<Split::Adaptive>(begin, end, Index{1}, std::forward<Body>(body));
}
} // namespace sr::jobs
