//
// Copyright (c) 2026 xiaozhuai
//

#pragma once

#include <cassert>
#include <concepts>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

enum class QueueFullPolicy {
    block,
    drop_oldest,
    drop_newest,
};

struct QueueConfig {
    // A capacity of zero keeps the queue unbounded.
    std::size_t capacity = 0;
    QueueFullPolicy full_policy = QueueFullPolicy::block;
};

enum class SubmitResult {
    accepted,
    dropped,
    not_accepting,
};

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(QueueConfig config = {}) : capacity_(config.capacity), full_policy_(config.full_policy) {}
    BlockingQueue(const BlockingQueue &) = delete;
    BlockingQueue &operator=(const BlockingQueue &) = delete;

    [[nodiscard]] bool configure(QueueConfig config) {
        std::lock_guard lock(mutex_);
        if (used_) {
            return false;
        }

        capacity_ = config.capacity;
        full_policy_ = config.full_policy;
        return true;
    }

    [[nodiscard]] SubmitResult push(T value) {
        std::unique_lock lock(mutex_);
        used_ = true;

        if (capacity_ != 0 && full_policy_ == QueueFullPolicy::block) {
            not_full_condition_.wait(lock, [this] { return closed_ || queue_.size() < capacity_; });
        }

        if (closed_) {
            return SubmitResult::not_accepting;
        }

        if (capacity_ != 0 && queue_.size() >= capacity_) {
            if (full_policy_ == QueueFullPolicy::drop_newest) {
                return SubmitResult::dropped;
            } else if (full_policy_ == QueueFullPolicy::drop_oldest) {
                queue_.pop_front();
            }
        }

        queue_.push_back(std::move(value));
        lock.unlock();
        not_empty_condition_.notify_one();
        return SubmitResult::accepted;
    }

    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        used_ = true;
        not_empty_condition_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        not_full_condition_.notify_one();
        return value;
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            used_ = true;
            closed_ = true;
        }
        not_empty_condition_.notify_all();
        not_full_condition_.notify_all();
    }

    void cancel() {
        {
            std::lock_guard lock(mutex_);
            used_ = true;
            closed_ = true;
            queue_.clear();
        }
        not_empty_condition_.notify_all();
        not_full_condition_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable not_empty_condition_;
    std::condition_variable not_full_condition_;
    std::deque<T> queue_;
    std::size_t capacity_ = 0;
    QueueFullPolicy full_policy_ = QueueFullPolicy::block;
    bool used_ = false;
    bool closed_ = false;
};

class JobBase {
public:
    virtual ~JobBase() = default;
    virtual void start() = 0;
    virtual void close_input() = 0;
    virtual void cancel_input() = 0;
    virtual void join() = 0;
};

struct NoopJobHook {
    void operator()() const noexcept {}
};

template <typename OnTask, typename OnStart, typename OnFinish>
struct JobConfig {
    OnTask on_task;
    OnStart on_start;
    OnFinish on_finish;
    QueueConfig input_queue{};
};

template <typename T>
struct JobTaskResultTraits {
    using Output = T;
    static constexpr bool interruptible = false;
};

template <typename T>
struct JobTaskResultTraits<std::optional<T>> {
    using Output = T;
    static constexpr bool interruptible = true;
};

template <typename Input, typename Output, typename Task, typename OnStart, typename OnFinish>
class Job final : public JobBase {
public:
    Job(std::shared_ptr<BlockingQueue<Input>> input, std::shared_ptr<BlockingQueue<Output>> output, Task task,
        OnStart on_start, OnFinish on_finish)
        : input_(std::move(input)),
          output_(std::move(output)),
          task_(std::move(task)),
          on_start_(std::move(on_start)),
          on_finish_(std::move(on_finish)) {}

    void start() override {
        thread_ = std::thread([this] { run(); });
    }

    void close_input() override { input_->close(); }

    void cancel_input() override { input_->cancel(); }

