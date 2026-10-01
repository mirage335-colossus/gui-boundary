#pragma once
#include "gui/retained_adapter.hpp"
#include "gui/runtime.hpp"
#include <memory>

namespace gui::rev {
class Probe;
// No module, window-system, or graphics types cross this implementation header.
// Like the native event loop, every operation belongs to the creating thread.
class Adapter final : public RetainedAdapter {
public:
    explicit Adapter(EventSink sink = {});
    ~Adapter() override;
    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;
    void present(Snapshot snapshot) override;
    Size measure_text(const TextMeasureRequest& request) const override;
    bool focus(std::optional<WidgetKey> key) override;
    bool focus_next(bool reverse = false) override;
    void text_selection(const WidgetKey& key, TextSelection selection) override;
    TextSelection text_selection(const WidgetKey& key) const override;
    void scroll(const WidgetKey& key, Point offset) override;
    Point scroll_offset(const WidgetKey& key) const override;
    bool open_popup(const WidgetKey& key) override;
    void close_popup(const WidgetKey& key) override;
    void invalidate(const WidgetKey& key, PixelRect damage) override;
    void close() override;
    void show();
    void sync();
    bool pump();
    const std::string& error() const;
    bool service_active() const;
    bool service(ServiceRequest request, std::function<void(ServiceResult)> completion);
    // Reads the actual rendered OpenGL target, with top-to-bottom opaque RGB.
    BitmapImage capture();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class Probe;
};
}
