#include "Utils/Jobs.h"

#include <bit>
#include <chrono>
#include <cstdio>
#include <cwchar>
#include <set>
#include <stdexcept>
#include <thread>
#include <immintrin.h>
#ifdef _WIN32
#include "Platform/Win32Headers.h"
#else
#include <pthread.h>
#endif

// 자체 스레드 풀.
// - worker는 (하드웨어 스레드 - 1)개를 처음 쓸 때 만들고 프로세스가 끝날 때까지 둔다. 호출 스레드도 참여한다.
// - 작업 하나 = 조각 n개. 참여자마다 연속 구간을 주고, 자기 구간을 다 쓰면 남은 것이 가장 많은 구간의
//   뒤쪽 절반을 훔친다(TBB의 범위 분할 훔치기와 같은 모양을 공유 카운터 없이 만든다).
// - 일이 끝난 worker는 잠깐 돌며(pause) 다음 작업을 기다리고, 오래 없으면 자기 표식(atomic::wait)에서 잠든다.
//   작업을 공개하면 필요한 helper 수만큼만 골라 깨운다.
// - 작업은 고정 칸(slots)에 공개한다. 중첩 작업(스키닝의 primitive 안 정점 분할)과 여러 호출 스레드가
//   동시에 낸 작업도 각자 칸을 쓴다. 칸이 다 차면 그 작업은 호출 스레드 혼자 처리한다.
#if defined(_MSC_VER)
// C4324: alignas(64)로 캐시 줄을 나눈 구조체가 채워졌다는 안내. 의도한 배치다.
#pragma warning(disable : 4324)
#endif

