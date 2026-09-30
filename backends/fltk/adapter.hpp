#pragma once
#include "gui/retained_adapter.hpp"
#include "gui/runtime.hpp"
#include "gui/presentation.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <cstdlib>
#include <FL/fl_draw.H>
#include <cmath>
#include <functional>
#include <map>
#include <memory>

namespace gui::fltk {
// Native toolkit types stay in this implementation directory. All decisions
// below concern kinds, declarations, and current eligibility, never feature IDs.
class Adapter final : public RetainedAdapter {
    struct Entry;
    class Window final : public Fl_Double_Window {
        Adapter& owner_;
    public:
        explicit Window(Adapter& owner) : Fl_Double_Window(640, 480), owner_(owner) {}
        void resize(int x, int y, int w, int h) override {
            Fl_Double_Window::resize(x,y,w,h);
            if(owner_.started_ && !owner_.closed())
                owner_.dispatch(ResizeEvent{{double(w),double(h)},1});
        }
        int handle(int event) override {
            if(event==FL_KEYDOWN) {
                if(Fl::event_key()==FL_Tab && !Fl::event_ctrl())return owner_.focus_next(Fl::event_shift()!=0)?1:0;
                std::optional<ShortcutKey> key;
                if(Fl::event_key()==FL_Escape)key=ShortcutKey::escape;
                else if(Fl::event_key()==FL_Enter)key=ShortcutKey::enter;
                else if(Fl::event_key()>FL_F && Fl::event_key()<=FL_F+12)
                    key=static_cast<ShortcutKey>(int(ShortcutKey::f1)+Fl::event_key()-FL_F-1);
                bool consumed=false;
                if(key)owner_.safely([&]{consumed=owner_.policy().send(ShortcutEvent{*key,Fl::event_ctrl()!=0,Fl::event_shift()!=0,Fl::event_alt()!=0})==Delivery::delivered;});
                if(consumed||Fl::event_key()==FL_Escape)return 1;
            }
            if(event==FL_PUSH && Fl::event_button()==FL_RIGHT_MOUSE) {
                const auto order=paint_order(owner_.snapshot());
                for(auto it=order.rbegin();it!=order.rend();++it) {
                    const auto a=owner_.resolved_availability(*it);
                    if(a.enabled&&a.visible&&gui::contains(a.clip,{double(Fl::event_x()),double(Fl::event_y())})&&owner_.open_popup(*it))return 1;
                }
            }
            if(event==FL_PUSH||event==FL_DRAG||event==FL_MOUSEWHEEL||event==FL_MOVE) {
                const auto order=paint_order(owner_.snapshot());
                for(auto it=order.rbegin();it!=order.rend();++it) {
                    const auto a=owner_.resolved_availability(*it);const auto* w=find_widget(owner_.snapshot(),*it);
                    if(!a.enabled||!a.visible||!gui::contains(a.clip,{double(Fl::event_x()),double(Fl::event_y())}))continue;
                    if(w->spec.pointer_input) {
                        const auto kind=event==FL_PUSH?(Fl::event_clicks()?PointerKind::double_click:PointerKind::click):event==FL_MOUSEWHEEL?PointerKind::wheel:PointerKind::move;
                        owner_.dispatch(WidgetEvent{*it,PointerInput{kind,{double(Fl::event_x()),double(Fl::event_y())},double(Fl::event_dx()),-double(Fl::event_dy()),Fl::event_ctrl()!=0,Fl::event_shift()!=0,Fl::event_alt()!=0}});
                        return 1;
                    }
                    break;
                }
            }
            if(event==FL_MOUSEWHEEL) {
                bool handled=false;
                owner_.safely([&]{handled=owner_.wheel_at({double(Fl::event_x()),double(Fl::event_y())},double(Fl::event_dx()),double(Fl::event_dy()));});
                if(handled)return 1;
            }
            return Fl_Double_Window::handle(event);
        }
    };
    class Clip final : public Fl_Group {
    public:
        Rect clip;
        Clip() : Fl_Group(0,0,0,0) {end();}
        void draw() override {
            fl_push_clip(int(clip.x),int(clip.y),int(clip.width),int(clip.height));
            Fl_Group::draw();fl_pop_clip();
        }
        int handle(int event) override {
            if((event==FL_PUSH || event==FL_MOUSEWHEEL) &&
               !gui::contains(clip,{double(Fl::event_x()),double(Fl::event_y())}))return 0;
            return Fl_Group::handle(event);
        }
    };
    struct LiteralDrawing {
        char previous=fl_draw_shortcut;
        LiteralDrawing() {fl_draw_shortcut=0;}
        ~LiteralDrawing() {fl_draw_shortcut=previous;}
    };
    class Text final : public Fl_Widget {
    public:
        std::string text; Font font; TextWrap wrap=TextWrap::none;Color foreground;
        Text() : Fl_Widget(0,0,0,0) {}
        void draw() override {
            LiteralDrawing literal;
            fl_font(font.bold?FL_HELVETICA_BOLD:FL_HELVETICA,int(font.size));
            fl_color(foreground.red,foreground.green,foreground.blue);
            fl_draw(text.c_str(),x(),y(),w(),h(),FL_ALIGN_TOP|FL_ALIGN_LEFT|FL_ALIGN_INSIDE|
                (wrap==TextWrap::word?FL_ALIGN_WRAP:0),nullptr,0);
        }
    };
    class Editor final : public Fl_Text_Editor {
        Adapter& owner_; WidgetKey key_;
        Fl_Text_Buffer owned_buffer_;
        bool wrapping_=false;
    public:
        Editor(Adapter& owner,WidgetKey key) : Fl_Text_Editor(0,0,0,0),owner_(owner),key_(std::move(key)) {
            buffer(&owned_buffer_);scrollbar_width(0);
        }
        ~Editor() override {buffer(nullptr);}
        void wrapping(bool enabled) {
            if(enabled==wrapping_)return;
            wrap_mode(enabled?Fl_Text_Display::WRAP_AT_BOUNDS:Fl_Text_Display::WRAP_NONE,0);wrapping_=enabled;
        }
        std::string value() const {
            const std::unique_ptr<char,decltype(&std::free)> text(owned_buffer_.text(),std::free);
            return text?text.get():"";
        }
        void value(const char* value) {owned_buffer_.text(value);}
        TextSelection selection() const {
            int first=0,last=0;const auto caret=std::size_t(std::max(0,insert_position()));
            if(!buffer()->selection_position(&first,&last))return {caret,caret};
            return {std::size_t(caret==std::size_t(first)?last:first),caret};
        }
        void selection(TextSelection selection) {
            insert_position(int(selection.caret));
            if(selection.anchor==selection.caret)buffer()->unselect();
            else buffer()->select(int(std::min(selection.anchor,selection.caret)),int(std::max(selection.anchor,selection.caret)));
            show_insert_position();
        }
        double row_height() const {fl_font(textfont(),textsize());return std::max(1,fl_height());}
        Point offset() const {return {double(std::max(0,mHorizOffset)),double(std::max(0,mTopLineNum-1))*row_height()};}
        void offset(Point value) {display_insert_position_hint=0;scroll(1+int(std::max(0.0,value.y)/row_height()),int(std::max(0.0,value.x)));redraw();}
        void draw() override {
            Fl_Text_Editor::draw();
            const auto* widget=find_widget(owner_.snapshot(),key_);
            if(!widget||buffer()->length()!=0||widget->state.placeholder.empty())return;
            LiteralDrawing literal;
            const auto& font=widget->state.font;fl_font(font.bold?FL_HELVETICA_BOLD:FL_HELVETICA,int(font.size));
            fl_color(Adapter::color(owner_.snapshot().palette.muted));
            fl_push_clip(text_area.x,text_area.y,text_area.w,text_area.h);
            fl_draw(widget->state.placeholder.c_str(),text_area.x,text_area.y,text_area.w,text_area.h,
                FL_ALIGN_LEFT|FL_ALIGN_INSIDE|FL_ALIGN_TOP,nullptr,0);
            fl_pop_clip();
        }
        int handle(int event) override {
            if(event==FL_FOCUS)owner_.RetainedAdapter::focus(key_);
            const auto* widget=find_widget(owner_.snapshot(),key_);if(!widget)return 0;
            const auto policy=widget->spec.text_policy;
            if(event==FL_KEYDOWN && Fl::event_key()==FL_Enter) {
                bool consumed=false;
                owner_.safely([&]{consumed=owner_.policy().enter(key_,Fl::event_ctrl()!=0,Fl::event_shift()!=0);});
                if(consumed||!policy.multiline)return 1;
            }
            const int result=policy.read_only?Fl_Text_Display::handle(event):Fl_Text_Editor::handle(event);
            if((event==FL_KEYDOWN || event==FL_RELEASE || event==FL_PASTE)&&find_widget(owner_.snapshot(),key_))
                owner_.safely([&]{owner_.RetainedAdapter::text_selection(key_,selection());});
            return result;
        }
    };
    class Choice final : public Fl_Choice {
        Adapter& owner_; WidgetKey key_;
    public:
        bool opening=false;
        Choice(Adapter& owner,WidgetKey key) : Fl_Choice(0,0,0,0),owner_(owner),key_(std::move(key)) {}
        void draw() override {
            Fl_Choice::draw();
            LiteralDrawing literal;
            const auto* widget=find_widget(owner_.snapshot(),key_);if(!widget)return;
            const auto& p=owner_.snapshot().palette;
            const auto enabled=owner_.resolved_availability(key_).enabled;
            fl_color(Adapter::color(enabled?p.surface:p.disabled));fl_rectf(x()+3,y()+3,std::max(0,w()-26),std::max(0,h()-6));
            fl_color(Adapter::color(enabled?tone_color(p,widget->state.font.tone):p.muted));fl_font(widget->state.font.bold?FL_HELVETICA_BOLD:FL_HELVETICA,int(widget->state.font.size));
            fl_draw(visible_text(*widget).c_str(),x()+6,y(),std::max(0,w()-32),h(),FL_ALIGN_LEFT|FL_ALIGN_INSIDE,nullptr,0);
        }
        int handle(int event) override {
            if(event==FL_FOCUS)owner_.RetainedAdapter::focus(key_);
            if(event!=FL_PUSH && event!=FL_KEYDOWN)return Fl_Choice::handle(event);
            owner_.safely([&]{owner_.policy().open_popup(key_);});
            opening=true;
            const int result=Fl_Choice::handle(event);
            opening=false;
            owner_.safely([&]{if(!owner_.closed()&&find_widget(owner_.snapshot(),key_))owner_.policy().close_popup(key_);});
            return result;
        }
    };
    class Canvas final : public Fl_Widget {
        Adapter& owner_; WidgetKey key_;
    public:
        Canvas(Adapter& owner,WidgetKey key) : Fl_Widget(0,0,0,0),owner_(owner),key_(std::move(key)) {}
        void draw() override {owner_.safely([&]{owner_.draw_canvas(*this,key_);});}
        int handle(int event) override {
            if(event==FL_FOCUS) {owner_.RetainedAdapter::focus(key_);return 1;}
            if(event==FL_PUSH) {take_focus();owner_.safely([&]{owner_.click_canvas(key_);});return 1;}
            if(event==FL_MOUSEWHEEL) {owner_.safely([&]{owner_.wheel(key_);});return 1;}
            if(event==FL_KEYDOWN) {
                bool accepted=false;
                owner_.safely([&]{
                    const auto* value=find_widget(owner_.snapshot(),key_);
                    if(!value)return;
                    if(value->spec.kind==Kind::list) {
                        switch(Fl::event_key()) {
                        case FL_Up:owner_.policy().list_key(key_,ListKey::up);accepted=true;break;
                        case FL_Down:owner_.policy().list_key(key_,ListKey::down);accepted=true;break;
                        case FL_Enter:owner_.policy().list_key(key_,ListKey::enter);accepted=true;break;
                        case ' ':owner_.policy().list_key(key_,ListKey::space);accepted=true;break;
                        default:break;
                        }
                    } else if(value->spec.kind==Kind::bitmap && Fl::event_key()==FL_Enter) {
                        owner_.open_popup(key_);accepted=true;
                    }
                });
                return accepted?1:0;
            }
            return 0;
        }
    };
    struct Entry {
        Adapter* owner=nullptr;
        WidgetKey key;
        Kind kind=Kind::label;
        Clip* clip=nullptr;
        Fl_Widget* native=nullptr;
        std::vector<Option> options;
    };
    struct PageButton {Adapter* owner;std::string id;Fl_Button* button;};
    struct PopupItem {Adapter* owner;WidgetKey key;std::size_t index;};
    std::unique_ptr<Window> window_;
    std::map<std::string,std::unique_ptr<Entry>,std::less<>> entries_;
    std::vector<std::unique_ptr<PageButton>> pages_;
    std::vector<Page> page_signature_;
    std::unique_ptr<Fl_Double_Window> popup_,service_window_;
    std::vector<std::unique_ptr<PopupItem>> popup_items_;
    Fl_Input* service_input_=nullptr;
    Fl_Box* service_error_=nullptr;
    std::optional<ServiceRequest> service_;
    std::optional<WidgetKey> popup_key_;
    std::function<void(ServiceResult)> service_reply_;
    std::optional<ServiceResult> completed_service_;
    bool started_=false,refreshing_=false;
    unsigned callback_depth_=0;
    struct PendingScroll {WidgetKey key;Point offset;};
    std::map<std::string,PendingScroll,std::less<>> pending_scroll_;
    std::string error_;
    static Fl_Color color(Color value) {return fl_rgb_color(value.red,value.green,value.blue);}
    static constexpr auto literal_label=static_cast<Fl_Labeltype>(FL_FREE_LABELTYPE);
    static void label_draw(const Fl_Label* label,int x,int y,int w,int h,Fl_Align align) {
        const auto shortcut=fl_draw_shortcut;fl_draw_shortcut=0;
        fl_font(label->font,label->size);fl_color(label->color);
        fl_draw(label->value,x,y,w,h,align,nullptr,0);fl_draw_shortcut=shortcut;
    }
    static void label_measure(const Fl_Label* label,int& width,int& height) {
        const auto shortcut=fl_draw_shortcut;fl_draw_shortcut=0;
        fl_font(label->font,label->size);fl_measure(label->value,width,height,0);fl_draw_shortcut=shortcut;
    }
    static Size metrics(const TextMeasureRequest& request) {
        validate_measure_request(request);LiteralDrawing literal;
        fl_font(request.font.bold?FL_HELVETICA_BOLD:FL_HELVETICA,int(request.font.size));
        int width=request.wrap==TextWrap::word?int(request.available_width):0,height=0;
        fl_measure(request.text.c_str(),width,height,0);
        return {std::min(coordinate_limit,double(width)),std::min(coordinate_limit,double(height))};
    }
    template<class Function> void safely(Function operation) {
        struct Guard {unsigned& depth;explicit Guard(unsigned& value):depth(value){++depth;}~Guard(){--depth;}} guard(callback_depth_);
        try {operation();}catch(const std::exception& e){error_=e.what();}
    }
    void dispatch(Event event) {safely([&]{policy().send(std::move(event));});}
    static void callback(Fl_Widget*,void* data) {
        auto& entry=*static_cast<Entry*>(data);entry.owner->safely([&]{entry.owner->changed(entry);});
    }
    void changed(Entry& entry) {
        const auto* ptr=find_widget(snapshot(),entry.key);if(!ptr)return;
        const auto widget=*ptr;const auto key=entry.key;
        RetainedAdapter::focus(key);
        switch(widget.spec.kind) {
        case Kind::button:policy().send(WidgetEvent{key,Activate{}});break;
        case Kind::toggle:policy().send(WidgetEvent{key,SetChecked{static_cast<Fl_Check_Button*>(entry.native)->value()!=0}});break;
        case Kind::text: {
            auto& editor=*static_cast<Editor*>(entry.native);
            const std::string proposed=editor.value();
            const auto accepted=policy().send(WidgetEvent{key,EditText{proposed,widget.state.text}});
            if(accepted==Delivery::ignored) {
                const auto selection=policy().text_selection(key);editor.value(widget.state.text.c_str());editor.selection(selection);
            }
            break;
        }
        case Kind::choice:case Kind::menu: {
            auto& choice=*static_cast<Choice*>(entry.native);const auto index=choice.value();
            if(index<0||std::size_t(index)>=entry.options.size())break;
            if(choice.opening)policy().choose_popup(key,std::size_t(index));
            else policy().send(WidgetEvent{key,ChooseOption{entry.options[std::size_t(index)].id}});
            break;
        }
        default:break;
        }
    }
    void create(const Widget& widget) {
        auto entry=std::make_unique<Entry>();entry->owner=this;entry->key=widget.spec.key;entry->kind=widget.spec.kind;
        window_->begin();entry->clip=new Clip;entry->clip->begin();
        switch(widget.spec.kind) {
        case Kind::group:entry->native=new Fl_Box(0,0,0,0);break;
        case Kind::label:entry->native=new Text;break;
        case Kind::button:entry->native=new Fl_Button(0,0,0,0);break;
        case Kind::toggle:entry->native=new Fl_Check_Button(0,0,0,0);break;
        case Kind::text:entry->native=new Editor(*this,widget.spec.key);break;
        case Kind::choice:case Kind::menu:entry->native=new Choice(*this,widget.spec.key);break;
        case Kind::list:case Kind::bitmap:entry->native=new Canvas(*this,widget.spec.key);break;
        }
        entry->native->callback(callback,entry.get());entry->clip->end();window_->end();
        entries_.emplace(widget.spec.key.id,std::move(entry));
    }
    void draw_canvas(Fl_Widget& native,const WidgetKey& key) {
        LiteralDrawing literal;
        const auto* value=find_widget(snapshot(),key);if(!value)return;
        const auto widget=*value;const auto area=resolved_availability(key);
        fl_color(color(snapshot().palette.surface));fl_rectf(native.x(),native.y(),native.w(),native.h());
        if(widget.spec.kind==Kind::bitmap) {
            policy().repaint(key);
            BitmapImage image(policy().image(key).width(),policy().image(key).height(),PixelFormat::rgb24);
            image.blit(0,0,policy().image(key).block());
            if(image.width()&&image.height()&&native.w()>0&&native.h()>0) {
                Fl_RGB_Image pixels(image.pixels().data(),int(image.width()),int(image.height()),3,int(image.block().stride_bytes));
                const std::unique_ptr<Fl_Image> scaled(pixels.copy(native.w(),native.h()));scaled->draw(native.x(),native.y());
            }
        } else {
            const auto offset=scroll_offset(key);
            if(widget.state.records.empty()) {
                fl_font(FL_HELVETICA,int(widget.state.font.size));fl_color(color(snapshot().palette.muted));
                fl_draw(widget.state.placeholder.c_str(),native.x()+4,native.y()+4,std::max(0,native.w()-8),std::max(0,native.h()-8),FL_ALIGN_TOP|FL_ALIGN_LEFT|FL_ALIGN_INSIDE,nullptr,0);
            }
            for(std::size_t index=0;index<widget.state.records.size();++index) {
                const auto& row=widget.state.records[index];
                const auto y=area.bounds.y+double(index)*widget.spec.row_height-offset.y;
                if(y+widget.spec.row_height<=area.clip.y || y>=area.clip.y+area.clip.height)continue;
                if(widget.state.selected==row.id) {fl_color(color(snapshot().palette.selection));fl_rectf(native.x(),int(y),native.w(),int(widget.spec.row_height));}
                auto cells=row.cells;
                if(cells.empty())cells.push_back({row.accessible_text,{4,0,std::max(0.0,area.bounds.width-8),widget.spec.row_height},widget.state.font});
                for(const auto& cell:cells) {
                    const auto clipped=intersect(area.clip,{area.bounds.x+cell.bounds.x-offset.x,y+cell.bounds.y,cell.bounds.width,cell.bounds.height});
                    fl_push_clip(int(clipped.x),int(clipped.y),int(clipped.width),int(clipped.height));
                    fl_font(cell.font.bold?FL_HELVETICA_BOLD:FL_HELVETICA,int(cell.font.size));fl_color(color(row.enabled&&area.enabled?tone_color(snapshot().palette,cell.font.tone):snapshot().palette.muted));
                    fl_draw(cell.text.c_str(),int(area.bounds.x+cell.bounds.x-offset.x),int(y+cell.bounds.y),int(cell.bounds.width),int(cell.bounds.height),
                        FL_ALIGN_TOP|FL_ALIGN_LEFT|FL_ALIGN_INSIDE|(cell.wrap==TextWrap::word?FL_ALIGN_WRAP:0),nullptr,0);
                    fl_pop_clip();
                }
            }
        }
        fl_color(color(focused()==key?snapshot().palette.accent:snapshot().palette.border));fl_rect(native.x(),native.y(),native.w(),native.h());
    }
    void click_canvas(const WidgetKey& key) {
        const auto* ptr=find_widget(snapshot(),key);if(!ptr)return;
        const auto widget=*ptr;const auto area=resolved_availability(key);
        const Point position{double(Fl::event_x()),double(Fl::event_y())};
        if(!contains(area.clip,position))return;
        if(widget.spec.kind==Kind::list) {
            const auto row=std::size_t(std::max(0.0,(position.y-area.bounds.y+scroll_offset(key).y)/widget.spec.row_height));
            if(row<widget.state.records.size()) {
                const auto id=widget.state.records[row].id;
                policy().send(WidgetEvent{key,Fl::event_clicks()?Input{ActivateRecord{id}}:Input{SelectRecord{id}}});
            }
        } else if(Fl::event_button()==FL_RIGHT_MOUSE)open_popup(key);
        else policy().send(WidgetEvent{key,PointerInput{Fl::event_clicks()?PointerKind::double_click:PointerKind::click,position,0,0,
            Fl::event_ctrl()!=0,Fl::event_shift()!=0,Fl::event_alt()!=0}});
    }
    void wheel(const WidgetKey& key) {
        const auto* widget=find_widget(snapshot(),key);if(!widget)return;
        if(widget->spec.kind==Kind::list) {auto offset=scroll_offset(key);offset.y+=Fl::event_dy()*widget->spec.row_height;scroll(key,offset);}
        else policy().send(WidgetEvent{key,PointerInput{PointerKind::wheel,{double(Fl::event_x()),double(Fl::event_y())},double(Fl::event_dx()),-double(Fl::event_dy())}});
    }
    bool wheel_at(Point position,double dx,double dy) {
        const auto order=paint_order(snapshot());
        for(auto it=order.rbegin();it!=order.rend();++it) {
            const auto available=resolved_availability(*it);
            if(!available.visible||!available.enabled||!contains(available.clip,position))continue;
            auto widget=*find_widget(snapshot(),*it);
            for(;;) {
                if(widget.spec.kind==Kind::group||widget.spec.kind==Kind::list||widget.spec.kind==Kind::text) {
                    const auto before=scroll_offset(widget.spec.key);
                    const double step=widget.spec.kind==Kind::list?widget.spec.row_height:32;
                    auto offset=Point{before.x+dx*step,before.y+dy*step};
                    scroll(widget.spec.key,offset);
                    if(scroll_offset(widget.spec.key)!=before)return true;
                }
                if(widget.spec.parent.empty())return false;
                const auto parent=std::find_if(snapshot().widgets.begin(),snapshot().widgets.end(),[&](const Widget& candidate){return candidate.spec.key.id==widget.spec.parent;});
                if(parent==snapshot().widgets.end())return false;
                widget=*parent;
            }
        }
        return false;
    }
    Point clamp_editor_scroll(const Widget& widget,Point offset) const {
        const auto natural=measure_text({widget.state.text,widget.state.font,std::max(0.0,widget.state.bounds.width-8),snapshot().display_scale,widget.state.wrap});
        const auto max_x=std::max(0.0,std::max(natural.width,widget.state.content_size.width)-std::max(0.0,widget.state.bounds.width-8));
        const auto max_y=std::max(0.0,std::max(natural.height,widget.state.content_size.height)-std::max(0.0,widget.state.bounds.height-6));
        return {std::clamp(offset.x,0.0,max_x),std::clamp(offset.y,0.0,max_y)};
    }
    void finish_service(bool cancel) {
        if(!service_)return;
        ServiceResult result;result.id=service_->id;result.status=cancel?ServiceStatus::cancelled:ServiceStatus::success;
        if(!cancel) {
            result.value=service_input_->value();
            if(const auto error=runtime_detail::input_error(*service_,result.value);!error.empty()) {
                service_error_->copy_label(error.c_str());service_window_->redraw();return;
            }
        }
        completed_service_=std::move(result);service_window_->hide();
    }
public:
    explicit Adapter(EventSink sink={}) : RetainedAdapter(std::move(sink),metrics),window_(std::make_unique<Window>(*this)) {
        Fl::set_labeltype(literal_label,label_draw,label_measure);
        window_->end();window_->resizable(window_.get());
        window_->callback([](Fl_Widget*,void* data){static_cast<Adapter*>(data)->dispatch(CloseEvent{});},this);
    }
    ~Adapter() override {if(window_)window_->hide();}
    void present(Snapshot view) override {
        RetainedAdapter::present(std::move(view));
        for(auto it=pending_scroll_.begin();it!=pending_scroll_.end();) {
            if(!find_widget(snapshot(),it->second.key))it=pending_scroll_.erase(it);else ++it;
        }
    }
    void show() {
        policy().require_interaction();
        if(!started_)window_->size(std::max(1,int(snapshot().client_size.width)),std::max(1,int(snapshot().client_size.height)));
        sync();window_->show();started_=true;
    }
    Fl_Window& window() {return *window_;}
    // Native test probes stay outside the public, toolkit-independent contract.
    Fl_Widget* native_widget(const WidgetKey& key) {
        if(closed())return nullptr;
        const auto it=entries_.find(key.id);return it!=entries_.end()&&it->second->key==key?it->second->native:nullptr;
    }
    const std::string& error() const {return error_;}
    void sync() {
        (void)closed(); // Owner-thread check also applies while a native callback defers synchronization.
        if(refreshing_||callback_depth_)return;
        // A native menu runs a nested event loop. Keep the model live, but never
        // replace widgets or menu storage beneath that toolkit stack frame.
        for(const auto& [id,entry]:entries_) {
            (void)id;
            if(auto* choice=dynamic_cast<Choice*>(entry->native);choice&&choice->opening)return;
        }
        if(closed()) {window_->hide();if(service_window_)service_window_->hide();if(popup_)popup_->hide();return;}
        struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(refreshing_);
        if(popup_key_) {
            const auto available=resolved_availability(*popup_key_);
            if(!available.visible||!available.enabled) {if(popup_)popup_->hide();}
        }
        if(popup_&&!popup_->shown()) {
            if(popup_key_&&find_widget(snapshot(),*popup_key_))RetainedAdapter::close_popup(*popup_key_);
            popup_key_.reset();popup_.reset();popup_items_.clear();
        }
        if(completed_service_) {
            auto result=std::move(*completed_service_);completed_service_.reset();service_.reset();service_window_.reset();
            auto reply=std::move(service_reply_);if(reply)reply(std::move(result));
        }
        window_->copy_label(snapshot().title.c_str());window_->color(color(snapshot().palette.background));
        const auto& palette=snapshot().palette;
        for(auto it=entries_.begin();it!=entries_.end();) {
            if(!find_widget(snapshot(),it->second->key)) {delete it->second->clip;it=entries_.erase(it);}else ++it;
        }
        for(const auto& key:paint_order(snapshot())) {
            const auto& widget=*find_widget(snapshot(),key);
            if(!entries_.contains(widget.spec.key.id))create(widget);
            auto& entry=*entries_.at(widget.spec.key.id);auto& native=*entry.native;
            const auto area=resolved_availability(entry.key);entry.clip->clip=area.clip;
            entry.clip->resize(int(area.bounds.x),int(area.bounds.y),int(area.bounds.width),int(area.bounds.height));
            native.resize(int(area.bounds.x),int(area.bounds.y),int(area.bounds.width),int(area.bounds.height));
            if(area.visible)entry.clip->show();else entry.clip->hide();
            if(area.enabled)native.activate();else native.deactivate();
            native.copy_tooltip(widget.state.help.c_str());native.labelsize(int(widget.state.font.size));
            native.labelfont(widget.state.font.bold?FL_HELVETICA_BOLD:FL_HELVETICA);
            native.labeltype(literal_label);native.labelcolor(color(tone_color(palette,widget.state.font.tone)));
            native.color(color(area.enabled?palette.surface:palette.disabled));native.selection_color(color(palette.selection));
            native.box(FL_THIN_UP_BOX);
            if(widget.spec.kind==Kind::group)native.box(FL_FLAT_BOX);
            if(widget.spec.kind==Kind::button||widget.spec.kind==Kind::toggle)native.copy_label(widget.state.label.c_str());
            else native.copy_label("");
            window_->remove(entry.clip);window_->add(entry.clip);
            switch(widget.spec.kind) {
            case Kind::label: {auto& text=static_cast<Text&>(native);text.text=widget.state.text;text.font=widget.state.font;text.wrap=widget.state.wrap;text.foreground=area.enabled?tone_color(palette,widget.state.font.tone):palette.muted;break;}
            case Kind::toggle:static_cast<Fl_Check_Button&>(native).value(widget.state.checked);break;
            case Kind::text: {
                auto& editor=static_cast<Editor&>(native);
                editor.wrapping(widget.spec.text_policy.multiline&&widget.state.wrap==TextWrap::word);
                editor.when(FL_WHEN_CHANGED);editor.textsize(int(widget.state.font.size));editor.textfont(widget.state.font.bold?FL_HELVETICA_BOLD:FL_HELVETICA);
                editor.textcolor(color(area.enabled?tone_color(palette,widget.state.font.tone):palette.muted));editor.cursor_color(color(palette.accent));
                editor.show_cursor(!widget.spec.text_policy.read_only);
                if(widget.state.text!=editor.value()) {const auto selection=policy().text_selection(entry.key);editor.value(widget.state.text.c_str());editor.selection(selection);}
                break;
            }
            case Kind::choice:case Kind::menu: {
                auto& choice=static_cast<Choice&>(native);
                if(!choice.opening && entry.options!=widget.state.options) {
                    entry.options=widget.state.options;choice.clear();
                    for(const auto& option:entry.options) {
                        // add() parses separators; insert a fixed label first,
                        // then replace it literally with stable owned storage.
                        const int at=choice.add("item");choice.replace(at,option.label.c_str());
                        const_cast<Fl_Menu_Item&>(choice.menu()[at]).labeltype(literal_label);
                        if(!option.enabled)choice.mode(at,FL_MENU_INACTIVE);
                    }
                }
                if(!choice.opening) {
                    int selected=-1;
                    for(std::size_t i=0;i<entry.options.size();++i)if(widget.state.selected==entry.options[i].id)selected=int(i);
                    choice.value(selected);
                }
                break;
            }
            default:break;
            }
            if(const auto pending=pending_scroll_.find(entry.key.id);pending!=pending_scroll_.end()&&pending->second.key==entry.key) {
                if(auto* editor=dynamic_cast<Editor*>(&native))editor->offset(clamp_editor_scroll(widget,pending->second.offset));
                pending_scroll_.erase(pending);
            }
            native.redraw();
        }
        const auto tabs=page_tabs(snapshot());
        if(page_signature_!=snapshot().pages) {
            for(auto& page:pages_)delete page->button;
            pages_.clear();page_signature_=snapshot().pages;
            window_->begin();
            for(const auto& page:snapshot().pages) {
                auto item=std::make_unique<PageButton>();item->owner=this;item->id=page.id;item->button=new Fl_Button(0,0,0,0);
                item->button->labeltype(literal_label);item->button->copy_label(page.label.c_str());
                item->button->callback([](Fl_Widget*,void* data){auto& page=*static_cast<PageButton*>(data);page.owner->dispatch(PageEvent{page.id});},item.get());
                pages_.push_back(std::move(item));
            }
            window_->end();
        }
        for(auto& page:pages_) {
            const auto tab=std::find_if(tabs.begin(),tabs.end(),[&](const PageTab& t){return t.id==page->id;});
            if(tab==tabs.end()) {page->button->hide();continue;}
            page->button->resize(int(tab->bounds.x),int(tab->bounds.y),int(tab->bounds.width),int(tab->bounds.height));
            page->button->show();
            if(tab->enabled)page->button->activate();else page->button->deactivate();
            page->button->color(color(tab->selected?palette.selection:palette.surface));page->button->labelcolor(color(palette.text));
            window_->remove(page->button);window_->add(page->button);
        }
        if(const auto key=focused();key) {if(auto* native=native_widget(*key);native&&Fl::focus()!=native)native->take_focus();}
        else if(Fl::focus()&&window_->contains(Fl::focus()))Fl::focus(nullptr);
        window_->redraw();
    }
    bool focus(std::optional<WidgetKey> key) override {
        if(!RetainedAdapter::focus(key))return false;
        if(key)if(auto* native=native_widget(*key))native->take_focus();
        return true;
    }
    bool focus_next(bool reverse=false) override {
        if(!RetainedAdapter::focus_next(reverse))return false;
        return focus(focused());
    }
    void text_selection(const WidgetKey& key,TextSelection selection) override {
        RetainedAdapter::text_selection(key,selection);
        if(auto* editor=dynamic_cast<Editor*>(native_widget(key)))editor->selection(RetainedAdapter::text_selection(key));
    }
    using RetainedAdapter::text_selection;
    Point scroll_offset(const WidgetKey& key) const override {
        const auto retained=RetainedAdapter::scroll_offset(key); // Enforce thread, closed and generation checks first.
        const auto* widget=find_widget(snapshot(),key);
        if(widget&&widget->spec.kind==Kind::text) {
            if(const auto pending=pending_scroll_.find(key.id);pending!=pending_scroll_.end()&&pending->second.key==key)
                return clamp_editor_scroll(*widget,pending->second.offset);
            const auto entry=entries_.find(key.id);
            if(entry!=entries_.end()&&entry->second->key==key)return static_cast<const Editor*>(entry->second->native)->offset();
        }
        return retained;
    }
    void scroll(const WidgetKey& key,Point offset) override {
        policy().require_interaction();
        const auto* widget=find_widget(snapshot(),key);
        if(!widget||widget->spec.kind!=Kind::text) {RetainedAdapter::scroll(key,offset);return;}
        if(!std::isfinite(offset.x)||!std::isfinite(offset.y))throw std::invalid_argument("Invalid native editor scroll offset");
        pending_scroll_.insert_or_assign(key.id,PendingScroll{key,clamp_editor_scroll(*widget,offset)});
    }
    bool open_popup(const WidgetKey& key) override {
        if(service_)return false;
        if(!RetainedAdapter::open_popup(key))return false;
        const auto* widget=find_widget(snapshot(),key);if(!widget)return false;
        const auto options=widget->spec.kind==Kind::bitmap?widget->state.actions:widget->state.options;
        popup_key_=key;popup_.reset();popup_items_.clear();popup_=std::make_unique<Fl_Double_Window>(260,int(options.size())*30+16,"Actions");
        popup_->begin();
        for(std::size_t i=0;i<options.size();++i) {
            auto item=std::make_unique<PopupItem>(PopupItem{this,key,i});auto* button=new Fl_Button(8,8+int(i)*30,244,26);
            button->labeltype(literal_label);button->copy_label(options[i].label.c_str());if(!options[i].enabled)button->deactivate();
            button->callback([](Fl_Widget*,void* data){auto item=*static_cast<PopupItem*>(data);item.owner->popup_->hide();item.owner->safely([&]{item.owner->policy().choose_popup(item.key,item.index);});},item.get());
            popup_items_.push_back(std::move(item));
        }
        popup_->end();popup_->set_modal();popup_->show();return true;
    }
    void close_popup(const WidgetKey& key) override {RetainedAdapter::close_popup(key);if(popup_)popup_->hide();}
    void service(ServiceRequest request,std::function<void(ServiceResult)> reply) {
        policy().require_interaction();
        if(service_)throw std::logic_error("A native service is already active");
        if(!reply)throw std::invalid_argument("Missing service completion");
        if(request.kind==ServiceKind::clipboard_write) {
            if(!valid_utf8(request.value)||request.value.size()>std::size_t(std::numeric_limits<int>::max()))throw std::invalid_argument("Invalid native clipboard text");
            Fl::copy(request.value.data(),int(request.value.size()),1);reply({request.id,ServiceStatus::success,{},{}});return;
        }
        if(request.kind!=ServiceKind::prompt) {reply({request.id,ServiceStatus::error,{},"This native example supports prompt and clipboard services; file/location services are unavailable."});return;}
        if(!runtime_detail::input_error(request,request.value).empty()||!valid_utf8(request.title))throw std::invalid_argument("Invalid native prompt request");
        if(popup_key_)close_popup(*popup_key_);
        service_=std::move(request);service_reply_=std::move(reply);
        service_window_=std::make_unique<Fl_Double_Window>(420,140);service_window_->copy_label(service_->title.c_str());service_window_->set_modal();
        service_window_->begin();service_input_=new Fl_Input(12,16,396,30);service_input_->value(service_->value.c_str());
        service_error_=new Fl_Box(12,48,396,34);service_error_->labelcolor(FL_DARK_RED);
        auto* ok=new Fl_Return_Button(212,96,92,28,"OK");auto* cancel=new Fl_Button(316,96,92,28,"Cancel");
        ok->callback([](Fl_Widget*,void* data){static_cast<Adapter*>(data)->finish_service(false);},this);
        cancel->callback([](Fl_Widget*,void* data){static_cast<Adapter*>(data)->finish_service(true);},this);
        service_window_->callback([](Fl_Widget*,void* data){static_cast<Adapter*>(data)->finish_service(true);},this);
        service_window_->end();service_window_->show();service_input_->take_focus();
    }
    bool service_active() const {(void)closed();return service_.has_value();}
    void close() override {
        RetainedAdapter::close();pending_scroll_.clear();service_reply_={};completed_service_.reset();service_.reset();
        window_->hide();if(service_window_)service_window_->hide();if(popup_)popup_->hide();
    }
};
}
