#include "IO/BackgroundLoader.h"

#include "Utils/Jobs.h"

#ifdef _WIN32
#include "Platform/Win32Headers.h"
#endif

namespace sr::io
{
BackgroundLoader::BackgroundLoader() : BackgroundLoader(Options{}) {}

BackgroundLoader::BackgroundLoader(Options options)
    : m_options(std::move(options)), m_worker([this](std::stop_token stop) { workerLoop(stop); })
{
}

BackgroundLoader::~BackgroundLoader()
{
    // 대기 중인 worker를 깨워 끝낸다. m_worker는 마지막 멤버라 가장 먼저 파괴(join)되므로
    // 큐와 mutex는 worker가 끝날 때까지 살아 있다.
    m_worker.request_stop();
}

void BackgroundLoader::enqueue(Job job)
{
    {
        const std::scoped_lock lock(m_mutex);
        m_jobs.push_back(std::move(job));
    }
    m_wake.notify_one();
}

void BackgroundLoader::workerLoop(std::stop_token stop)
{
#ifdef _WIN32
    // 렌더 worker와 코어를 다툴 때 OS가 프레임 쪽을 먼저 돌리게 한다.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    // 로더 내부의 ParallelFor(예: ModelLoader의 공간 데이터 생성)가 렌더러의 전역 worker를
    // 쓰지 않도록 이 스레드 하나에서만 실행한다.
    sr::jobs::IsolatedRunner isolated;

    while (true)
    {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            if (!m_wake.wait(lock, stop, [this] { return !m_jobs.empty(); }))
                return; // 정지 요청
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
            ++m_running;
        }

        Completion completion;
        try
        {
            isolated.Run([&] { completion = job(stop); });
        }
        catch (const std::exception &error)
        {
            completion = [this, message = std::string(error.what())]
            {
                if (m_options.onError)
                    m_options.onError(message);
            };
        }
        catch (...)
        {
            completion = [this]
            {
                if (m_options.onError)
                    m_options.onError("unknown error in background job");
            };
        }

        {
            const std::scoped_lock lock(m_mutex);
            --m_running;
            m_completed.push_back(std::move(completion));
        }
        if (m_options.onCompleted)
            m_options.onCompleted();
    }
}

std::size_t BackgroundLoader::DrainCompleted(std::chrono::microseconds budget)
{
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    std::size_t applied = 0;
    while (true)
    {
        Completion completion;
        {
            const std::scoped_lock lock(m_mutex);
            if (m_completed.empty())
                break;
            completion = std::move(m_completed.front());
            m_completed.pop_front();
        }
        // apply는 잠금 밖에서 돈다. apply 안에서 다시 Submit해도 교착되지 않는다.
        completion();
        ++applied;
        if (std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start) >= budget)
            break;
    }
    return applied;
}

std::size_t BackgroundLoader::PendingCount() const
{
    const std::scoped_lock lock(m_mutex);
    return m_jobs.size() + m_running + m_completed.size();
}
} // namespace sr::io
