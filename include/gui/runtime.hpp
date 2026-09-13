#pragma once

#include "text.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gui {

enum class ServiceKind { open_file, save_file, prompt, clipboard_write, open_location };
enum class ServiceStatus { success, cancelled, error };

struct ServiceRequest {
    std::uint64_t id = 0;
    ServiceKind kind = ServiceKind::prompt;
    std::string title;
    std::string value;
    // For prompts and file selections, limits the UTF-8 bytes in defaults/replies.
    // Output-only services do not apply this text-input limit.
    std::size_t byte_limit = 32768;
};

struct ServiceResult {
    std::uint64_t id = 0;
    ServiceStatus status = ServiceStatus::success;
    std::string value;
    std::string error;
};

namespace runtime_detail {

// Editors and service replies share one encoding and byte-limit policy.
inline std::string text_error(std::string_view text, std::size_t limit, bool multiline) {
    return gui::text_error(text,TextPolicy{multiline,false,limit,SubmitKey::none});
}

inline bool input_service(ServiceKind kind) {
    switch (kind) {
        case ServiceKind::prompt:
        case ServiceKind::open_file:
        case ServiceKind::save_file: return true;
        case ServiceKind::clipboard_write:
        case ServiceKind::open_location: return false;
    }
    throw std::invalid_argument("Unknown service kind.");
}

inline std::string input_error(const ServiceRequest& request, std::string_view value) {
    return text_error(value, request.byte_limit, request.kind != ServiceKind::prompt);
}

} // namespace runtime_detail

// Use this queue only on the UI thread. It contains no native service calls.
// IDs, including zero, must be unique for the queue's entire lifetime. Accepted
// IDs remain reserved after completion; create a new queue for a new session.
class ServiceQueue {
public:
    ServiceQueue() = default;
    ServiceQueue(const ServiceQueue&) = delete;
    ServiceQueue& operator=(const ServiceQueue&) = delete;

    // False means closed or an already accepted ID. Invalid text throws without
    // consuming the ID. The caller owns the original request's meaning and any
    // associated content while a native file selection is pending.
    bool enqueue(ServiceRequest request) {
        if (closed_ || accepted_ids_.contains(request.id)) return false;
        const bool has_input = runtime_detail::input_service(request.kind);
        const auto unlimited = std::numeric_limits<std::size_t>::max();
        if (const auto error = runtime_detail::text_error(request.title, unlimited, true);
            !error.empty()) throw std::invalid_argument(error);
        const auto error = has_input
            ? runtime_detail::input_error(request, request.value)
            : runtime_detail::text_error(request.value, unlimited, true);
        if (!error.empty()) throw std::invalid_argument(error);

        const auto id = request.id;
        accepted_ids_.insert(id);
        try {
            pending_.push_back(std::move(request));
        } catch (...) {
            accepted_ids_.erase(id);
            throw;
        }
        return true;
    }

    // Returns a new request exactly once. While one is active, returns nullopt.
    // Returned requests are owned copies and survive completion or shutdown.
    std::optional<ServiceRequest> begin_next() {
        if (closed_ || active_ || pending_.empty()) return std::nullopt;
        auto result = std::optional<ServiceRequest>{pending_.front()};
        active_.emplace(std::move(pending_.front()));
        pending_.pop_front();
        return result;
    }

    std::optional<ServiceRequest> current() const { return active_; }