    void join() override {
        if (thread_.joinable()) {
            assert(thread_.get_id() != std::this_thread::get_id() &&
                   "Workflow must be waited for and destroyed outside its Job threads");
            thread_.join();
        }
    }

private:
    void run() {
        std::invoke(on_start_);

        while (auto value = input_->pop()) {
            using TaskResult = std::remove_cvref_t<std::invoke_result_t<Task &, Input>>;
            if constexpr (JobTaskResultTraits<TaskResult>::interruptible) {
                auto result = std::invoke(task_, std::move(*value));
                if (result && output_->push(std::move(*result)) == SubmitResult::not_accepting) {
                    break;
                }
            } else {
                if (output_->push(std::invoke(task_, std::move(*value))) == SubmitResult::not_accepting) {
                    break;
                }
            }
        }

        std::invoke(on_finish_);

        // Propagate end-of-stream to the next job.
        input_->close();
        output_->close();
    }

    std::shared_ptr<BlockingQueue<Input>> input_;
    std::shared_ptr<BlockingQueue<Output>> output_;
    Task task_;
    OnStart on_start_;
    OnFinish on_finish_;
    std::thread thread_;
};

template <typename Input, typename Task, typename OnStart, typename OnFinish>
class Job<Input, void, Task, OnStart, OnFinish> final : public JobBase {
public:
    Job(std::shared_ptr<BlockingQueue<Input>> input, Task task, OnStart on_start, OnFinish on_finish)
        : input_(std::move(input)),
          task_(std::move(task)),
          on_start_(std::move(on_start)),
          on_finish_(std::move(on_finish)) {
        static_assert(std::is_void_v<std::invoke_result_t<Task &, Input>>, "A workflow sink task must return void");
    }

    void start() override {
        thread_ = std::thread([this] { run(); });
    }

    void close_input() override { input_->close(); }

    void cancel_input() override { input_->cancel(); }

    void join() override {
        if (thread_.joinable()) {
            assert(thread_.get_id() != std::this_thread::get_id() &&
                   "Workflow must be waited for and destroyed outside its Job threads");
            thread_.join();
        }
    }

private:
    void run() {
        std::invoke(on_start_);

        while (auto value = input_->pop()) {
            std::invoke(task_, std::move(*value));
        }

        std::invoke(on_finish_);
        input_->close();
    }

    std::shared_ptr<BlockingQueue<Input>> input_;
    Task task_;
    OnStart on_start_;
    OnFinish on_finish_;
    std::thread thread_;
};

class WorkflowState {
public:
    WorkflowState() = default;
    WorkflowState(const WorkflowState &) = delete;
    WorkflowState &operator=(const WorkflowState &) = delete;

    ~WorkflowState() {
        cancel();
        join_jobs();
    }

    [[nodiscard]] bool add_job(std::unique_ptr<JobBase> job) {
        std::lock_guard lock(mutex_);
        if (started_) {
            return false;
        }
        jobs_.push_back(std::move(job));
        return true;
    }

    void start() {
        std::lock_guard lock(mutex_);
        assert(!started_);
        assert(!jobs_.empty());
        if (started_ || jobs_.empty()) {
            return;
        }

        started_ = true;
        // Start consumers before their producers.
        for (auto &job : std::views::reverse(jobs_)) {
            job->start();
        }
    }

    [[nodiscard]] bool started() const {
        std::lock_guard lock(mutex_);
        return started_;
    }

    void wait() {
        assert(started());
        if (!started()) {
            return;
        }
        join_jobs();
    }

    void cancel() {
        std::lock_guard lock(mutex_);
        for (auto &job : jobs_) {
            job->cancel_input();
        }
    }

private:
    void join_jobs() {
        for (auto &job : jobs_) {
            job->join();
        }
    }

    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<JobBase>> jobs_;
    bool started_ = false;
};

template <typename Input, typename Output, typename... JobInputs>
class Workflow;

template <typename Input, typename... JobInputs>
class Workflow<Input, void, JobInputs...>;

template <typename Input>
auto make_workflow();

template <typename Input, typename Output = Input, typename... JobInputs>
class Workflow {
public:
    // Ownership contract: wait for and destroy the Workflow only from a
    // thread that is not executing one of its Jobs.
    Workflow(const Workflow &) = delete;
    Workflow &operator=(const Workflow &) = delete;
    Workflow(Workflow &&) noexcept = default;
    Workflow &operator=(Workflow &&) noexcept = default;