namespace sr::jobs::detail
{
namespace
{
using Clock = std::chrono::steady_clock;
// 일이 끝난 worker가 다음 작업을 기다리며 도는 시간. 한 프레임 안의 병렬 단계 사이 간격(수~수십 µs)보다
// 길어 프레임 중에는 잠들지 않고, 창이 쉬는 동안에는 곧 잠들어 CPU를 놓는다.
constexpr auto workerSpin = std::chrono::microseconds(200);
// 호출 스레드가 다른 참여자의 마지막 조각을 기다리며 도는 시간. 그 뒤에는 OS 대기로 넘어간다.
constexpr auto ownerSpin = std::chrono::microseconds(50);
// 잠든 worker 표시(비트 i = worker i+1). worker는 최대 maximumParticipants - 1 = 63개라 64비트 하나로 충분하다.
static_assert(maximumParticipants <= 65);

constexpr std::uint64_t pack(std::uint32_t first, std::uint32_t last) noexcept
{
    return static_cast<std::uint64_t>(first) | (static_cast<std::uint64_t>(last) << 32);
}
constexpr std::uint32_t firstOf(std::uint64_t bounds) noexcept
{
    return static_cast<std::uint32_t>(bounds);
}
constexpr std::uint32_t lastOf(std::uint64_t bounds) noexcept
{
    return static_cast<std::uint32_t>(bounds >> 32);
}

// 자기 구간의 앞 조각 하나를 가져간다. 다른 참여자는 뒤쪽만 훔치므로 경합은 끝 무렵에만 생긴다.
bool takeFront(ChunkRange &range, std::uint32_t &chunk) noexcept
{
    auto bounds = range.bounds.load(std::memory_order_relaxed);
    while (true)
    {
        const auto first = firstOf(bounds), last = lastOf(bounds);
        if (first >= last)
            return false;
        if (range.bounds.compare_exchange_weak(bounds, pack(first + 1, last), std::memory_order_relaxed))
        {
            chunk = first;
            return true;
        }
    }
}

// 남은 조각이 가장 많은 구간의 뒤쪽 절반(올림)을 비어 있는 자기 구간으로 옮긴다. 모두 비었으면 false.
// 조각 번호는 한 번만 나가므로 같은 (first, last) 값이 다시 나타나지 않는다(ABA 없음).
bool steal(Job &job, ChunkRange &own) noexcept
{
    while (true)
    {
        std::uint32_t victim = 0, most = 0;
        std::uint64_t victimBounds = 0;
        for (std::uint32_t index = 0; index < job.participants; ++index)
        {
            const auto bounds = job.ranges[index].bounds.load(std::memory_order_relaxed);
            const auto first = firstOf(bounds), last = lastOf(bounds);
            if (last > first && last - first > most)
            {
                most = last - first;
                victim = index;
                victimBounds = bounds;
            }
        }
        if (most == 0)
            return false;
        const auto take = (most + 1) / 2;
        const auto first = firstOf(victimBounds), last = lastOf(victimBounds);
        if (job.ranges[victim].bounds.compare_exchange_strong(victimBounds, pack(first, last - take),
                                                              std::memory_order_relaxed))
        {
            own.bounds.store(pack(last - take, last), std::memory_order_relaxed);
            return true;
        }
    }
}

// 조각이 남아 있는 동안 실행한다. 반환값은 이 참여자가 끝낸 조각 수(예외로 건너뛴 조각 포함).
std::size_t participate(Job &job, std::uint32_t id) noexcept
{
    auto &own = job.ranges[id];
    std::size_t completed = 0;
    std::uint32_t chunk = 0;
    while (true)
    {
        if (!takeFront(own, chunk))
        {
            if (!steal(job, own))
                break;
            continue;
        }
        ++completed;
        // 앞 조각이 실패하면 나머지는 실행하지 않고 개수만 센다.
        if (job.failed.load(std::memory_order_relaxed))
            continue;
        const std::size_t first = static_cast<std::size_t>(chunk) * job.chunkSize;
        const std::size_t last = std::min(first + job.chunkSize, job.itemCount);
        try
        {
            job.run(job.body, first, last);
        }
        catch (...)
        {
            bool expected = false;
            if (job.failed.compare_exchange_strong(expected, true))
                job.error = std::current_exception();
        }
    }
    if (completed != 0 && job.finish && !job.failed.load(std::memory_order_relaxed))
    {
        try
        {
            job.finish(job.body);
        }
        catch (...)
        {
            bool expected = false;
            if (job.failed.compare_exchange_strong(expected, true))
                job.error = std::current_exception();
        }
    }
    return completed;
}

// helper가 끝낸 조각 수를 더한다. 마지막 조각이면 잠든 호출 스레드를 깨운다.
// done 증가와 ownerWaiting 읽기, 호출 스레드의 ownerWaiting 쓰기와 done 읽기가 모두 seq_cst라
// 둘 중 하나는 반드시 상대의 쓰기를 본다(깨우기 유실 없음).
void finishHelper(Job &job, std::size_t completed) noexcept
{
    if (completed == 0)
        return;
    if (job.done.fetch_add(completed) + completed == job.chunkCount && job.ownerWaiting.load())
        job.done.notify_one();
}

std::size_t hardwareThreads() noexcept
{
    static const std::size_t threads =
        std::clamp<std::size_t>(std::thread::hardware_concurrency(), 1, maximumParticipants);
    return threads;
}

std::atomic<std::size_t> activeLimit{std::numeric_limits<std::size_t>::max()};
struct Limits
{
    std::mutex mutex;
    std::multiset<std::size_t> values;
};
Limits &limits()
{
    static auto *instance = new Limits();
    return *instance;
}

// PerWorker 칸 번호. 살아 있는 스레드끼리 겹치지 않고 끝난 스레드의 번호는 다시 쓴다.
class SlotRegistry
{
  public:
    std::size_t Acquire()
    {
        const std::scoped_lock lock(m_mutex);
        for (std::size_t index = 0; index < maximumThreadSlots; ++index)
            if (!m_used[index])
            {
                m_used[index] = true;
                return index;
            }
        throw std::runtime_error("sr::jobs: too many threads use PerWorker");
    }
    void Release(std::size_t index)
    {
        const std::scoped_lock lock(m_mutex);
        m_used[index] = false;
    }

  private:
    std::mutex m_mutex;
    bool m_used[maximumThreadSlots]{};
};
SlotRegistry &registry()
{
    // 스레드 종료(thread_local 소멸)가 정적 소멸 뒤에 와도 안전하도록 해제하지 않는다.
    static auto *instance = new SlotRegistry();
    return *instance;
}
struct ThreadSlotHolder
{
    std::size_t index = registry().Acquire();
    ThreadSlotHolder() = default;
    ThreadSlotHolder(const ThreadSlotHolder &) = delete;
    ThreadSlotHolder &operator=(const ThreadSlotHolder &) = delete;
    ~ThreadSlotHolder() { registry().Release(index); }
};

void nameThread(std::size_t index)
{
    // VTune/VS 프로파일러의 스레드 목록에서 풀 worker를 구분한다. 실패해도 동작에는 영향이 없다.
#ifdef _WIN32
    using SetDescription = HRESULT(WINAPI *)(HANDLE, PCWSTR);
    if (const auto kernel = GetModuleHandleW(L"kernel32.dll"))
        if (const auto function = reinterpret_cast<SetDescription>(
                reinterpret_cast<void *>(GetProcAddress(kernel, "SetThreadDescription"))))
        {
            wchar_t name[32];
            std::swprintf(name, 32, L"sr::jobs worker %zu", index);
            function(GetCurrentThread(), name);
        }
#else
    char name[16];
    std::snprintf(name, sizeof(name), "sr-jobs-%zu", index);
    pthread_setname_np(pthread_self(), name);
#endif
}

class Pool
{
  public:
    static Pool &Instance()
    {
        // worker는 프로세스가 끝날 때까지 산다. 정적 소멸 순서 문제를 피하려고 해제하지 않는다.
        static auto *pool = new Pool();
        return *pool;
    }

