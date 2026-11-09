#ifndef WOOD_TASKS_HPP
#define WOOD_TASKS_HPP
namespace wd {
namespace task_detail {
inline std::atomic<unsigned> active{0};
struct State {
    std::mutex mutex;
    std::condition_variable ready;
    Json result;
    std::exception_ptr failure;
    bool done = false;
    bool closed = false;
    std::jthread worker;
    State() = default;
    State(const State&) = delete;
    void close() noexcept {
        if (worker.joinable()) {
            worker.request_stop();
            worker.join();
        }
        closed = true;
    }
    ~State() {
        close();
    }
};
template <class Work> std::shared_ptr<State> spawn(Work work, Site at) {
    if (active.fetch_add(1) >= 256) {
        active.fetch_sub(1);
        error(at, "R4022", "at most 256 native tasks may run at once");
    }
    bool started = false;
    try {
        auto state = std::make_shared<State>();
        // State owns the joinable thread. Its destructor joins before fields are
        // destroyed, so the worker borrows State without a shared ownership cycle.
        state->worker = std::jthread(
            [owner = state.get(), work = std::move(work)](std::stop_token token) mutable {
                native_detail::stop = token;
                Json result;
                std::exception_ptr failure;
                try {
                    result = work();
                } catch (...) {
                    failure = std::current_exception();
                }
                {
                    std::lock_guard lock(owner->mutex);
                    owner->result = std::move(result);
                    owner->failure = failure;
                    owner->done = true;
                }
                active.fetch_sub(1);
                owner->ready.notify_all();
            });
        started = true;
        return state;
    } catch (const std::system_error&) {
        if (!started)
            active.fetch_sub(1);
        error(at, "R4022", "cannot start native task");
    } catch (...) {
        if (!started)
            active.fetch_sub(1);
        throw;
    }
}
} // namespace task_detail
struct Task {
    std::shared_ptr<task_detail::State> state;
    bool operator==(const Task&) const = default;
};
inline std::string to_str(const Task& task) {
    return !task.state || task.state->closed ? "<task:closed>" : "<task:open>";
}
inline task_detail::State& task_state(const Task& task, Site at) {
    if (!task.state || task.state->closed)
        error(at, "R4022", "task is closed");
    return *task.state;
}
inline Task task_spawn(Json (*function)(Json), const Json& payload, Site at) {
    json_validate(payload, 0, at);
    return {task_detail::spawn([function, payload] { return function(payload); }, at)};
}
inline Task task_spawn_io(Json (*function)(Stream, Json), const Stream& connection,
                          const Json& payload, Site at) {
    json_validate(payload, 0, at);
    auto& old = stream_state(connection, at);
    if (old.kind != stream_detail::Kind::Connection)
        error(at, "R4016", "transfer requires a connected socket");
    auto next = std::make_shared<stream_detail::State>(old.kind);
    auto gate = std::make_shared<std::atomic<bool>>(false);
    auto state = task_detail::spawn(
        [function, owned = Stream{next}, payload, at, gate] {
            gate->wait(false, std::memory_order_acquire);
            StreamScope scope(owned, at);
            return function(owned, payload);
        },
        at);
    // No throwing operations after the worker starts: publish only the fully
    // detached identity, including buffered bytes and stable TLS BIO ownership.
    stream_transfer_into(old, *next);
    gate->store(true, std::memory_order_release);
    gate->notify_one();
    return {std::move(state)};
}
inline Json task_wait(const Task& task, Int timeout, Site at) {
    if (timeout < 1 || timeout > 86400000)
        error(at, "R4022", "task timeout must be in 1..86400000 milliseconds");
    auto& state = task_state(task, at);
    std::unique_lock lock(state.mutex);
    if (!state.ready.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return state.done; }))
        error(at, "R4022", "task wait timed out; task is still running");
    if (state.failure)
        std::rethrow_exception(state.failure);
    return state.result;
}
inline bool task_done(const Task& task, Site at) {
    auto& state = task_state(task, at);
    std::lock_guard lock(state.mutex);
    return state.done;
}
inline void task_cancel(const Task& task, Site at) {
    task_state(task, at).worker.request_stop();
}
inline void task_close(const Task& task, Site) {
    if (task.state)
        task.state->close();
}
inline bool task_cancelled(Site) {
    return native_detail::cancelled();
}
inline void task_checkpoint(Site at) {
    if (native_detail::cancelled())
        error(at, "R4022", "task cancelled");
}
} // namespace wd
#endif