    // False leaves both the reply and the queue untouched. A matching reply is
    // consumed once. A malformed successful text reply becomes an error reply;
    // cancellation stays cancellation. The adapter delivers the normalized
    // result to the application only when this returns true.
    bool complete(ServiceResult& result) {
        if (!active_ || result.id != active_->id) return false;
        switch (result.status) {
            case ServiceStatus::success:
                if (!result.error.empty()) {
                    result.status = ServiceStatus::error;
                    result.value.clear();
                } else if (runtime_detail::input_service(active_->kind)) {
                    result.error = runtime_detail::input_error(*active_, result.value);
                    if (!result.error.empty()) {
                        result.status = ServiceStatus::error;
                        result.value.clear();
                    }
                } else {
                    result.value.clear();
                }
                break;
            case ServiceStatus::cancelled:
                result.value.clear();
                result.error.clear();
                break;
            case ServiceStatus::error:
                result.value.clear();
                if (result.error.empty()) result.error = "Platform service failed.";
                break;
            default:
                result.status = ServiceStatus::error;
                result.value.clear();
                result.error = "Unknown service result status.";
                break;
        }
        if (result.status == ServiceStatus::error &&
            !runtime_detail::text_error(result.error, std::numeric_limits<std::size_t>::max(), true).empty())
            result.error = "Platform service returned invalid error text.";
        active_.reset();
        return true;
    }

    // Permanently discard queued and active work. The adapter separately closes
    // its native dialog and releases native resources; no reply is fabricated.
    void shutdown() {
        closed_ = true;
        pending_.clear();
        active_.reset();
        accepted_ids_.clear();
    }

    bool closed() const noexcept { return closed_; }

private:
    bool closed_ = false;
    std::deque<ServiceRequest> pending_;
    std::optional<ServiceRequest> active_;
    std::unordered_set<std::uint64_t> accepted_ids_;
};

struct DrainResult {
    std::size_t executed = 0;
    std::vector<std::exception_ptr> errors;
};

// Construct on the UI thread. Producers may call post(), pending(), closed(),
// and shutdown() from any thread. The creating thread alone may call drain().
// Tasks must be short and must not retain unprotected references to widgets.
class UiQueue {
public:
    explicit UiQueue(std::size_t capacity = 1024)
        : owner_(std::this_thread::get_id()), capacity_(capacity) {
        if (capacity == 0) throw std::invalid_argument("Queue capacity must be positive.");
    }
    UiQueue(const UiQueue&) = delete;
    UiQueue& operator=(const UiQueue&) = delete;
    ~UiQueue() { shutdown(); }

    // FIFO follows successful insertion order, including across producers.
    // False means empty task, full queue, or shutdown; it never waits for room.
    // Allocation failure propagates. Callers must handle rejection explicitly.
    bool post(std::function<void()> task) {
        if (!task) return false;
        std::lock_guard lock(mutex_);
        if (closed_ || tasks_.size() >= capacity_) return false;
        tasks_.push_back(std::move(task));
        return true;
    }

    // Run at most max_tasks, releasing the lock before each invocation. Newly
    // posted work may run in the same drain, but cannot exceed that total.
    // Every invoked task counts, including tasks that throw. Task exceptions
    // are returned in invocation order; later tasks still run. A recursive or
    // wrong-thread drain throws logic_error and consumes no work.
    DrainResult drain(std::size_t max_tasks) {
        if (std::this_thread::get_id() != owner_)
            throw std::logic_error("Drain must run on the creating UI thread.");
        if (draining_) throw std::logic_error("Recursive drain is not allowed.");
        struct Reset {
            bool& flag;
            ~Reset() { flag = false; }
        } reset{draining_};
        draining_ = true;
        DrainResult result;
        while (result.executed < max_tasks) {
            std::function<void()> task;
            {
                std::lock_guard lock(mutex_);
                if (closed_ || tasks_.empty()) break;
                task = std::move(tasks_.front());
                tasks_.pop_front();
            }
            ++result.executed;
            try {
                task();
            } catch (...) {
                result.errors.push_back(std::current_exception());
            }
        }
        return result;
    }

    // Pending work is discarded. A task already removed by drain may finish.
    // Destroy captured resources outside the mutex, so their destructors may
    // safely query this queue. Join producers before destroying the queue.
    void shutdown() {
        std::deque<std::function<void()>> discarded;
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
            discarded.swap(tasks_);
        }
    }

    bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

    std::size_t pending() const {
        std::lock_guard lock(mutex_);
        return tasks_.size();
    }

private:
    const std::thread::id owner_;
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
    bool closed_ = false;
    bool draining_ = false;
};

} // namespace gui