    void Run(Job &job, std::size_t threads)
    {
        job.participants = static_cast<std::uint32_t>(std::min(
            {threads, m_workers + 1, static_cast<std::size_t>(job.chunkCount), maximumParticipants}));
        for (std::uint32_t index = 0; index < job.participants; ++index)
        {
            const auto first = static_cast<std::uint64_t>(job.chunkCount) * index / job.participants;
            const auto last = static_cast<std::uint64_t>(job.chunkCount) * (index + 1) / job.participants;
            job.ranges[index].bounds.store(
                pack(static_cast<std::uint32_t>(first), static_cast<std::uint32_t>(last)),
                std::memory_order_relaxed);
        }

        Slot *slot = nullptr;
        for (auto &candidate : m_slots)
        {
            Job *expected = nullptr;
            if (candidate.job.compare_exchange_strong(expected, &job))
            {
                slot = &candidate;
                break;
            }
        }
        if (!slot)
        {
            // 공개할 칸이 없으면 호출 스레드가 모든 구간을 차례로 훔쳐 혼자 끝낸다.
            participate(job, 0);
            rethrowIfFailed(job);
            return;
        }
        m_epoch.fetch_add(1);
        wakeHelpers(job.participants - 1);

        const auto completed = participate(job, 0);
        if (job.done.fetch_add(completed) + completed != job.chunkCount)
            waitDone(job);
        retire(*slot);
        rethrowIfFailed(job);
    }

  private:
    struct alignas(64) Slot
    {
        std::atomic<Job *> job{nullptr};
        std::atomic<std::uint32_t> users{0};
    };
    static constexpr std::size_t slotCount = 32;

    Pool()
    {
        const auto workers = hardwareThreads() - 1;
        for (std::size_t index = 0; index < workers; ++index)
        {
            try
            {
                std::thread(&Pool::workerMain, this, index + 1).detach();
            }
            catch (...)
            {
                break; // 만든 만큼만 쓴다.
            }
            ++m_workers;
        }
    }

    static void rethrowIfFailed(Job &job)
    {
        if (job.failed.load())
            std::rethrow_exception(job.error);
    }

    static void waitDone(Job &job)
    {
        const auto start = Clock::now();
        for (unsigned spin = 1; job.done.load(std::memory_order_acquire) != job.chunkCount; ++spin)
        {
            _mm_pause();
            if ((spin & 255) == 0 && Clock::now() - start > ownerSpin)
            {
                job.ownerWaiting.store(true);
                for (auto done = job.done.load(); done != job.chunkCount; done = job.done.load())
                    job.done.wait(done);
                return;
            }
        }
    }

    // 칸을 닫고, 칸에 들어와 있던 worker가 모두 나간 뒤에 비운다. 그 전에 job(호출자 스택)을 해제하지 않는다.
    // worker는 users 증가 → job 읽기, 여기서는 job 쓰기 → users 읽기 순서라(seq_cst) 늦게 들어온 worker는
    // 닫힘 표시를 보거나 users로 보인다.
    void retire(Slot &slot) noexcept
    {
        slot.job.store(&m_closing);
        while (slot.users.load() != 0)
            _mm_pause();
        slot.job.store(nullptr, std::memory_order_release);
    }

    // 공개된 작업에 참여한다. 조각을 하나라도 끝냈으면 true(곧바로 다른 칸도 다시 본다).
    bool helpAny() noexcept
    {
        bool worked = false;
        for (auto &slot : m_slots)
        {
            if (slot.job.load(std::memory_order_relaxed) == nullptr)
                continue;
            slot.users.fetch_add(1);
            auto *job = slot.job.load();
            if (job && job != &m_closing)
            {
                const auto id = job->helpers.fetch_add(1, std::memory_order_relaxed) + 1;
                if (id < job->participants)
                {
                    const auto completed = participate(*job, id);
                    finishHelper(*job, completed);
                    worked |= completed != 0;
                }
            }
            slot.users.fetch_sub(1, std::memory_order_release);
        }
        return worked;
    }

    void workerMain(std::size_t index)
    {
        nameThread(index);
        while (true)
        {
            // 칸을 보기 전에 세대를 읽는다. 본 뒤에 공개된 작업은 세대가 달라져 아래 대기에서 바로 깬다.
            const auto seen = m_epoch.load();
            if (helpAny())
                continue;
            waitForWork(index, seen);
        }
    }

