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
struct Option {
    std::string id,label,value;
    bool enabled=true;
    bool operator==(const Option&) const = default;
};
struct Cell {
    std::string text;
    Rect bounds;
    Font font;
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
    std::string label,text,help,accessible_name,placeholder,display_text;
    Font font;
    bool enabled=true,visible=true,checked=false;
    std::optional<std::string> selected;
    std::vector<Option> options;
    std::vector<Record> records;
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
enum class PointerKind { click,double_click,move,wheel };
struct PointerInput {
    PointerKind kind=PointerKind::click;
    Point position; // Absolute logical client coordinates.
    double wheel_x=0,wheel_y=0; // Positive means right/up, in detents.
    bool control=false,shift=false,alt=false;
};
using Input=std::variant<Activate,SetChecked,EditText,ChooseOption,SelectRecord,ActivateRecord,SubmitText,PointerInput>;
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
    struct Resolved {Availability area;Point children_translation;};
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
            a.clip=intersect(a.clip,parent->area.clip);
            a.visible=a.visible&&parent->area.visible;a.enabled=a.enabled&&parent->area.enabled;
        }
        a.visible=a.visible&&has_area(a.clip);
        if(w.spec.kind==Kind::group&&scroll) {
            const auto offset=scroll(w.spec.key);
            if(!std::isfinite(offset.x)||!std::isfinite(offset.y)||offset.x<0||offset.y<0||
               offset.x>coordinate_limit||offset.y>coordinate_limit)
                throw std::invalid_argument("Invalid group scroll offset");
            translation.x-=offset.x;translation.y-=offset.y;
        }
        resolved.emplace(w.spec.key.id,Resolved{a,translation});
        if(w.spec.key==key)return a;
    }
    return {};
}
inline bool focusable(const Widget& w) {
    return w.spec.kind!=Kind::group&&w.spec.kind!=Kind::label&&
        (w.spec.kind!=Kind::bitmap||w.spec.pointer_input);
}
inline void validate_snapshot(const Snapshot& view) {
    const auto require=[](bool ok,const char* message){if(!ok)throw std::invalid_argument(message);};
    const auto text=[&](const std::string& s){require(valid_utf8(s),"Invalid UTF-8 presentation text");};
    const auto font=[&](const Font& f){require(std::isfinite(f.size)&&f.size>0&&f.size<=1024,"Invalid font size");};
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
        text(s.key.id);text(s.page);text(s.parent);text(s.binding);
        require(!s.key.id.empty()&&s.key.generation!=0&&widgets.emplace(s.key.id,&w).second,"Invalid or duplicate widget key");
        require(s.page.empty()||pages.contains(s.page),"Unknown widget page");
        if(!s.parent.empty()) {
            const auto found=widgets.find(s.parent);
            require(found!=widgets.end()&&found->second!=&w&&found->second->spec.kind==Kind::group,"Parent must be an earlier group");
            require(found->second->spec.page==s.page,"Child and parent must use the same page");
        }
        require(valid_rect(v.bounds)&&valid_rect({0,0,v.content_size.width,v.content_size.height}),"Invalid widget geometry");
        font(v.font);
        for(const auto* value:{&v.label,&v.text,&v.help,&v.accessible_name,&v.placeholder,&v.display_text})text(*value);
        require(std::isfinite(s.row_height)&&s.row_height>0&&s.row_height<=coordinate_limit,"Invalid row height");
        if(s.kind==Kind::text)require(text_error(v.text,s.text_policy).empty(),"Invalid editor state");
        require(v.options.empty()||s.kind==Kind::choice||s.kind==Kind::menu||s.kind==Kind::text,"Options on unsupported widget");
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
        require(double(v.records.size())*s.row_height<=coordinate_limit,"List is too tall");
        for(const auto& record:v.records) {
            text(record.id);text(record.accessible_text);
            require(!record.id.empty()&&ids.insert(record.id).second,"Record IDs must be nonempty and unique");
            for(const auto& cell:record.cells) {
                text(cell.text);font(cell.font);require(valid_rect(cell.bounds),"Invalid record cell geometry");
            }
        }
        text(v.bitmap.source_id);
    }
}
// The adapter owns native objects. The caller retains application state.
// All methods run on the UI thread. present() and focus/scroll never emit input.
class Adapter {
public:
    virtual ~Adapter()=default;
    virtual void present(Snapshot snapshot)=0;
    virtual bool focus(std::optional<WidgetKey> target)=0;
    virtual void scroll(const WidgetKey& target,Point offset)=0;
    virtual void close()=0;
};
}
