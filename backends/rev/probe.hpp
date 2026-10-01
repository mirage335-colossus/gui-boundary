#pragma once
#include "adapter.hpp"

namespace gui::rev {
// Host qualification hooks; these exercise native controls and the same input
// translation as physical events. Application code must use Adapter instead.
class Probe {
public:
    static bool exists(Adapter&, const WidgetKey&);
    static Rect bounds(Adapter&, const WidgetKey&);
    static Rect clip(Adapter&, const WidgetKey&);
    static std::string text(Adapter&, const WidgetKey&);
    static bool visible(Adapter&, const WidgetKey&);
    static bool enabled(Adapter&, const WidgetKey&);
    static TextSelection selection(Adapter&, const WidgetKey&);
    static void activate(Adapter&, const WidgetKey&);
    static void set_checked(Adapter&, const WidgetKey&, bool);
    static void edit_text(Adapter&, const WidgetKey&, std::string, TextSelection = {});
    static void select_option(Adapter&, const WidgetKey&, std::string);
    static void select_record(Adapter&, const WidgetKey&, std::string, bool activate = false);
    static void scroll(Adapter&, const WidgetKey&, Point);
    static void pointer(Adapter&, Point, int button = 1, bool double_click = false);
    static void wheel(Adapter&, Point position, Point detents);
    static void key(Adapter&, std::string, bool control = false, bool shift = false, bool alt = false);
    static void page(Adapter&, std::string);
    static void complete_prompt(Adapter&, std::string, bool cancel = false);
    static std::function<void()> callback_for(Adapter&, const WidgetKey&);
    static void hold_clipboard_read(Adapter&, bool);
    static void complete_clipboard_read(Adapter&, std::optional<std::string>, std::string error = {});
    static void batch(Adapter&, const std::function<void()>&);
};
}
