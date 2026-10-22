#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

// 프레임 루프가 파일을 기다리지 않게 하는 작업 큐다.
//   1. Submit(work, apply): 호출 스레드(프레임 루프)는 바로 돌아온다.
//   2. work는 전용 작업 스레드에서 돈다. 파일 읽기·파싱·Octree 생성처럼 오래 걸리는 일을 여기서 한다.
//      작업 안의 병렬 루프(sr::jobs)는 이 스레드 하나에서만 돌아 렌더러의 worker를 빼앗지 않는다.
//   3. apply는 DrainCompleted를 부른 스레드(FrameRunner 프레임 시작 훅)에서 돈다. 장면 수정은 여기서만 한다.
// 소멸 시 시작하지 않은 작업은 버리고, 도는 중인 작업은 stop_token으로 중단을 요청한 뒤 끝날 때까지 기다린다.
// 꺼내지 않은 결과는 apply 없이 버린다.
namespace sr::io
{
class BackgroundLoader
{
  public:
    struct Options
    {
        // 결과 하나가 큐에 들어올 때마다 작업 스레드에서 호출된다(예: PostMessage로 창 깨우기).
        // 짧고 예외 없는 함수여야 한다.
        std::function<void()> onCompleted{};
        // work가 던진 예외를 DrainCompleted 스레드에서 받는다. 비어 있으면 결과를 버린다.
        std::function<void(const std::string &message)> onError{};
    };

    BackgroundLoader();
    explicit BackgroundLoader(Options options);
    ~BackgroundLoader();
    BackgroundLoader(const BackgroundLoader &) = delete;
    BackgroundLoader &operator=(const BackgroundLoader &) = delete;

    // work(stop_token) -> Result 는 작업 스레드, apply(Result&&)는 DrainCompleted 스레드에서 돈다.
    // Result는 이동 가능하면 된다(unique_ptr 가능).
    template <class Work, class Apply> void Submit(Work work, Apply apply)
    {
        using Result = std::invoke_result_t<Work &, std::stop_token>;
        enqueue([work = std::move(work), apply = std::move(apply)](std::stop_token stop) mutable -> Completion
                {
                    Result result = work(stop);
                    return [apply = std::move(apply), result = std::move(result)]() mutable
                    { apply(std::move(result)); };
                });
    }

    // 끝난 작업의 apply를 끝난 순서대로 실행한다. budget을 넘기면 남은 것은 다음 호출로 미룬다
    // (적어도 하나는 실행한다). 실행한 개수를 돌려준다.
    std::size_t DrainCompleted(std::chrono::microseconds budget = std::chrono::microseconds::max());

    // 제출됐지만 아직 apply되지 않은 작업 수(대기 + 실행 중 + 완료 대기).
    [[nodiscard]] std::size_t PendingCount() const;

  private:
    using Completion = std::move_only_function<void()>;
    using Job = std::move_only_function<Completion(std::stop_token)>;

    void enqueue(Job job);
    void workerLoop(std::stop_token stop);

    Options m_options;
    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    std::deque<Job> m_jobs;
    std::deque<Completion> m_completed;
    std::size_t m_running = 0;
    std::jthread m_worker; // 마지막 멤버: 다른 멤버가 준비된 뒤 시작하고 먼저 정지·join된다
};
} // namespace sr::io
