#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace gui::rev {
struct ClipboardResult {
    // An engaged empty string is valid text, distinct from a failed read.
    std::optional<std::string> text;
    std::string error;
};

// Native text service. Use only from the creating thread. Completions run from
// poll(), never paste(); cancel/destruction discard a pending completion.
class Clipboard {
public:
    Clipboard();
    ~Clipboard();
    Clipboard(const Clipboard&) = delete;
    Clipboard& operator=(const Clipboard&) = delete;
    void copy(const std::string& text);
    void paste(std::function<void(ClipboardResult)> completion);
    void poll();
    void cancel() noexcept;
    bool pending() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
