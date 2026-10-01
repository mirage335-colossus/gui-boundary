#pragma once
#include "retained_adapter.hpp"
#include "runtime.hpp"
#include "presentation.hpp"
#include <functional>
#include <optional>
#include <string_view>

namespace gui {
// Host-independent physical input used by the software-rendered profiles.
// Native widget toolkits may supply semantic events directly instead.
enum class Key { tab,enter,space,escape,left,right,up,down,home,end,backspace,delete_key,page_up,page_down,select_all,menu,next_page,previous_page,quit,f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11,f12 };
namespace interaction_detail {
inline std::size_t next(std::string_view value,std::size_t at) {
    if(at>=value.size())return value.size();
    ++at;while(at<value.size()&&(static_cast<unsigned char>(value[at])&0xc0)==0x80)++at;
    return at;
}
inline std::size_t previous(std::string_view value,std::size_t at) {return at==0?0:text_boundary(value,at-1);}
inline std::size_t line_start(std::string_view value,std::size_t at) {
    const auto found=at==0?std::string_view::npos:value.rfind('\n',at-1);return found==std::string_view::npos?0:found+1;
}
inline std::size_t line_end(std::string_view value,std::size_t at) {
    const auto found=value.find('\n',at);return found==std::string_view::npos?value.size():found;
}
}

// Focus, selection, popup identity, hit testing and prompt modality live here,
// shared by terminal and software pixels. This class never contains application
// IDs, model state, renderer APIs or an alternative application layout.
class InteractiveAdapter : public RetainedAdapter {
public:
    struct Popup {WidgetKey key;std::vector<Option> options;std::size_t index=0;};
    struct VisualLine {
        std::size_t begin=0,end=0; // UTF-8 byte range, excluding a consumed line separator.
        bool operator==(const VisualLine&) const = default;
    };
    using ServiceCompletion=std::function<void(ServiceResult)>;
    explicit InteractiveAdapter(EventSink sink={},TextMeasure measure={},double editor_inset=6)
        :RetainedAdapter(std::move(sink),std::move(measure)),editor_inset_(editor_inset) {
        if(!std::isfinite(editor_inset)||editor_inset<0||editor_inset>coordinate_limit)
            throw std::invalid_argument("Invalid editor inset");
    }
    void present(Snapshot view) override {
        policy().require_interaction();
        std::map<std::string,EditorView,std::less<>> editors;
        for(const auto& widget:view.widgets)if(widget.spec.kind==Kind::text) {
            const auto previous=editors_.find(widget.spec.key.id);
            auto entry=previous!=editors_.end()&&previous->second.key==widget.spec.key?previous->second:EditorView{widget.spec.key,{},true,{},std::nullopt,0};
            const auto* old=find_widget(snapshot(),widget.spec.key);
            if(!old||old->state.text!=widget.state.text||old->state.font!=widget.state.font||
                old->state.bounds!=widget.state.bounds||old->state.wrap!=widget.state.wrap||snapshot().display_scale!=view.display_scale) {
                entry.reveal=true;entry.visual_line.reset();
            }
            editors.emplace(widget.spec.key.id,std::move(entry));
        }
        RetainedAdapter::present(std::move(view));editors_.swap(editors);
        if(popup_) {
            const auto* widget=find_widget(snapshot(),popup_->key);
            const auto area=resolved_availability(popup_->key);
            if(!widget||!area.visible||!area.enabled)popup_.reset();
        }
    }
    bool open_popup(const WidgetKey& key) override {
        if(prompt_||!RetainedAdapter::open_popup(key))return false;
        if(popup_&&popup_->key!=key)RetainedAdapter::close_popup(popup_->key);
        const auto* widget=find_widget(snapshot(),key);
        popup_=Popup{key,widget->spec.kind==Kind::bitmap?widget->state.actions:widget->state.options,0};
        const auto enabled=std::find_if(popup_->options.begin(),popup_->options.end(),[](const Option& option){return option.enabled;});
        if(enabled!=popup_->options.end())popup_->index=static_cast<std::size_t>(enabled-popup_->options.begin());
        if(widget->state.selected)for(std::size_t i=0;i<popup_->options.size();++i)
            if(popup_->options[i].enabled&&popup_->options[i].id==*widget->state.selected)popup_->index=i;
        return true;
    }
    void close_popup(const WidgetKey& key) override {
        RetainedAdapter::close_popup(key);if(popup_&&popup_->key==key)popup_.reset();
    }
    void close() override {
        RetainedAdapter::close();popup_.reset();prompt_.reset();completion_={};editors_.clear();
    }
    bool focus(std::optional<WidgetKey> target) override {
        const auto accepted=RetainedAdapter::focus(target);
        if(accepted&&target)mark_editor(*target);
        return accepted;
    }
    bool focus_next(bool reverse=false) override {
        const auto accepted=RetainedAdapter::focus_next(reverse);
        if(const auto target=focused())mark_editor(*target);
        return accepted;
    }
    TextSelection text_selection(const WidgetKey& target) const override {return RetainedAdapter::text_selection(target);}
    void text_selection(const WidgetKey& target,TextSelection selection) override {
        RetainedAdapter::text_selection(target,selection);mark_editor(target);editors_.at(target.id).visual_line.reset();
    }
    void scroll(const WidgetKey& target,Point offset) override {
        const auto* widget=find_widget(snapshot(),target);
        if(!widget||widget->spec.kind!=Kind::text) {
            RetainedAdapter::scroll(target,offset);
            if(popup_) {const auto area=resolved_availability(popup_->key);if(!area.visible||!area.enabled)popup_.reset();}
            return;
        }
        if(!std::isfinite(offset.x)||!std::isfinite(offset.y))throw std::invalid_argument("Invalid editor scroll offset");
        RetainedAdapter::scroll(target,offset); // Enforce owner, open and paint/reentrancy guards.
        const auto maximum=editor_maximum(*widget);auto& entry=editors_.at(target.id);
        entry.offset={std::clamp(offset.x,0.0,maximum.x),std::clamp(offset.y,0.0,maximum.y)};
        entry.reveal=false;entry.selection=text_selection(target);
    }
    Point scroll_offset(const WidgetKey& target) const override {
        const auto* widget=find_widget(snapshot(),target);
        if(!widget||widget->spec.kind!=Kind::text)return RetainedAdapter::scroll_offset(target);
        auto& entry=editors_.at(target.id);const auto maximum=editor_maximum(*widget);
        entry.offset={std::clamp(entry.offset.x,0.0,maximum.x),std::clamp(entry.offset.y,0.0,maximum.y)};
        const auto selection=text_selection(target);
        if(focused()==target&&(entry.reveal||selection!=entry.selection)) {
            const auto box=text_bounds(*widget);const auto caret=editor_caret_position(*widget,selection.caret);
            const double x=caret.x,y=caret.y,row_height=line_height(widget->state.font);
            if(x<entry.offset.x)entry.offset.x=x;
            else if(x+1>entry.offset.x+box.width)entry.offset.x=x+1-box.width;
            if(y<entry.offset.y)entry.offset.y=y;
            else if(y+row_height>entry.offset.y+box.height)entry.offset.y=y+row_height-box.height;
            entry.offset={std::clamp(entry.offset.x,0.0,maximum.x),std::clamp(entry.offset.y,0.0,maximum.y)};
            entry.reveal=false;entry.selection=selection;
        }
        return entry.offset;
    }
    const std::optional<Popup>& popup() const {(void)closed();return popup_;}
    const std::optional<ServiceRequest>& prompt() const {(void)closed();return prompt_;}
    TextSelection prompt_selection() const {(void)closed();return prompt_selection_;}
    void resize(Size viewport,double scale=1) {if(!closed())policy().send(ResizeEvent{viewport,scale});}

