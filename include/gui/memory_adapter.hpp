#pragma once
#include "contract.hpp"
#include <thread>
#include <type_traits>
#include <utility>

namespace gui {
enum class Delivery { ignored,delivered };
enum class ListKey { up,down,space,enter };

// Display-free executable reference. Implements retained GUI state and event
// translation; a native adapter additionally creates, draws and destroys actual
// widgets. No host dialogs or native input devices are simulated implicitly.
// A producer may read retained state while painting, but must not destroy the
// adapter. Adapter mutation and event dispatch during paint are rejected.
class MemoryAdapter final : public Adapter {
public:
    explicit MemoryAdapter(EventSink sink={},std::size_t bitmap_budget=64*1024*1024)
        : owner_(std::this_thread::get_id()),sink_(std::move(sink)),bitmap_budget_(bitmap_budget) {}
    MemoryAdapter(const MemoryAdapter&)=delete;
    MemoryAdapter& operator=(const MemoryAdapter&)=delete;

    void present(Snapshot next) override {
        require_mutable();
        validate_snapshot(next);
        auto generations=generations_;
        // Validate identities before preparing a replacement retained state.
        for(const auto& w:next.widgets) {
            const auto found=entries_.find(w.spec.key.id);
            const bool retained=found!=entries_.end()&&found->second.spec.key==w.spec.key;
            if(retained) {
                if(found->second.spec!=w.spec)throw std::invalid_argument("Widget specification changed without a new generation");
            } else {
                const auto previous=generations.find(w.spec.key.id);
                if(previous!=generations.end()&&w.spec.key.generation<=previous->second)
                    throw std::invalid_argument("Widget generation was reused");
            }
            generations[w.spec.key.id]=w.spec.key.generation;
        }
        std::map<std::string,Entry,std::less<>> entries;
        for(const auto& w:next.widgets) {
            const auto found=entries_.find(w.spec.key.id);
            const bool retained=found!=entries_.end()&&found->second.spec.key==w.spec.key;
            Entry entry=retained?found->second:Entry{};
            entry.spec=w.spec;
            entry.selection=entry.selection.clamped(w.state.text);
            const auto maximum=scroll_maximum(w);
            bool was_at_tail=true;
            if(retained) {
                const auto* old=find_widget(snapshot_,w.spec.key);
                was_at_tail=entry.offset.y>=scroll_maximum(*old).y-2;
            }
            entry.offset.x=std::clamp(entry.offset.x,0.0,maximum.x);
            entry.offset.y=w.spec.follow_tail&&was_at_tail?maximum.y:std::clamp(entry.offset.y,0.0,maximum.y);
            entries.emplace(w.spec.key.id,std::move(entry));
        }
        prepare_bitmaps(next,entries,bitmap_budget_);
        auto focus=focus_;
        if(focus) {
            const auto* w=find_widget(next,*focus);const auto a=resolve_availability(next,entries,*focus);
            if(!w||!focusable(*w)||!a.visible||!a.enabled)focus.reset();
        }
        for(auto& [id,entry]:entries) {
            (void)id;
            const auto a=resolve_availability(next,entries,entry.spec.key);
            if(!a.visible||!a.enabled)entry.popup.reset();
        }
        snapshot_=std::move(next);entries_=std::move(entries);
        generations_=std::move(generations);focus_=std::move(focus);
    }
    const Snapshot& snapshot() const {require_thread();return snapshot_;}
    Availability resolved_availability(const WidgetKey& key) const {
        require_open();return resolve_availability(snapshot_,entries_,key);
    }
    std::optional<WidgetKey> focused() const {require_thread();return focus_;}
    bool focus(std::optional<WidgetKey> target) override {
        require_mutable();
        if(target) {
            const auto* w=find_widget(snapshot_,*target);const auto a=resolve_availability(snapshot_,entries_,*target);
            if(!w||!focusable(*w)||!a.enabled||!a.visible)return false;
        }
        focus_=std::move(target);return true;
    }
    bool focus_next(bool reverse=false) {
        require_mutable();std::vector<WidgetKey> eligible;
        for(const auto& w:snapshot_.widgets) {
            const auto a=resolve_availability(snapshot_,entries_,w.spec.key);
            if(focusable(w)&&a.visible&&a.enabled)eligible.push_back(w.spec.key);
        }
        if(eligible.empty()){focus_.reset();return false;}
        auto found=focus_?std::find(eligible.begin(),eligible.end(),*focus_):eligible.end();
        if(found==eligible.end())focus_=reverse?eligible.back():eligible.front();
        else {
            auto i=static_cast<std::size_t>(found-eligible.begin());
            i=reverse?(i+eligible.size()-1)%eligible.size():(i+1)%eligible.size();focus_=eligible[i];
        }
        return true;
    }
    void scroll(const WidgetKey& key,Point offset) override {
        require_mutable();auto& entry=at(key);const auto* w=find_widget(snapshot_,key);
        if(w->spec.kind!=Kind::list&&w->spec.kind!=Kind::text&&w->spec.kind!=Kind::group)
            throw std::invalid_argument("Widget does not scroll");
        if(!std::isfinite(offset.x)||!std::isfinite(offset.y))throw std::invalid_argument("Invalid scroll offset");
        const auto maximum=scroll_maximum(*w);
        offset={std::clamp(offset.x,0.0,maximum.x),std::clamp(offset.y,0.0,maximum.y)};
        if(w->spec.kind==Kind::group) {
            // Fractional movement can change endpoint-snapped bitmap extents.
            // Prepare grids and interaction state before committing the scroll.
            auto entries=entries_;entries.at(key.id).offset=offset;
            prepare_bitmaps(snapshot_,entries,bitmap_budget_);
            auto focus=focus_;prune_unavailable(snapshot_,entries,focus);
            entries_=std::move(entries);focus_=std::move(focus);
        } else entry.offset=offset;
    }
    Point scroll_offset(const WidgetKey& key) const {require_open();return at(key).offset;}
    TextSelection text_selection(const WidgetKey& key) const {
        require_open();require_kind(key,Kind::text);return at(key).selection;
    }
    void text_selection(const WidgetKey& key,TextSelection selection) {
        require_mutable();require_kind(key,Kind::text);
        at(key).selection=selection.clamped(find_widget(snapshot_,key)->state.text);
    }
    Delivery replace(const WidgetKey& key,std::string inserted) {
        require_mutable();require_kind(key,Kind::text);
        const auto target=key; // The sink may replace storage that owned key.
        const auto* w=find_widget(snapshot_,key);
        const auto candidate=replace_text(w->state.text,at(key).selection,inserted,w->spec.text_policy);
        if(!candidate.error.empty()||!candidate.changed)return Delivery::ignored;
        // The application decides whether to publish candidate.text. Retained
        // native selection is updated only if that exact text is published.
        const auto result=send(WidgetEvent{target,EditText{candidate.text,w->state.text}});
        if(!closed_)if(const auto* current=find_widget(snapshot_,target);current&&current->state.text==candidate.text)
            at(target).selection=candidate.selection;
        return result;
    }
    bool enter(const WidgetKey& key,bool control=false,bool shift=false) {
        require_mutable();const auto* w=find_widget(snapshot_,key);
        if(!w||w->spec.kind!=Kind::text||!is_submit(w->spec.text_policy,control,shift))return false;
        send(WidgetEvent{key,SubmitText{}});return true;
    }
    bool open_popup(const WidgetKey& key) {
        require_mutable();const auto* w=find_widget(snapshot_,key);const auto a=resolve_availability(snapshot_,entries_,key);
        if(!w||!a.enabled||!a.visible||w->state.options.empty()||
           (w->spec.kind!=Kind::choice&&w->spec.kind!=Kind::menu&&w->spec.kind!=Kind::text)||
           (w->spec.kind==Kind::text&&w->spec.text_policy.read_only))return false;
        at(key).popup=w->state.options;return true;
    }
    Delivery choose_popup(const WidgetKey& key,std::size_t index) {
        require_mutable();auto found=entries_.find(key.id);
        if(found==entries_.end()||found->second.spec.key!=key||!found->second.popup)return Delivery::ignored;
        auto displayed=std::move(*found->second.popup);found->second.popup.reset();
        if(index>=displayed.size()||!displayed[index].enabled)return Delivery::ignored;
        return send(WidgetEvent{key,ChooseOption{displayed[index].id}});
    }
    void close_popup(const WidgetKey& key) {require_mutable();at(key).popup.reset();}
    Delivery list_key(const WidgetKey& key,ListKey input) {
        require_mutable();require_kind(key,Kind::list);const auto* w=find_widget(snapshot_,key);
        const auto a=resolve_availability(snapshot_,entries_,key);if(!a.enabled||!a.visible)return Delivery::ignored;
        const auto& rows=w->state.records;
        const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& r){return w->state.selected==r.id;});
        if(input==ListKey::space||input==ListKey::enter) {
            if(found==rows.end()||!found->enabled||(input==ListKey::enter&&!found->activatable))return Delivery::ignored;
            const auto id=found->id;
            reveal_record(key,static_cast<std::size_t>(found-rows.begin()));
            return send(WidgetEvent{key,input==ListKey::enter?Input{ActivateRecord{id}}:Input{SelectRecord{id}}});
        }
        const auto step=input==ListKey::up?-1:1;
        auto index=found==rows.end()?(step>0?0:static_cast<std::ptrdiff_t>(rows.size())-1):
            static_cast<std::ptrdiff_t>(found-rows.begin())+step;
        while(index>=0&&index<static_cast<std::ptrdiff_t>(rows.size())) {
            if(rows[static_cast<std::size_t>(index)].enabled) {
                const auto id=rows[static_cast<std::size_t>(index)].id;
                reveal_record(key,static_cast<std::size_t>(index));
                return send(WidgetEvent{key,SelectRecord{id}});
            }
            index+=step;
        }
        return Delivery::ignored;
    }
    // Entry point used by a native callback after translating its input.
    Delivery send(Event event) {
        require_thread();require_not_painting();if(closed_||dispatching_)return Delivery::ignored;
        const bool accepted=std::visit([&](auto& value)->bool {
            using T=std::decay_t<decltype(value)>;
            if constexpr(std::is_same_v<T,WidgetEvent>)return accept_widget(value);
            else if constexpr(std::is_same_v<T,PageEvent>) {
                for(const auto& p:snapshot_.pages)if(p.id==value.id)return p.enabled&&p.visible&&snapshot_.active_page!=p.id;
                return false;
            } else if constexpr(std::is_same_v<T,ResizeEvent>) {
                return valid_rect({0,0,value.client_size.width,value.client_size.height})&&
                    std::isfinite(value.display_scale)&&value.display_scale>0&&value.display_scale<=16;
            } else return true;
        },event);
        if(!accepted||!sink_)return Delivery::ignored;
        struct Guard {bool& flag;explicit Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(dispatching_);
        auto callback=sink_;callback(event);return Delivery::delivered;
    }
    void invalidate(const WidgetKey& key,PixelRect damage) {
        require_mutable();require_kind(key,Kind::bitmap);at(key).bitmap.invalidate(damage);
    }
    bool repaint(const WidgetKey& key) {
        require_mutable();require_kind(key,Kind::bitmap);
        if(!resolve_availability(snapshot_,entries_,key).visible)return false;
        struct Guard {bool& flag;explicit Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(painting_);
        return at(key).bitmap.repaint();
    }
    const BitmapImage& image(const WidgetKey& key) const {
        require_open();require_kind(key,Kind::bitmap);return at(key).bitmap.image();
    }
    bool closed() const {require_thread();return closed_;}
    void close() override {
        require_thread();require_not_painting();closed_=true;sink_={};focus_.reset();entries_.clear();snapshot_={};
    }
private:
    struct Entry {
        WidgetSpec spec;
        TextSelection selection;
        Point offset;
        std::optional<std::vector<Option>> popup;
        BitmapSurface bitmap;
    };
    std::thread::id owner_;
    EventSink sink_;
    std::size_t bitmap_budget_;
    Snapshot snapshot_;
    std::map<std::string,Entry,std::less<>> entries_;
    std::map<std::string,std::uint64_t,std::less<>> generations_;
    std::optional<WidgetKey> focus_;
    bool closed_=false,dispatching_=false,painting_=false;
    static Availability resolve_availability(const Snapshot& view,
            const std::map<std::string,Entry,std::less<>>& entries,const WidgetKey& key) {
        return availability(view,key,[&](const WidgetKey& group) {
            const auto found=entries.find(group.id);
            return found!=entries.end()&&found->second.spec.key==group?found->second.offset:Point{};
        });
    }
    static void prepare_bitmaps(const Snapshot& view,std::map<std::string,Entry,std::less<>>& entries,
            std::size_t budget) {
        std::map<std::string,DeviceRect,std::less<>> grids;
        std::size_t bytes=0;
        for(const auto& w:view.widgets)if(w.spec.kind==Kind::bitmap) {
            // Fully resolved bounds must still satisfy the device conversion's
            // coordinate limit, even for an image outside the current viewport.
            const auto grid=device_rect(resolve_availability(view,entries,w.spec.key).bounds,view.display_scale);
            const auto row=pixel_row_bytes(grid.width,view.bitmap_format);
            if(grid.height&&row>(budget-bytes)/grid.height)throw std::length_error("Bitmap budget exceeded");
            bytes+=row*grid.height;grids.emplace(w.spec.key.id,grid);
        }
        for(const auto& w:view.widgets)if(w.spec.kind==Kind::bitmap) {
            auto& bitmap=entries.at(w.spec.key.id).bitmap;const auto grid=grids.at(w.spec.key.id);
            bitmap.resize(grid.width,grid.height,view.bitmap_format);
            bitmap.set_source(w.state.bitmap.source_id,w.state.bitmap.revision,w.state.bitmap.source);
        }
    }
    static void prune_unavailable(const Snapshot& view,std::map<std::string,Entry,std::less<>>& entries,
            std::optional<WidgetKey>& focus) {
        if(focus) {
            const auto a=resolve_availability(view,entries,*focus);
            if(!a.visible||!a.enabled)focus.reset();
        }
        for(auto& [id,entry]:entries) {
            (void)id;
            if(entry.popup) {
                const auto a=resolve_availability(view,entries,entry.spec.key);
                if(!a.visible||!a.enabled)entry.popup.reset();
            }
        }
    }
    void require_thread() const {
        if(std::this_thread::get_id()!=owner_)throw std::logic_error("Adapter requires the UI thread");
    }
    void require_open() const {require_thread();if(closed_)throw std::logic_error("Adapter is closed");}
    void require_not_painting() const {
        if(painting_)throw std::logic_error("Adapter mutation during bitmap paint");
    }
    void require_mutable() const {require_open();require_not_painting();}
    Entry& at(const WidgetKey& key) {
        auto found=entries_.find(key.id);
        if(found==entries_.end()||found->second.spec.key!=key)throw std::out_of_range("Unknown widget");
        return found->second;
    }
    const Entry& at(const WidgetKey& key) const {
        const auto found=entries_.find(key.id);
        if(found==entries_.end()||found->second.spec.key!=key)throw std::out_of_range("Unknown widget");
        return found->second;
    }
    void require_kind(const WidgetKey& key,Kind kind) const {
        const auto* w=find_widget(snapshot_,key);
        if(!w||w->spec.kind!=kind)throw std::invalid_argument("Unexpected widget kind");
    }
    static Point scroll_maximum(const Widget& w) {
        const auto height=std::max(w.state.content_size.height,w.spec.kind==Kind::list?
            double(w.state.records.size())*w.spec.row_height:0.0);
        return {std::max(0.0,w.state.content_size.width-w.state.bounds.width),
                std::max(0.0,height-w.state.bounds.height)};
    }
    void reveal_record(const WidgetKey& key,std::size_t index) {
        const auto* w=find_widget(snapshot_,key);auto offset=at(key).offset;
        const auto top=double(index)*w->spec.row_height,bottom=top+w->spec.row_height;
        if(top<offset.y)offset.y=top;
        else if(bottom>offset.y+w->state.bounds.height)offset.y=bottom-w->state.bounds.height;
        scroll(key,offset);
    }
    bool accept_widget(WidgetEvent& event) const {
        const auto* w=find_widget(snapshot_,event.target);const auto a=resolve_availability(snapshot_,entries_,event.target);
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
            else return w->spec.pointer_input&&contains(a.clip,input.position)&&
                std::isfinite(input.wheel_x)&&std::isfinite(input.wheel_y)&&
                (input.kind!=PointerKind::wheel||input.wheel_x!=0||input.wheel_y!=0);
        },event.input);
    }
};
}