    // 작업에 필요한 helper 수만큼만 깨운다. 이미 돌며 기다리는 worker는 세대 변화를 스스로 보므로 빼고,
    // 모자란 만큼 잠든 worker를 하나씩 골라 그 worker의 표식에만 알린다.
    // PC 1차(1b514d3)는 작업마다 notify_all로 잠든 worker를 모두 깨웠다. 조각 2~4개짜리 작업(정렬, 메시 몇 개)
    // 에도 23개가 깨어나 200 µs씩 돌았고, 같은 코어의 하이퍼스레드 짝이 하는 래스터를 늦췄다.
    void wakeHelpers(std::uint32_t wanted) noexcept
    {
        const auto spinning = m_spinning.load();
        if (wanted <= spinning)
            return;
        auto missing = wanted - spinning;
        auto mask = m_sleepMask.load();
        while (missing != 0 && mask != 0)
        {
            const auto bit = mask & (~mask + 1);
            if (!m_sleepMask.compare_exchange_weak(mask, mask & ~bit))
                continue;
            auto &wake = m_wake[std::countr_zero(bit)].value;
            wake.store(1, std::memory_order_release);
            wake.notify_one();
            mask &= ~bit;
            --missing;
        }
    }

    void waitForWork(std::size_t index, std::uint32_t seen) noexcept
    {
        m_spinning.fetch_add(1, std::memory_order_relaxed);
        const auto start = Clock::now();
        for (unsigned spin = 1;; ++spin)
        {
            if (m_epoch.load(std::memory_order_relaxed) != seen)
            {
                m_spinning.fetch_sub(1, std::memory_order_relaxed);
                return;
            }
            _mm_pause();
            if ((spin & 1023) == 0 && Clock::now() - start > workerSpin)
                break;
        }
        m_spinning.fetch_sub(1, std::memory_order_relaxed);

        // 공개하는 쪽은 세대 증가 → 표시 읽기, 여기서는 표시 세우기 → 세대 비교 순서라(seq_cst)
        // 둘 중 하나는 상대의 쓰기를 본다.
        const auto bit = std::uint64_t{1} << (index - 1);
        auto &wake = m_wake[index - 1].value;
        m_sleepMask.fetch_or(bit);
        if (m_epoch.load() != seen && (m_sleepMask.fetch_and(~bit) & bit) != 0)
            return; // 아무도 고르기 전에 표시를 스스로 거뒀다.
        // 표시를 거둔 쪽(공개하는 쪽)이 곧 표식을 세운다.
        while (wake.load(std::memory_order_acquire) == 0)
            wake.wait(0);
        wake.store(0, std::memory_order_relaxed);
    }

    struct alignas(64) WakeFlag
    {
        std::atomic<std::uint32_t> value{0};
    };

    Slot m_slots[slotCount];
    alignas(64) std::atomic<std::uint32_t> m_epoch{0};
    alignas(64) std::atomic<std::uint32_t> m_spinning{0};
    alignas(64) std::atomic<std::uint64_t> m_sleepMask{0};
    WakeFlag m_wake[maximumParticipants];
    std::size_t m_workers = 0;
    // 닫는 중인 칸 표시. 실제 작업으로 쓰지 않는다.
    Job m_closing;
};
} // namespace

std::size_t AvailableConcurrency() noexcept
{
    return IsolatedFlag() ? 1 : MaxConcurrency();
}

void Execute(Job &job, std::size_t threads)
{
    Pool::Instance().Run(job, threads);
}

std::size_t ThreadSlot()
{
    thread_local ThreadSlotHolder holder;
    return holder.index;
}

bool &IsolatedFlag() noexcept
{
    thread_local bool isolated = false;
    return isolated;
}
} // namespace sr::jobs::detail

namespace sr::jobs
{
ConcurrencyLimit::ConcurrencyLimit(std::size_t threads) : m_threads(std::max<std::size_t>(threads, 1))
{
    auto &state = detail::limits();
    const std::scoped_lock lock(state.mutex);
    state.values.insert(m_threads);
    detail::activeLimit.store(*state.values.begin(), std::memory_order_relaxed);
}

ConcurrencyLimit::~ConcurrencyLimit()
{
    auto &state = detail::limits();
    const std::scoped_lock lock(state.mutex);
    state.values.erase(state.values.find(m_threads));
    detail::activeLimit.store(state.values.empty() ? std::numeric_limits<std::size_t>::max()
                                                   : *state.values.begin(),
                              std::memory_order_relaxed);
}

std::size_t MaxConcurrency() noexcept
{
    return std::min(detail::activeLimit.load(std::memory_order_relaxed), detail::hardwareThreads());
}
} // namespace sr::jobs