    template <typename Task>
        requires std::invocable<std::decay_t<Task> &, Output>
    [[nodiscard]] auto then(Task &&task) && {
        return std::move(*this).then(JobConfig{
            .on_task = std::forward<Task>(task),
            .on_start = NoopJobHook{},
            .on_finish = NoopJobHook{},
        });
    }

    template <typename OnTask, typename OnStart, typename OnFinish>
        requires std::invocable<std::decay_t<OnTask> &, Output> && std::invocable<std::decay_t<OnStart> &> &&
                 std::invocable<std::decay_t<OnFinish> &>
    [[nodiscard]] auto then(JobConfig<OnTask, OnStart, OnFinish> config) && {
        using TaskType = std::decay_t<OnTask>;
        using OnStartType = std::decay_t<OnStart>;
        using OnFinishType = std::decay_t<OnFinish>;
        using TaskResult = std::remove_cvref_t<std::invoke_result_t<TaskType &, Output>>;
        static_assert(!std::is_void_v<TaskResult>, "A workflow job must produce a value");
        using NextOutput = JobTaskResultTraits<TaskResult>::Output;

        const bool configured = output_->configure(config.input_queue);
        assert(configured && "A Job input queue must be configured before the Workflow starts");
        (void)configured;

        auto next = std::make_shared<BlockingQueue<NextOutput>>();
        const bool added =
            state_->add_job(std::make_unique<Job<Output, NextOutput, TaskType, OnStartType, OnFinishType>>(
                output_, next, std::move(config.on_task), std::move(config.on_start), std::move(config.on_finish)));
        assert(added);
        if (!added) {
            next->close();
        }

        auto job_inputs = std::tuple_cat(std::move(job_inputs_), std::make_tuple(output_));
        auto state = std::move(state_);
        auto input = std::move(input_);
        output_.reset();
        return Workflow<Input, NextOutput, JobInputs..., Output>(std::move(state), std::move(input), std::move(next),
                                                                 std::move(job_inputs));
    }

    template <typename Task>
        requires std::invocable<std::decay_t<Task> &, Output>
    [[nodiscard]] auto sink(Task &&task) && {
        return std::move(*this).sink(JobConfig{
            .on_task = std::forward<Task>(task),
            .on_start = NoopJobHook{},
            .on_finish = NoopJobHook{},
        });
    }

    template <typename OnTask, typename OnStart, typename OnFinish>
        requires std::invocable<std::decay_t<OnTask> &, Output> && std::invocable<std::decay_t<OnStart> &> &&
                 std::invocable<std::decay_t<OnFinish> &>
    [[nodiscard]] auto sink(JobConfig<OnTask, OnStart, OnFinish> config) && {
        using TaskType = std::decay_t<OnTask>;
        using OnStartType = std::decay_t<OnStart>;
        using OnFinishType = std::decay_t<OnFinish>;
        using TaskResult = std::remove_cvref_t<std::invoke_result_t<TaskType &, Output>>;
        static_assert(std::is_void_v<TaskResult>, "A workflow sink task must return void");

        const bool configured = output_->configure(config.input_queue);
        assert(configured && "A Job input queue must be configured before the Workflow starts");
        (void)configured;

        const bool added = state_->add_job(std::make_unique<Job<Output, void, TaskType, OnStartType, OnFinishType>>(
            output_, std::move(config.on_task), std::move(config.on_start), std::move(config.on_finish)));
        assert(added);
        (void)added;

        auto job_inputs = std::tuple_cat(std::move(job_inputs_), std::make_tuple(output_));
        auto state = std::move(state_);
        auto input = std::move(input_);
        output_.reset();
        return Workflow<Input, void, JobInputs..., Output>(std::move(state), std::move(input), std::move(job_inputs));
    }

    void start() { state_->start(); }

    [[nodiscard]] SubmitResult submit(Input value) {
        if (!state_->started()) {
            return SubmitResult::not_accepting;
        }
        return input_->push(std::move(value));
    }

