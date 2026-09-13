#pragma once
#include "bitmap.hpp"
#include "geometry.hpp"
#include "text.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace gui {
struct WidgetKey {
    std::string id;
    std::uint64_t generation=1;
    bool operator==(const WidgetKey&) const = default;
};
enum class Kind { group,label,button,toggle,choice,text,list,bitmap,menu };
enum class Tone { normal,muted,accent,error };
struct Font {
    double size=14;bool bold=false;Tone tone=Tone::normal;
    bool operator==(const Font&) const = default;
};
enum class TextWrap { none,word };
struct TextMeasureRequest {
    std::string text;
    Font font;
    double available_width=0,display_scale=1;
    TextWrap wrap=TextWrap::none;
};
using TextMeasure=std::function<Size(const TextMeasureRequest&)>;
inline void validate_font(const Font& font) {
    if(!std::isfinite(font.size)||font.size<=0||font.size>1024)
        throw std::invalid_argument("Invalid font size");
    switch(font.tone) {
        case Tone::normal:case Tone::muted:case Tone::accent:case Tone::error:return;
    }
    throw std::invalid_argument("Unknown text tone");
}
inline void validate_wrap(TextWrap wrap) {
    switch(wrap) {case TextWrap::none:case TextWrap::word:return;}
    throw std::invalid_argument("Unknown text wrapping policy");
}
inline void validate_measure_request(const TextMeasureRequest& request) {
    validate_font(request.font);validate_wrap(request.wrap);
    if(!valid_utf8(request.text)||!valid_rect({0,0,request.available_width,0}))
        throw std::invalid_argument("Invalid text measurement request");
    (void)device_rect({0,0,0,0},request.display_scale);
}
struct Option {
    std::string id,label,value;
    bool enabled=true;
    bool operator==(const Option&) const = default;
};
struct Cell {
    std::string text;
    Rect bounds;
    Font font;
    TextWrap wrap=TextWrap::none;
    bool operator==(const Cell&) const = default;
};
struct Record {
    std::string id,accessible_text;
    std::vector<Cell> cells;
    bool enabled=true,activatable=false;
    bool operator==(const Record&) const = default;
};
struct WidgetSpec {
    WidgetKey key;
    Kind kind=Kind::label;
    std::string page,parent,binding;
    TextPolicy text_policy;
    bool activate_on_select=false,follow_tail=false,pointer_input=false;
    double row_height=28;
    bool operator==(const WidgetSpec&) const = default;
};
struct BitmapView {
    std::string source_id;
    std::uint64_t revision=0;
    BitmapSource source;
};
struct WidgetState {
    Rect bounds;
    // A group's optional child viewport, relative to its own top-left corner.
    // Its frame remains available; only descendants use this additional clip.
    std::optional<Rect> content_clip;
    std::string label,text,help,accessible_name,placeholder,display_text;
    Font font;
    TextWrap wrap=TextWrap::none;
    bool enabled=true,visible=true,checked=false;
    std::optional<std::string> selected;
    std::vector<Option> options;
    // Named bitmap actions exposed through native keyboard/accessibility UI.
    std::vector<Option> actions;
    std::vector<Record> records;
    // Scrollable content extent measured from the child viewport's top-left.
    // A group's content_clip determines viewport size; padding is not content.
    Size content_size;
    BitmapView bitmap;
};
struct Widget { WidgetSpec spec;WidgetState state; };
struct Page {
    std::string id,label;
    bool enabled=true,visible=true;
};
struct Snapshot {
    std::uint64_t revision=0;
    std::string title;
    Size client_size{640,480};
    double display_scale=1;
    PixelFormat bitmap_format=PixelFormat::rgb24;
    std::vector<Page> pages;
    std::optional<std::string> active_page;
    // Parents precede their children. Bounds are absolute logical client units.
    std::vector<Widget> widgets;
};
struct Activate {};
struct SetChecked {bool value;};
struct EditText {std::string value,base_text;};
struct ChooseOption {std::string id;};
struct SelectRecord {std::string id;};
struct ActivateRecord {std::string id;};
struct SubmitText {};
struct InvokeAction {std::string id;};
enum class PointerKind { click,double_click,move,wheel };
struct PointerInput {
    PointerKind kind=PointerKind::click;
    Point position; // Absolute logical client coordinates.
    double wheel_x=0,wheel_y=0; // Positive means right/up, in detents.
    bool control=false,shift=false,alt=false;
};
using Input=std::variant<Activate,SetChecked,EditText,ChooseOption,SelectRecord,ActivateRecord,SubmitText,InvokeAction,PointerInput>;
struct WidgetEvent {WidgetKey target;Input input;};
struct PageEvent {std::string id;};
struct ResizeEvent {Size client_size;double display_scale=1;};
struct CloseEvent {};
using Event=std::variant<WidgetEvent,PageEvent,ResizeEvent,CloseEvent>;
using EventSink=std::function<void(const Event&)>;

