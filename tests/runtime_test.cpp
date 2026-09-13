#include "gui/runtime.hpp"

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

gui::ServiceRequest request(std::uint64_t id) {
    gui::ServiceRequest value;
    value.id = id;
    return value;
}

template<class Exception, class Function>
void require_throws(Function action, const char* message) {
    try {
        action();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void service_order_and_identity() {
    gui::ServiceQueue queue;
    require(queue.enqueue({0, gui::ServiceKind::prompt, "Enter text", "First"}), "ID zero rejected");
    require(queue.enqueue({9, gui::ServiceKind::save_file, "Choose a file", "example.txt"}), "Second request rejected");
    require(!queue.enqueue(request(9)), "Duplicate pending ID accepted");
    const auto first = queue.begin_next();
    require(first && first->id == 0 && first->value == "First", "First request changed");
    require(!queue.begin_next(), "Active service began twice");
    require(!queue.enqueue(request(0)), "Active ID accepted again");

    gui::ServiceResult wrong{9, gui::ServiceStatus::success, "Wrong", ""};
    require(!queue.complete(wrong), "Out-of-order reply accepted");
    require(wrong.value == "Wrong" && queue.current()->id == 0, "Stale reply changed state");
    gui::ServiceResult reply{0, gui::ServiceStatus::success, "Completed", ""};
    require(queue.complete(reply) && reply.status == gui::ServiceStatus::success, "Matching reply failed");
    require(!queue.complete(reply) && !queue.enqueue(request(0)), "Finished ID reused");
    require(first->value == "First", "Retained request changed after completion");

    require(queue.begin_next()->id == 9, "FIFO order changed");
    reply = {9, gui::ServiceStatus::cancelled, "Ignored", "Ignored"};
    require(queue.complete(reply) && reply.status == gui::ServiceStatus::cancelled, "Cancellation changed");
    require(reply.value.empty() && reply.error.empty() && !queue.begin_next(), "Cancellation retained data");
}

gui::ServiceResult reply_to(gui::ServiceKind kind, std::string value,
                            std::size_t byte_limit = 8,
                            gui::ServiceStatus status = gui::ServiceStatus::success,
                            std::string error = {}) {
    gui::ServiceQueue queue;
    require(queue.enqueue({1, kind, "Input", "", byte_limit}), "Request rejected");
    require(queue.begin_next().has_value(), "Request did not begin");
    gui::ServiceResult result{1, status, std::move(value), std::move(error)};
    require(queue.complete(result) && !queue.current(), "Reply did not finish");
    return result;
}

void service_text_and_status() {
    const std::vector<std::string> invalid{
        std::string("a\0b", 3), std::string("\xc3", 1), std::string("\x80", 1),
        std::string("\xc0\xaf", 2), std::string("\xe0\x80\xaf", 3),
        std::string("\xed\xa0\x80", 3), std::string("\xf0\x80\x80\xaf", 4),
        std::string("\xf4\x90\x80\x80", 4), std::string("\xf5\x80\x80\x80", 4),
        std::string("\xe2\x28\xa1", 3), std::string("\xf0\x9f\x98", 3)
    };
    for (const auto kind : {gui::ServiceKind::prompt, gui::ServiceKind::open_file, gui::ServiceKind::save_file}) {
        for (const auto& value : invalid) {
            const auto result = reply_to(kind, value);
            require(result.status == gui::ServiceStatus::error && result.value.empty() && !result.error.empty(),
                    "Invalid text escaped validation");
        }
        for (const auto& value : {std::string{}, std::string("caf\xc3\xa9"),
                                  std::string("\xf0\x9f\x98\x80"), std::string("\xf4\x8f\xbf\xbf")}) {
            const auto result = reply_to(kind, value);
            require(result.status == gui::ServiceStatus::success && result.value == value, "Valid text changed");
        }
        require(reply_to(kind, "12345", 4).status == gui::ServiceStatus::error, "Byte limit ignored");
        require(reply_to(kind, "caf\xc3\xa9", 4).status == gui::ServiceStatus::error, "Bytes counted as characters");
        require(reply_to(kind, "caf\xc3\xa9", 5).status == gui::ServiceStatus::success, "Exact byte limit rejected");
        require(reply_to(kind, "", 0).status == gui::ServiceStatus::success, "Empty zero-limit text rejected");
        require(reply_to(kind, "x", 0).status == gui::ServiceStatus::error, "Zero limit ignored");
        require(reply_to(kind, std::string(40000, 'x'), 40000).status == gui::ServiceStatus::success,
                "Custom limit replaced by a fixed limit");
        const auto newline = reply_to(kind, "a\nb");
        require(newline.status == (kind == gui::ServiceKind::prompt ? gui::ServiceStatus::error : gui::ServiceStatus::success),
                "File path and prompt line policies confused");
        const auto cancelled = reply_to(kind, invalid.front(), 0, gui::ServiceStatus::cancelled, "Ignored");
        require(cancelled.status == gui::ServiceStatus::cancelled && cancelled.error.empty(), "Cancelled text validated");
        const auto failed = reply_to(kind, invalid.front(), 0, gui::ServiceStatus::error, "Native failure");
        require(failed.status == gui::ServiceStatus::error && failed.error == "Native failure" && failed.value.empty(),
                "Native error changed");
    }
    for (const auto kind : {gui::ServiceKind::clipboard_write, gui::ServiceKind::open_location}) {
        gui::ServiceQueue queue;
        require(queue.enqueue({0, kind, "Output", "Text\nvalue", 0}), "Output request acquired input limit");
        require(queue.begin_next()->value == "Text\nvalue", "Output text changed before dispatch");
        gui::ServiceResult result{0, gui::ServiceStatus::success, "Unused", ""};
        require(queue.complete(result) && result.value.empty(), "Output success retained irrelevant input");
    }
    require(reply_to(gui::ServiceKind::prompt, "", 8, gui::ServiceStatus::error).error == "Platform service failed.",
            "Error reply lost diagnostic");
    require(reply_to(gui::ServiceKind::prompt, "", 8, gui::ServiceStatus::success, "Failure").status == gui::ServiceStatus::error,
            "Contradictory success accepted");
    require(reply_to(gui::ServiceKind::prompt, "", 8, static_cast<gui::ServiceStatus>(99)).status == gui::ServiceStatus::error,
            "Invalid status accepted");
    require(reply_to(gui::ServiceKind::prompt, "", 8, gui::ServiceStatus::error, std::string("\xc3", 1)).error ==
                "Platform service returned invalid error text.", "Invalid native error text escaped validation");

    gui::ServiceQueue queue;
    require_throws<std::invalid_argument>([&] { queue.enqueue({2, gui::ServiceKind::prompt, "Title", "a\nb"}); },
                                         "Invalid default accepted");
    require(queue.enqueue(request(2)), "Invalid default consumed ID");
    require_throws<std::invalid_argument>([&] { queue.enqueue({3, static_cast<gui::ServiceKind>(99), {}, {}}); },
                                         "Unknown service accepted");
    require_throws<std::invalid_argument>([&] { queue.enqueue({4, gui::ServiceKind::prompt, std::string("a\0b", 3), {}}); },
                                         "Invalid title accepted");
}

void service_shutdown() {
    for (const bool start : {false, true}) {
        gui::ServiceQueue queue;
        require(queue.enqueue(request(0)), "First request rejected");
        require(queue.enqueue({1, gui::ServiceKind::clipboard_write, "Copy", "Pending"}), "Output rejected");
        if (start) require(queue.begin_next().has_value(), "Dialog did not begin");
        queue.shutdown();
        queue.shutdown();
        require(queue.closed() && !queue.current() && !queue.begin_next(), "Shutdown kept work");
        gui::ServiceResult result{0, gui::ServiceStatus::success, "Late", ""};
        require(!queue.complete(result) && result.value == "Late", "Late reply changed state");
        require(!queue.enqueue(request(2)) && !queue.enqueue(request(0)), "Closed service queue reopened");
    }
}

void ui_order_limits_and_errors() {
    require_throws<std::invalid_argument>([] { gui::UiQueue queue(0); }, "Zero capacity accepted");
    gui::UiQueue queue(3);
    std::vector<int> values;
    require(!queue.post({}), "Empty task accepted");
    require(queue.post([&] { values.push_back(1); }), "First task rejected");
    require(queue.post([] { throw std::runtime_error("Task failure"); }), "Throwing task rejected");
    require(queue.post([&] { values.push_back(3); }), "Third task rejected");
    require(!queue.post([] {}), "Full queue accepted work");
    require(queue.drain(0).executed == 0 && queue.pending() == 3, "Zero drain consumed work");
    const auto first = queue.drain(2);
    require(first.executed == 2 && first.errors.size() == 1 && values == std::vector<int>{1}, "Bounded drain failed");
    require_throws<std::runtime_error>([&] { std::rethrow_exception(first.errors.front()); }, "Exception was not retained");
    require(queue.drain(10).executed == 1 && values == std::vector<int>({1, 3}), "Error stopped later work");
    require(queue.post([&] { queue.drain(1); }), "Recursive task rejected prematurely");
    const auto recursive = queue.drain(1);
    require(recursive.errors.size() == 1, "Recursive drain allowed");
    require_throws<std::logic_error>([&] { std::rethrow_exception(recursive.errors.front()); }, "Wrong recursion error");
    require(queue.post([&] { values.push_back(4); }), "Queue stuck after exception");
    require(queue.drain(1).executed == 1 && values.back() == 4, "Drain state not restored");

    std::function<void()> repeat;
    unsigned count = 0;
    repeat = [&] { ++count; require(queue.post(repeat), "Self-post rejected"); };
    require(queue.post(repeat), "Repeating task rejected");
    require(queue.drain(7).executed == 7 && count == 7 && queue.pending() == 1, "New work bypassed drain limit");
    queue.shutdown();
}

void ui_threads_and_shutdown() {
    gui::UiQueue queue(500);
    constexpr unsigned workers = 4;
    constexpr unsigned per_worker = 100;
    unsigned executed = 0;
    std::atomic<unsigned> rejected{0};
    std::vector<std::thread> producers;
    const auto owner = std::this_thread::get_id();
    for (unsigned n = 0; n < workers; ++n) {
        producers.emplace_back([&] {
            for (unsigned i = 0; i < per_worker; ++i) {
                if (!queue.post([&] {
                    require(std::this_thread::get_id() == owner, "Task ran on a producer");
                    ++executed;
                })) ++rejected;
            }
        });
    }
    for (auto& producer : producers) producer.join();
    require(rejected == 0 && queue.pending() == workers * per_worker, "Concurrent posts lost work");
    bool wrong_thread_rejected = false;
    std::thread wrong([&] {
        try { queue.drain(1); }
        catch (const std::logic_error&) { wrong_thread_rejected = true; }
    });
    wrong.join();
    require(wrong_thread_rejected && queue.pending() == workers * per_worker, "Wrong thread consumed work");
    const auto drained = queue.drain(500);
    require(drained.errors.empty() && executed == workers * per_worker, "UI drain lost work");

    auto captured = std::make_shared<int>(1);
    const std::weak_ptr<int> weak = captured;
    require(queue.post([captured] {}), "Captured task rejected");
    captured.reset();
    require(!weak.expired(), "Queued capture was not retained");
    std::thread stopper([&] { queue.shutdown(); });
    stopper.join();
    require(weak.expired() && queue.closed() && queue.pending() == 0, "Shutdown retained captures");
    require(!queue.post([] {}) && queue.drain(10).executed == 0, "Closed UI queue reopened");

    gui::UiQueue during;
    unsigned after_close = 0;
    require(during.post([&] { during.shutdown(); ++after_close; }), "Closing task rejected");
    require(during.post([&] { after_close += 100; }), "Pending task rejected");
    require(during.drain(10).executed == 1 && after_close == 1, "Shutdown interrupted current task or ran pending work");

    gui::UiQueue cleanup;
    struct Capture {
        gui::UiQueue& queue;
        bool& destroyed;
        ~Capture() { destroyed = queue.closed() && queue.pending() == 0; }
    };
    bool destroyed = false;
    auto retained = std::make_shared<Capture>(cleanup, destroyed);
    require(cleanup.post([retained] {}), "Cleanup task rejected");
    retained.reset();
    cleanup.shutdown();
    require(destroyed, "Captured destructor could not inspect shutdown queue");
}

} // namespace

int main() {
    try {
        service_order_and_identity();
        service_text_and_status();
        service_shutdown();
        ui_order_limits_and_errors();
        ui_threads_and_shutdown();
        std::cout << "Runtime contract checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