    // Hosts pass decoded UTF-8 insertion separately from physical key presses;
    // a Space key event therefore never inserts a second copy of a text event.
    void text(std::string value) {
        if(closed()||!valid_utf8(value))return;
        policy().require_interaction();
        if(prompt_) {
            auto replacement=replace_text(prompt_->value,prompt_selection_,value,
                {false,false,prompt_->byte_limit,SubmitKey::enter});
            if(replacement.error.empty()) {prompt_->value=std::move(replacement.text);prompt_selection_=replacement.selection;}
            return;
        }
        if(popup_)return;
        const auto target=focused();if(!target)return;
        const auto* widget=find_widget(snapshot(),*target);
        if(widget&&widget->spec.kind==Kind::text)policy().replace(*target,std::move(value));
    }
    void key(Key input,bool control=false,bool shift=false,bool alt=false) {
        if(closed())return;
        policy().require_interaction();
        if(input==Key::quit) {policy().send(CloseEvent{});return;}
        if(prompt_) {prompt_key(input,control,shift);return;}
        if(popup_) {
            if(input==Key::escape||input==Key::tab) {
                const auto target=popup_->key;close_popup(target);
                if(input==Key::tab)focus_next(shift);
            } else if(input==Key::enter||input==Key::space)choose_popup();
            else if(input==Key::up||input==Key::down) {
                const auto size=popup_->options.size();
                for(std::size_t i=0;i<size;++i) {
                    popup_->index=input==Key::up?(popup_->index+size-1)%size:(popup_->index+1)%size;
                    if(popup_->options[popup_->index].enabled)break;
                }
            }
            return;
        }
        if(const auto shortcut=shortcut_key(input);shortcut&&
            policy().send(ShortcutEvent{*shortcut,control,shift,alt})==Delivery::delivered)return;
        if(input==Key::next_page||input==Key::previous_page) {change_page(input==Key::previous_page?-1:1);return;}
        if(input==Key::tab&&control) {change_page(shift?-1:1);return;}
        if(input==Key::tab) {focus_next(shift);return;}
        const auto target=focused();if(!target)return;
        const auto* found=find_widget(snapshot(),*target);if(!found)return;
        // A synchronous sink can replace snapshot storage. Never retain its
        // pointers, references or iterators across a call to the application.
        const auto widget=*found;const auto id=widget.spec.key;
        if(input==Key::menu||(alt&&input==Key::down)) {open_popup(id);return;}
        if(widget.spec.kind==Kind::text) {
            if(input==Key::enter) {
                if(!policy().enter(id,control,shift)&&widget.spec.text_policy.multiline)text("\n");
                return;
            }
            auto selection=text_selection(id);
            if(editor_navigation(widget,input,selection,control,shift))return;
            if(edit_key(input,widget.state.text,selection,control,shift)) {text_selection(id,selection);return;}
            if(input==Key::backspace||input==Key::delete_key) {
                delete_selection(widget.state.text,selection,input);text_selection(id,selection);policy().replace(id,"");
            }
            return;
        }
        if(widget.spec.kind==Kind::list) {
            if(input==Key::up||input==Key::down||input==Key::space||input==Key::enter)
                policy().list_key(id,input==Key::up?ListKey::up:input==Key::down?ListKey::down:input==Key::space?ListKey::space:ListKey::enter);
            else if(input==Key::page_up||input==Key::page_down) {
                auto offset=scroll_offset(id);offset.y+=widget.state.bounds.height*(input==Key::page_up?-1:1);scroll(id,offset);
            }
            return;
        }
        if(input!=Key::enter&&input!=Key::space&&input!=Key::down)return;
        if(input==Key::down&&widget.spec.kind!=Kind::choice&&widget.spec.kind!=Kind::menu&&widget.spec.kind!=Kind::bitmap)return;
        if(widget.spec.kind==Kind::button)policy().send(WidgetEvent{id,Activate{}});
        else if(widget.spec.kind==Kind::toggle)policy().send(WidgetEvent{id,SetChecked{!widget.state.checked}});
        else open_popup(id);
    }
    void pointer(PointerInput input) {
        if(closed()||!std::isfinite(input.position.x)||!std::isfinite(input.position.y)||
            !std::isfinite(input.wheel_x)||!std::isfinite(input.wheel_y))return;
        policy().require_interaction();
        if(prompt_) {
            if(input.kind==PointerKind::click) {
                if(contains(prompt_cancel_bounds(),input.position))finish_prompt(ServiceStatus::cancelled);
                else if(contains(prompt_accept_bounds(),input.position))finish_prompt(ServiceStatus::success);
                else if(contains(prompt_field_bounds(),input.position)) {
                    const auto box=prompt_field_bounds();
                    const auto at=offset_at(prompt_->value,{14},input.position.x-box.x-4,0);
                    prompt_selection_={input.shift?prompt_selection_.anchor:at,at};
                }
            }
            return;
        }
        if(popup_) {
            if(input.kind==PointerKind::click||input.kind==PointerKind::double_click) {
                const auto area=popup_bounds();
                if(contains(area,input.position)) {
                    popup_->index=std::min(popup_->options.size()-1,static_cast<std::size_t>((input.position.y-area.y)/popup_row_height()));choose_popup();
                } else {const auto target=popup_->key;close_popup(target);}
            }
            return;
        }
        if(input.kind==PointerKind::click)for(const auto& [page,area]:page_bounds())
            if(page.enabled&&contains(area,input.position)) {policy().send(PageEvent{page.id});return;}
        const auto order=paint_order(snapshot());
        for(auto it=order.rbegin();it!=order.rend();++it) {
            const auto area=resolved_availability(*it);
            if(!area.visible||!area.enabled||!contains(area.clip,input.position))continue;
            const Widget widget=*find_widget(snapshot(),*it);const auto id=widget.spec.key;
            if(widget.spec.pointer_input) {
                if(input.kind==PointerKind::click||input.kind==PointerKind::double_click)focus(id);
                policy().send(WidgetEvent{id,input});return;
            }
            if(input.kind==PointerKind::wheel) {scroll_hit(widget,input);return;}
            if(input.kind!=PointerKind::click&&input.kind!=PointerKind::double_click)return;
            if(!focusable(widget))continue;
            focus(id);
            if(widget.spec.kind==Kind::button)policy().send(WidgetEvent{id,Activate{}});
            else if(widget.spec.kind==Kind::toggle)policy().send(WidgetEvent{id,SetChecked{!widget.state.checked}});
            else if(widget.spec.kind==Kind::choice||widget.spec.kind==Kind::menu)open_popup(id);
            else if(widget.spec.kind==Kind::list) {
                const auto index=static_cast<std::size_t>((input.position.y-area.bounds.y+scroll_offset(id).y)/widget.spec.row_height);
                if(index<widget.state.records.size()) {
                    const auto row=widget.state.records[index];
                    policy().send(WidgetEvent{id,input.kind==PointerKind::double_click?Input{ActivateRecord{row.id}}:Input{SelectRecord{row.id}}});
                }
            } else if(widget.spec.kind==Kind::text) {
                const auto offset=scroll_offset(id);const auto box=text_bounds(widget);
                const auto lines=editor_lines(widget);
                const auto line=std::min(lines.size()-1,static_cast<std::size_t>(std::max(0.0,std::floor((input.position.y-box.y+offset.y)/line_height(widget.state.font)))));
                const auto at=offset_on_line(widget.state.text,widget.state.font,lines[line],input.position.x-box.x+offset.x);
                select_visual(id,{input.shift?text_selection(id).anchor:at,at},line);
            }
            return;
        }
    }
    void context_menu(Point position) {
        if(closed()||prompt_||popup_)return;
        policy().require_interaction();
        const auto order=paint_order(snapshot());
        for(auto it=order.rbegin();it!=order.rend();++it) {
            const auto area=resolved_availability(*it);
            if(area.visible&&area.enabled&&contains(area.clip,position)&&open_popup(*it)) {focus(*it);return;}
        }
    }
    // Prompt modality belongs to the shared interaction engine. File selection,
    // clipboard and URL opening are explicit host extensions; unsupported calls
    // return a truthful error. Closing revokes the callback without fabrication.
    bool service(ServiceRequest request,ServiceCompletion completion) {
        if(closed()||prompt_||!completion)return false;
        policy().require_interaction();
        if(request.kind!=ServiceKind::prompt) {
            completion(ServiceResult{request.id,ServiceStatus::error,{},"Service requires a host implementation"});return true;
        }
        if(!runtime_detail::input_error(request,request.value).empty()||!valid_utf8(request.title))
            throw std::invalid_argument("Invalid prompt request");
        if(popup_) {const auto target=popup_->key;close_popup(target);}
        prompt_selection_={0,request.value.size()};prompt_=std::move(request);completion_=std::move(completion);return true;
    }
    double line_height(const Font& font={}) const {
        return std::max(1.0,measure_text({"M",font,coordinate_limit,snapshot().display_scale,TextWrap::none}).height);
    }
    // Rendering and input share these boxes. A renderer may reserve horizontal
    // space for its chrome (for example, a terminal's focus-marker cell).
    Rect text_bounds(const Widget& widget) const {
        auto box=resolved_availability(widget.spec.key).bounds;
        const double y=widget.spec.text_policy.multiline?4:std::max(2.0,(box.height-line_height(widget.state.font))/2);
        return {box.x+editor_inset_,box.y+y,std::max(0.0,box.width-2*editor_inset_),std::max(0.0,box.height-y-2)};
    }
    // One mapping serves drawing, hit testing and navigation. Word wrapping
    // consumes one separating ASCII space/tab; hard newlines are also excluded
    // from line ranges. Long words split only at UTF-8 codepoint boundaries.
    std::vector<VisualLine> editor_lines(const Widget& widget) const {
        const auto& text=widget.state.text;const auto width=text_bounds(widget).width;
        const bool wrap=widget.spec.text_policy.multiline&&widget.state.wrap==TextWrap::word;
        std::vector<VisualLine> result;
        std::size_t begin=0;
        for(;;) {
            const auto newline=text.find('\n',begin),end=newline==std::string::npos?text.size():newline;
            if(!wrap||begin==end)result.push_back({begin,end});
            else {
                std::vector<std::size_t> boundaries{begin};
                for(auto at=begin;at<end;) {at=interaction_detail::next(text,at);boundaries.push_back(at);}
                std::size_t first=0;
                while(first+1<boundaries.size()) {
                    const auto fits=[&](std::size_t index) {
                        return measured_width(text,widget.state.font,boundaries[first],boundaries[index])<=width;
                    };
                    auto low=first,high=first+1;std::size_t step=1;
                    while(fits(high)) {
                        low=high;if(high+1==boundaries.size())break;
                        step=std::min(step*2,boundaries.size()-1-low);high=low+step;
                    }
                    if(low!=high)while(low+1<high) {
                        const auto middle=low+(high-low)/2;if(fits(middle))low=middle;else high=middle;
                    }
                    auto last=std::max(first+1,low),next=last;
                    if(last+1<boundaries.size()) {
                        const auto limit=last;
                        for(auto index=first+1;index<=limit;++index) {
                            const auto byte=boundaries[index];
                            if(byte<end&&(text[byte]==' '||text[byte]=='\t')) {last=index;next=index+1;}
                        }
                    }
                    result.push_back({boundaries[first],boundaries[last]});first=next;
                }
            }
            if(newline==std::string::npos)break;
            begin=newline+1;
        }
        return result;
    }
    std::size_t editor_line_index(const Widget& widget,std::size_t caret) const {
        const auto lines=editor_lines(widget);caret=text_boundary(widget.state.text,caret);
        const auto found=editors_.find(widget.spec.key.id);
        if(found!=editors_.end()&&found->second.visual_line&&found->second.visual_caret==caret) {
            const auto line=*found->second.visual_line;
            if(line<lines.size()&&caret>=lines[line].begin&&caret<=lines[line].end)return line;
        }
        std::size_t row=0;
        for(std::size_t i=1;i<lines.size()&&lines[i].begin<=caret;++i)row=i;
        return row;
    }
    Point editor_caret_position(const Widget& widget,std::size_t caret) const {
        const auto lines=editor_lines(widget);const auto row=editor_line_index(widget,caret);
        const auto& line=lines[row];caret=std::clamp(text_boundary(widget.state.text,caret),line.begin,line.end);
        return {measured_width(widget.state.text,widget.state.font,line.begin,caret),double(row)*line_height(widget.state.font)};
    }
    double popup_row_height() const {
        const auto natural=std::max(24.0,line_height()+8);
        return gui::popup_row_height(snapshot().client_size,popup_?popup_->options.size():0,natural);
    }
    Rect popup_bounds() const {
        if(!popup_)return {};
        const auto area=resolved_availability(popup_->key);
        return gui::popup_bounds(snapshot().client_size,area.bounds,popup_->options.size(),popup_row_height());
    }
    std::vector<std::pair<Page,Rect>> page_bounds() const {
        std::vector<std::pair<Page,Rect>> result;
        for(const auto& tab:page_tabs(snapshot()))result.emplace_back(Page{tab.id,tab.label,tab.enabled,true},tab.bounds);
        return result;
    }
    Rect prompt_bounds() const {
        return gui::prompt_bounds(snapshot().client_size);
    }
    Rect prompt_field_bounds() const {
        return gui::prompt_field_bounds(snapshot().client_size);
    }
    Rect prompt_cancel_bounds() const {
        return gui::prompt_cancel_bounds(snapshot().client_size);
    }
    Rect prompt_accept_bounds() const {
        return gui::prompt_accept_bounds(snapshot().client_size);
    }
private:
    double editor_inset_;
    struct EditorView {
        WidgetKey key;Point offset;bool reveal=true;TextSelection selection;
        std::optional<std::size_t> visual_line;std::size_t visual_caret=0;
    };
    mutable std::map<std::string,EditorView,std::less<>> editors_;
    void mark_editor(const WidgetKey& target) {
        if(const auto found=editors_.find(target.id);found!=editors_.end()&&found->second.key==target)found->second.reveal=true;
    }
    Point editor_maximum(const Widget& widget) const {
        const auto lines=editor_lines(widget);double width=0;
        for(const auto& line:lines)width=std::max(width,measured_width(widget.state.text,widget.state.font,line.begin,line.end));
        const Size natural{width,std::min(coordinate_limit,double(lines.size())*line_height(widget.state.font))};
        const auto box=text_bounds(widget);
        const double caret_space=widget.spec.text_policy.multiline&&widget.state.wrap==TextWrap::word?0:1;
        return {std::max(0.0,std::max(widget.state.content_size.width,natural.width+caret_space)-box.width),
            std::max(0.0,std::max(widget.state.content_size.height,natural.height)-box.height)};
    }
    double measured_width(const std::string& text,const Font& font,std::size_t begin,std::size_t end) const {
        return measure_text({text.substr(begin,end-begin),font,coordinate_limit,snapshot().display_scale,TextWrap::none}).width;
    }
    std::size_t offset_on_line(const std::string& value,const Font& font,VisualLine line,double x) const {
        double previous=0;
        for(auto at=line.begin;at<line.end;) {
            const auto next=interaction_detail::next(value,at);
            const auto width=measured_width(value,font,line.begin,next);
            if(x<(previous+width)/2)return at;
            previous=width;at=next;
        }
        return line.end;
    }
    void select_visual(const WidgetKey& key,TextSelection selection,std::size_t line) {
        text_selection(key,selection);auto& entry=editors_.at(key.id);
        entry.visual_line=line;entry.visual_caret=selection.caret;
    }
    bool editor_navigation(const Widget& widget,Key input,TextSelection selection,bool control,bool shift) {
        if((input!=Key::home&&input!=Key::end&&input!=Key::up&&input!=Key::down)||
           (control&&(input==Key::home||input==Key::end)))return false;
        const auto lines=editor_lines(widget);auto row=editor_line_index(widget,selection.caret);
        if(input==Key::home)selection.caret=lines[row].begin;
        else if(input==Key::end)selection.caret=lines[row].end;
        else {
            const auto x=editor_caret_position(widget,selection.caret).x;
            if(input==Key::up&&row>0)--row;
            else if(input==Key::down&&row+1<lines.size())++row;
            selection.caret=offset_on_line(widget.state.text,widget.state.font,lines[row],x);
        }
        if(!shift)selection.anchor=selection.caret;
        select_visual(widget.spec.key,selection,row);return true;
    }
    static std::optional<ShortcutKey> shortcut_key(Key key) {
        if(key==Key::escape)return ShortcutKey::escape;
        if(key==Key::enter)return ShortcutKey::enter;
        if(key>=Key::f1&&key<=Key::f12)return static_cast<ShortcutKey>(static_cast<int>(ShortcutKey::f1)+static_cast<int>(key)-static_cast<int>(Key::f1));
        return {};
    }
    std::optional<Popup> popup_;
    std::optional<ServiceRequest> prompt_;
    TextSelection prompt_selection_;
    ServiceCompletion completion_;
    std::size_t offset_at(const std::string& value,const Font& font,double x,double y) const {
        const auto row=static_cast<std::size_t>(std::max(0.0,std::floor(y/line_height(font))));
        std::size_t begin=0;
        for(std::size_t i=0;i<row;++i) {
            const auto end=value.find('\n',begin);if(end==std::string::npos)return value.size();begin=end+1;
        }
        const auto end=interaction_detail::line_end(value,begin);
        double previous=0;
        for(std::size_t at=begin;at<end;) {
            const auto next=interaction_detail::next(value,at);
            const double width=measure_text({value.substr(begin,next-begin),font,coordinate_limit,snapshot().display_scale,TextWrap::none}).width;
            if(x<(previous+width)/2)return at;
            previous=width;at=next;
        }
        return end;
    }
    void scroll_hit(Widget current,const PointerInput& input) {
        for(;;) {
            if(current.spec.kind==Kind::list||current.spec.kind==Kind::group||current.spec.kind==Kind::text) {
                auto offset=scroll_offset(current.spec.key);offset.x+=input.wheel_x*32;offset.y-=input.wheel_y*32;
                scroll(current.spec.key,offset);return;
            }
            if(current.spec.parent.empty())return;
            const auto parent=std::find_if(snapshot().widgets.begin(),snapshot().widgets.end(),[&](const Widget& widget){return widget.spec.key.id==current.spec.parent;});
            if(parent==snapshot().widgets.end())return;
            current=*parent;
        }
    }
    void change_page(int direction) {
        std::vector<std::string> pages;
        for(const auto& page:snapshot().pages)if(page.enabled&&page.visible)pages.push_back(page.id);
        if(pages.empty())return;
        const auto found=std::find(pages.begin(),pages.end(),snapshot().active_page.value_or(""));
        const auto current=found==pages.end()?std::size_t{0}:static_cast<std::size_t>(found-pages.begin());
        const auto index=direction<0?(current+pages.size()-1)%pages.size():(current+1)%pages.size();policy().send(PageEvent{pages[index]});
    }
    void choose_popup() {
        if(!popup_)return;
        const auto target=popup_->key;const auto index=popup_->index;popup_.reset();policy().choose_popup(target,index);
    }
    static bool edit_key(Key input,const std::string& value,TextSelection& selection,bool control,bool shift) {
        if(input==Key::select_all) {selection={0,value.size()};return true;}
        if((input==Key::left||input==Key::right)&&!shift&&selection.anchor!=selection.caret) {
            selection.caret=input==Key::left?std::min(selection.anchor,selection.caret):std::max(selection.anchor,selection.caret);
        } else if(input==Key::left)selection.caret=interaction_detail::previous(value,selection.caret);
        else if(input==Key::right)selection.caret=interaction_detail::next(value,selection.caret);
        else if(input==Key::home)selection.caret=control?0:interaction_detail::line_start(value,selection.caret);
        else if(input==Key::end)selection.caret=control?value.size():interaction_detail::line_end(value,selection.caret);
        else if(input==Key::up||input==Key::down) {
            const auto start=interaction_detail::line_start(value,selection.caret),end=interaction_detail::line_end(value,selection.caret);
            std::size_t count=0;for(auto at=start;at<selection.caret;at=interaction_detail::next(value,at))++count;
            auto at=input==Key::up?(start==0?0:interaction_detail::line_start(value,start-1)):(end==value.size()?end:end+1);
            const auto limit=interaction_detail::line_end(value,at);
            for(std::size_t i=0;i<count&&at<limit;++i)at=interaction_detail::next(value,at);
            selection.caret=at;
        } else return false;
        if(!shift)selection.anchor=selection.caret;
        return true;
    }
    static void delete_selection(const std::string& value,TextSelection& selection,Key input) {
        if(selection.anchor==selection.caret) {
            if(input==Key::backspace)selection.anchor=interaction_detail::previous(value,selection.caret);
            else selection.caret=interaction_detail::next(value,selection.caret);
        }
    }
    void finish_prompt(ServiceStatus status) {
        if(!prompt_)return;
        ServiceResult result{prompt_->id,status,status==ServiceStatus::success?prompt_->value:std::string{}, {}};
        auto completion=std::move(completion_);prompt_.reset();completion_= {};
        completion(std::move(result));
    }
    void prompt_key(Key input,bool control,bool shift) {
        if(input==Key::enter) {finish_prompt(ServiceStatus::success);return;}
        if(input==Key::escape) {finish_prompt(ServiceStatus::cancelled);return;}
        if(edit_key(input,prompt_->value,prompt_selection_,control,shift))return;
        if(input==Key::backspace||input==Key::delete_key) {delete_selection(prompt_->value,prompt_selection_,input);text("");}
    }
};
}