inline const Widget* find_widget(const Snapshot& view,const WidgetKey& key) {
    for(const auto& w:view.widgets)if(w.spec.key==key)return &w;
    return nullptr;
}
struct Availability {
    Rect bounds,clip; // Resolved client bounds and their visible intersection.
    bool visible=false,enabled=false;
};
using ScrollLookup=std::function<Point(const WidgetKey&)>;
// Declared bounds describe the unscrolled layout. A group's positive scroll
// offset moves its descendants left/up, including nested groups; its own frame
// stays fixed. Supplying no lookup resolves the layout at zero scroll.
inline Availability availability(const Snapshot& view,const WidgetKey& key,const ScrollLookup& scroll={}) {
    struct Resolved {Availability area;Point children_translation;Rect children_clip;};
    std::map<std::string,Resolved,std::less<>> resolved;
    for(const auto& w:view.widgets) {
        Point translation;
        const Resolved* parent=nullptr;
        if(!w.spec.parent.empty()) {
            parent=&resolved.at(w.spec.parent);translation=parent->children_translation;
        }
        const Rect bounds{w.state.bounds.x+translation.x,w.state.bounds.y+translation.y,
            w.state.bounds.width,w.state.bounds.height};
        Availability a{bounds,intersect(bounds,{0,0,view.client_size.width,view.client_size.height}),
            w.state.visible,w.state.enabled};
        if(!w.spec.page.empty()) {
            a.visible=a.visible&&view.active_page==w.spec.page;
            for(const auto& page:view.pages)if(page.id==w.spec.page) {
                a.visible=a.visible&&page.visible;a.enabled=a.enabled&&page.enabled;
            }
        }
        if(parent) {
            a.clip=intersect(a.clip,parent->children_clip);
            a.visible=a.visible&&parent->area.visible;a.enabled=a.enabled&&parent->area.enabled;
        }
        a.visible=a.visible&&has_area(a.clip);
        auto children_clip=a.clip;
        if(w.state.content_clip) {
            const auto clip=*w.state.content_clip;
            children_clip=intersect(children_clip,{bounds.x+clip.x,bounds.y+clip.y,clip.width,clip.height});
        }
        if(w.spec.kind==Kind::group&&scroll) {
            const auto offset=scroll(w.spec.key);
            if(!std::isfinite(offset.x)||!std::isfinite(offset.y)||offset.x<0||offset.y<0||
               offset.x>coordinate_limit||offset.y>coordinate_limit)
                throw std::invalid_argument("Invalid group scroll offset");
            translation.x-=offset.x;translation.y-=offset.y;
        }
        resolved.emplace(w.spec.key.id,Resolved{a,translation,children_clip});
        if(w.spec.key==key)return a;
    }
    return {};
}
inline bool focusable(const Widget& w) {
    return w.spec.kind!=Kind::group&&w.spec.kind!=Kind::label&&
        (w.spec.kind!=Kind::bitmap||w.spec.pointer_input||!w.state.actions.empty());
}
inline void validate_snapshot(const Snapshot& view) {
    const auto require=[](bool ok,const char* message){if(!ok)throw std::invalid_argument(message);};
    const auto text=[&](const std::string& s){require(valid_utf8(s),"Invalid UTF-8 presentation text");};
    require(valid_rect({0,0,view.client_size.width,view.client_size.height}),"Invalid client size");
    device_rect({0,0,view.client_size.width,view.client_size.height},view.display_scale);
    (void)pixel_row_bytes(0,view.bitmap_format);
    text(view.title);
    std::set<std::string> pages;
    for(const auto& page:view.pages) {
        text(page.id);text(page.label);
        require(!page.id.empty()&&pages.insert(page.id).second,"Page IDs must be nonempty and unique");
    }
    require(!view.active_page||pages.contains(*view.active_page),"Unknown active page");
    std::map<std::string,const Widget*,std::less<>> widgets;
    for(const auto& w:view.widgets) {
        const auto& s=w.spec;const auto& v=w.state;
        switch(s.kind) {
            case Kind::group:case Kind::label:case Kind::button:case Kind::toggle:case Kind::choice:
            case Kind::text:case Kind::list:case Kind::bitmap:case Kind::menu:break;
            default:throw std::invalid_argument("Unknown widget kind");
        }
        switch(s.text_policy.submit) {
            case SubmitKey::none:case SubmitKey::enter:case SubmitKey::control_enter:break;
            default:throw std::invalid_argument("Unknown text submission policy");
        }
        text(s.key.id);text(s.page);text(s.parent);text(s.binding);
        require(!s.key.id.empty()&&s.key.generation!=0&&widgets.emplace(s.key.id,&w).second,"Invalid or duplicate widget key");
        require(s.page.empty()||pages.contains(s.page),"Unknown widget page");
        if(!s.parent.empty()) {
            const auto found=widgets.find(s.parent);
            require(found!=widgets.end()&&found->second!=&w&&found->second->spec.kind==Kind::group,"Parent must be an earlier group");
            require(found->second->spec.page==s.page,"Child and parent must use the same page");
        }
        require(valid_rect(v.bounds)&&valid_rect({0,0,v.content_size.width,v.content_size.height}),"Invalid widget geometry");
        validate_font(v.font);validate_wrap(v.wrap);
        if(v.content_clip)require(s.kind==Kind::group&&valid_rect(*v.content_clip),"Invalid group content clip");
        for(const auto* value:{&v.label,&v.text,&v.help,&v.accessible_name,&v.placeholder,&v.display_text})text(*value);
        require(std::isfinite(s.row_height)&&s.row_height>0&&s.row_height<=coordinate_limit,"Invalid row height");
        if(s.kind==Kind::text)require(text_error(v.text,s.text_policy).empty(),"Invalid editor state");
        require(v.options.empty()||s.kind==Kind::choice||s.kind==Kind::menu||s.kind==Kind::text,"Options on unsupported widget");
        require(v.actions.empty()||s.kind==Kind::bitmap,"Actions on unsupported widget");
        require(v.records.empty()||s.kind==Kind::list,"Records on unsupported widget");
        require(!v.selected||s.kind==Kind::list||s.kind==Kind::choice,"Selection on unsupported widget");
        if(v.selected){text(*v.selected);require(!v.selected->empty(),"Empty selected ID");}
        std::set<std::string> ids;
        for(const auto& option:v.options) {
            text(option.id);text(option.label);text(option.value);
            require(!option.id.empty()&&ids.insert(option.id).second,"Option IDs must be nonempty and unique");
            if(s.kind==Kind::text)require(text_error(option.value,s.text_policy).empty(),"Invalid suggested text");
        }
        ids.clear();
        for(const auto& action:v.actions) {
            text(action.id);text(action.label);
            require(!action.id.empty()&&!action.label.empty()&&action.value.empty()&&ids.insert(action.id).second,
                    "Action IDs and labels must be nonempty, IDs unique, and values empty");
        }
        ids.clear();
        require(double(v.records.size())*s.row_height<=coordinate_limit,"List is too tall");
        for(const auto& record:v.records) {
            text(record.id);text(record.accessible_text);
            require(!record.id.empty()&&ids.insert(record.id).second,"Record IDs must be nonempty and unique");
            for(const auto& cell:record.cells) {
                text(cell.text);validate_font(cell.font);validate_wrap(cell.wrap);require(valid_rect(cell.bounds),"Invalid record cell geometry");
            }
        }
        text(v.bitmap.source_id);
    }
}
// Shared event policy for an already validated presentation. Scroll offsets
// must match the presentation being checked. A successful normalization may
// convert SelectRecord into its declared combined ActivateRecord; callers must
// discard events for which this returns false. Lifetime and dispatch guards
// remain the adapter's responsibility.
inline bool normalize_widget_event(const Snapshot& view,WidgetEvent& event,const ScrollLookup& scroll={}) {
    const auto* w=find_widget(view,event.target);const auto a=availability(view,event.target,scroll);
    if(!w||!a.visible||!a.enabled)return false;
    const auto kind=w->spec.kind;
    // Convert configured row selection into one combined activation event.
    if(auto* select=std::get_if<SelectRecord>(&event.input);select&&w->spec.activate_on_select)
        for(const auto& row:w->state.records)if(row.id==select->id&&row.activatable) {
            auto id=select->id;event.input=ActivateRecord{std::move(id)};break;
        }
    return std::visit([&](const auto& input)->bool {
        using T=std::decay_t<decltype(input)>;
        if constexpr(std::is_same_v<T,Activate>)return kind==Kind::button;
        else if constexpr(std::is_same_v<T,SetChecked>)return kind==Kind::toggle&&input.value!=w->state.checked;
        else if constexpr(std::is_same_v<T,EditText>)return kind==Kind::text&&!w->spec.text_policy.read_only&&
            input.base_text==w->state.text&&input.value!=w->state.text&&text_error(input.value,w->spec.text_policy).empty();
        else if constexpr(std::is_same_v<T,ChooseOption>) {
            if(kind!=Kind::choice&&kind!=Kind::text&&kind!=Kind::menu)return false;
            if(kind==Kind::text&&w->spec.text_policy.read_only)return false;
            for(const auto& option:w->state.options)if(option.id==input.id)
                return option.enabled&&(kind!=Kind::choice||w->state.selected!=option.id);
            return false;
        } else if constexpr(std::is_same_v<T,SelectRecord>||std::is_same_v<T,ActivateRecord>) {
            if(kind!=Kind::list)return false;
            for(const auto& row:w->state.records)if(row.id==input.id)
                return row.enabled&&(!std::is_same_v<T,ActivateRecord>||row.activatable);
            return false;
        } else if constexpr(std::is_same_v<T,SubmitText>)return kind==Kind::text&&
            !w->spec.text_policy.read_only&&w->spec.text_policy.submit!=SubmitKey::none;
        else if constexpr(std::is_same_v<T,InvokeAction>) {
            if(kind!=Kind::bitmap)return false;
            for(const auto& action:w->state.actions)if(action.id==input.id)return action.enabled;
            return false;
        } else {
            switch(input.kind) {case PointerKind::click:case PointerKind::double_click:case PointerKind::move:case PointerKind::wheel:break;
                default:return false;}
            return w->spec.pointer_input&&contains(a.clip,input.position)&&
                std::isfinite(input.wheel_x)&&std::isfinite(input.wheel_y)&&
                (input.kind!=PointerKind::wheel||input.wheel_x!=0||input.wheel_y!=0);
        }
    },event.input);
}
inline bool normalize_event(const Snapshot& view,Event& event,const ScrollLookup& scroll={}) {
    return std::visit([&](auto& value)->bool {
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,WidgetEvent>)return normalize_widget_event(view,value,scroll);
        else if constexpr(std::is_same_v<T,PageEvent>) {
            for(const auto& page:view.pages)if(page.id==value.id)
                return page.enabled&&page.visible&&view.active_page!=page.id;
            return false;
        } else if constexpr(std::is_same_v<T,ResizeEvent>) {
            return valid_rect({0,0,value.client_size.width,value.client_size.height})&&
                std::isfinite(value.display_scale)&&value.display_scale>0&&value.display_scale<=16;
        } else return true;
    },event);
}
// The adapter owns native objects. The caller retains application state.
// All methods run on the UI thread. present() and focus/scroll never emit input.
class Adapter {
public:
    virtual ~Adapter()=default;
    virtual void present(Snapshot snapshot)=0;
    virtual Size measure_text(const TextMeasureRequest& request) const=0;
    virtual Availability resolved_availability(const WidgetKey& target) const=0;
    virtual std::optional<WidgetKey> focused() const=0;
    virtual bool focus(std::optional<WidgetKey> target)=0;
    virtual bool focus_next(bool reverse=false)=0;
    virtual void scroll(const WidgetKey& target,Point offset)=0;
    virtual Point scroll_offset(const WidgetKey& target) const=0;
    virtual TextSelection text_selection(const WidgetKey& target) const=0;
    virtual void text_selection(const WidgetKey& target,TextSelection selection)=0;
    virtual bool open_popup(const WidgetKey& target)=0;
    virtual void close_popup(const WidgetKey& target)=0;
    virtual void invalidate(const WidgetKey& target,PixelRect damage)=0;
    virtual bool closed() const=0;
    virtual void close()=0;
};
}