    template <std::size_t JobIndex, typename Value>
        requires(JobIndex < sizeof...(JobInputs)) &&
                std::constructible_from<std::tuple_element_t<JobIndex, std::tuple<JobInputs...>>, Value &&>
    [[nodiscard]] SubmitResult submit(Value &&value) {
        if (!state_->started()) {
            return SubmitResult::not_accepting;
        }

        using JobInput = std::tuple_element_t<JobIndex, std::tuple<JobInputs...>>;
        return std::get<JobIndex>(job_inputs_)->push(JobInput(std::forward<Value>(value)));
    }

    void close() { input_->close(); }

    void cancel() {
        // Wake an external receive() before waiting for running Jobs to stop.
        output_->cancel();
        state_->cancel();
    }

    [[nodiscard]] std::optional<Output> receive() { return output_->pop(); }

    void wait() {
        close();
        state_->wait();
    }

private:
    template <typename, typename, typename...>
    friend class Workflow;
    template <typename T>
    friend auto make_workflow();

    Workflow(std::shared_ptr<WorkflowState> state, std::shared_ptr<BlockingQueue<Input>> input,
             std::shared_ptr<BlockingQueue<Output>> output,
             std::tuple<std::shared_ptr<BlockingQueue<JobInputs>>...> job_inputs)
        : state_(std::move(state)),
          input_(std::move(input)),
          output_(std::move(output)),
          job_inputs_(std::move(job_inputs)) {}

    std::shared_ptr<WorkflowState> state_;
    std::shared_ptr<BlockingQueue<Input>> input_;
    std::shared_ptr<BlockingQueue<Output>> output_;
    std::tuple<std::shared_ptr<BlockingQueue<JobInputs>>...> job_inputs_;
};

template <typename Input, typename... JobInputs>
class Workflow<Input, void, JobInputs...> {
public:
    // Ownership contract: wait for and destroy the Workflow only from a
    // thread that is not executing one of its Jobs.
    Workflow(const Workflow &) = delete;
    Workflow &operator=(const Workflow &) = delete;
    Workflow(Workflow &&) noexcept = default;
    Workflow &operator=(Workflow &&) noexcept = default;

    void start() { state_->start(); }

    [[nodiscard]] SubmitResult submit(Input value) {
        if (!state_->started()) {
            return SubmitResult::not_accepting;
        }
        return input_->push(std::move(value));
    }

    template <std::size_t JobIndex, typename Value>
        requires(JobIndex < sizeof...(JobInputs)) &&
                std::constructible_from<std::tuple_element_t<JobIndex, std::tuple<JobInputs...>>, Value &&>
    [[nodiscard]] SubmitResult submit(Value &&value) {
        if (!state_->started()) {
            return SubmitResult::not_accepting;
        }

        using JobInput = std::tuple_element_t<JobIndex, std::tuple<JobInputs...>>;
        return std::get<JobIndex>(job_inputs_)->push(JobInput(std::forward<Value>(value)));
    }

    void close() { input_->close(); }

    void cancel() { state_->cancel(); }

    void wait() {
        close();
        state_->wait();
    }

private:
    template <typename, typename, typename...>
    friend class Workflow;

    Workflow(std::shared_ptr<WorkflowState> state, std::shared_ptr<BlockingQueue<Input>> input,
             std::tuple<std::shared_ptr<BlockingQueue<JobInputs>>...> job_inputs)
        : state_(std::move(state)), input_(std::move(input)), job_inputs_(std::move(job_inputs)) {}

    std::shared_ptr<WorkflowState> state_;
    std::shared_ptr<BlockingQueue<Input>> input_;
    std::tuple<std::shared_ptr<BlockingQueue<JobInputs>>...> job_inputs_;
};

template <typename Input>
auto make_workflow() {
    static_assert(std::is_object_v<Input>, "Workflow values must be object types");
    auto queue = std::make_shared<BlockingQueue<Input>>();
    return Workflow<Input, Input>(std::make_shared<WorkflowState>(), queue, queue, std::tuple<>{});
}
