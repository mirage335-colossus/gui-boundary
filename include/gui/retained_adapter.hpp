#pragma once
#include "memory_adapter.hpp"

namespace gui {
// Common retained semantics for concrete renderers. The application sees only
// Adapter; each renderer owns its drawing, host input, and service execution.
// Keeping this engine shared also keeps validation, identity and editor policy
// identical in terminal, browser, native-widget and software-rendered views.
class RetainedAdapter : public Adapter {
public:
    explicit RetainedAdapter(EventSink sink = {}, TextMeasure measure = {})
        : policy_(std::move(sink), 64 * 1024 * 1024, std::move(measure)) {}
    void present(Snapshot snapshot) override { policy_.present(std::move(snapshot)); }
    Size measure_text(const TextMeasureRequest& request) const override { return policy_.measure_text(request); }
    Availability resolved_availability(const WidgetKey& key) const override { return policy_.resolved_availability(key); }
    std::optional<WidgetKey> focused() const override { return policy_.focused(); }
    bool focus(std::optional<WidgetKey> key) override { return policy_.focus(std::move(key)); }
    bool focus_next(bool reverse = false) override { return policy_.focus_next(reverse); }
    void scroll(const WidgetKey& key, Point offset) override { policy_.scroll(key, offset); }
    Point scroll_offset(const WidgetKey& key) const override { return policy_.scroll_offset(key); }
    TextSelection text_selection(const WidgetKey& key) const override { return policy_.text_selection(key); }
    void text_selection(const WidgetKey& key, TextSelection selection) override { policy_.text_selection(key, selection); }
    bool open_popup(const WidgetKey& key) override { return policy_.open_popup(key); }
    void close_popup(const WidgetKey& key) override { policy_.close_popup(key); }
    void invalidate(const WidgetKey& key, PixelRect damage) override { policy_.invalidate(key, damage); }
    bool closed() const override { return policy_.closed(); }
    void close() override { policy_.close(); }
    const Snapshot& snapshot() const { return policy_.snapshot(); }
    MemoryAdapter& policy() { return policy_; }
    const MemoryAdapter& policy() const { return policy_; }
private:
    MemoryAdapter policy_;
};
}
