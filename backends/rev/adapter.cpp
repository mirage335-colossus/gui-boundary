#include "adapter.hpp"
#include "probe.hpp"
#include "clipboard.hpp"
#include "gui/presentation.hpp"
#include "rev_embedded.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <chrono>
#include <map>
#include <limits>
#include <stdexcept>
#include <utility>
#include <glew/glew.h>

import Rev.Window;
import Rev.NativeWindow;
import Rev.Element;
import Rev.Element.Event;
import Rev.Element.Box;
import Rev.Element.Text;
import Rev.Element.Button;
import Rev.Element.Checkbox;
import Rev.Element.Dropdown;
import Rev.Element.ControlTheme;
import Rev.Appearance;
import Rev.Core.Color;
import Rev.Core.Pos;
import Rev.Core.Rect;
import Rev.Core.Observable;
import Rev.Core.Resource;
import Rev.Primitive.Video;
import Rev.Primitive.Text;
import Rev.Primitive.Lines;
import Rev.Graphics.Texture;
import Rev.Graphics.Canvas;
import Rev.Graphics.FrameBuffer;

namespace gui::rev {
namespace re = Rev::Element;
using namespace Rev::Appearance;
namespace {
auto color(Color value) { return rgba(value.red,value.green,value.blue,1); }
Rev::Core::Color primitive_color(Color value) {
    return {value.red/255.0f,value.green/255.0f,value.blue/255.0f,1};
}
template<class Edges> void spacing(Edges& edges,float amount) {
    edges.left=Px(amount);edges.right=Px(amount);edges.top=Px(amount);edges.bottom=Px(amount);
}
void place(re::Element* element, Rect box) {
    element->style->layout.position=Position::Absolute;
    element->style->position.left=Px(float(box.x));element->style->position.top=Px(float(box.y));
    element->style->size={.width=Px(float(box.width)),.height=Px(float(box.height)),
        .min={Px(float(box.width)),Px(float(box.height))},.max={Px(float(box.width)),Px(float(box.height))}};
    spacing(element->style->margin,0);
    element->dirty.style=true;
}
void text_style(re::Text* text,const Font& font,Color foreground,TextWrap wrap=TextWrap::none) {
    text->style->text.font=Rev::Core::Resource{
        font.bold?gui_rev_resources::font_bold:gui_rev_resources::font_regular,
        font.bold?gui_rev_resources::font_bold_size:gui_rev_resources::font_regular_size};
    text->style->text.size=Px(float(font.size));text->style->text.weight=font.bold?700:400;
    text->style->text.color=color(foreground);
    text->style->text.wrap=wrap==TextWrap::word?Wrap::BreakWord:Wrap::False;
    text->dirty.style=true;
}
void flat(re::Element* element,const Palette& palette,Color background) {
    spacing(element->style->padding,0);
    spacing(element->style->margin,0);
    element->style->background.color=color(background);
    element->style->border.color=color(palette.border);element->style->border.radius=0_px;element->style->border.width=1_px;
    element->style->shadow.color=rgba(0,0,0,0);element->style->transition=0;
    element->dirty.style=true;
}
std::size_t next_boundary(const std::string& value,std::size_t at) {
    if(at<value.size())++at;
    while(at<value.size()&&(static_cast<unsigned char>(value[at])&0xc0)==0x80)++at;
    return at;
}
struct Depth {
    unsigned& value;
    explicit Depth(unsigned& depth):value(depth){++value;}
    ~Depth(){--value;}
};
}

struct Adapter::Impl {
    struct Window;
    struct Editor;
    struct Bitmap;
    struct Entry {
        WidgetKey key;
        std::unique_ptr<re::Box> clip;
        re::Element* control=nullptr;
        re::Text* label=nullptr;
        re::Button* button=nullptr;
        re::Checkbox* toggle=nullptr;
        re::Dropdown* choice=nullptr;
        Editor* editor=nullptr;
        Bitmap* bitmap=nullptr;
        std::map<std::string,re::Button*,std::less<>> rows;
        std::map<std::string,std::vector<re::Text*>,std::less<>> cells;
        std::shared_ptr<bool> alive=std::make_shared<bool>(true);
        std::function<void()> activate;
        Rect bounds,clipping;
        ~Entry(){*alive=false;}
    };
    struct Popup {
        WidgetKey key;
        std::shared_ptr<bool> token=std::make_shared<bool>(true);
        std::vector<Option> options;
        std::unique_ptr<re::Box> root;
        std::vector<re::Button*> rows;
        std::size_t highlight=0;
    };
    struct Prompt {
        ServiceRequest request;
        std::function<void(ServiceResult)> complete;
        std::unique_ptr<re::Box> root;
        re::Box* frame=nullptr;
        re::Text* title=nullptr;
        Editor* editor=nullptr;
        re::Button* accept=nullptr;
        re::Button* cancel=nullptr;
        std::optional<ServiceResult> result;
    };
    Adapter& owner;
    std::vector<void*> windows;
    std::unique_ptr<Window> window;
    std::unique_ptr<re::Box> background;
    std::map<std::string,std::unique_ptr<Entry>,std::less<>> entries;
    std::map<std::string,re::Button*,std::less<>> tabs;
    std::unique_ptr<re::Text> measure;
    std::unique_ptr<re::Text> tooltip;
    std::optional<WidgetKey> hover;
    Point hover_point;
    std::chrono::steady_clock::time_point hover_started;
    bool tooltip_visible=false;
    std::unique_ptr<Popup> popup;
    std::unique_ptr<Prompt> prompt;
    std::vector<std::unique_ptr<re::Box>> retired;
    std::map<std::string,std::pair<WidgetKey,Point>,std::less<>> editor_offsets;
    Clipboard clipboard;
    bool hold_clipboard=false;
    std::function<void(ClipboardResult)> clipboard_reply;
    unsigned depth=0;
    unsigned measuring_depth=0;
    bool dirty=true,shown=false,closing=false;
    std::string failure;
    std::shared_ptr<bool> alive=std::make_shared<bool>(true);
    explicit Impl(Adapter& value);
    ~Impl();
    template<class F> void safely(F&& callback) {
        try {Depth guard(depth);callback();}
        catch(const std::exception& error){failure=error.what();owner.RetainedAdapter::close();closing=true;}
        catch(...){failure="Unknown native callback failure";owner.RetainedAdapter::close();closing=true;}
    }
    Entry* entry(const WidgetKey& key) const {
        const auto found=entries.find(key.id);
        return found!=entries.end()&&found->second->key==key?found->second.get():nullptr;
    }
    bool eligible(const WidgetKey& key) const {
        if(owner.closed()||prompt||!find_widget(owner.snapshot(),key))return false;
        const auto area=owner.resolved_availability(key);return area.enabled&&area.visible;
    }
    template<class F> auto callback(Entry& entry,F function) {
        std::weak_ptr<bool> live=entry.alive,adapter_live=alive;
        const auto key=entry.key;
        return [this,live,adapter_live,key,function=std::move(function)]() mutable {
            const auto adapter=adapter_live.lock(),control=live.lock();
            if(!adapter||!*adapter||!control||!*control||!eligible(key))return;
            safely([&]{function(key);dirty=true;});
        };
    }
    void send(Event event) {if(!owner.closed()){owner.policy().send(std::move(event));dirty=true;}}
    std::unique_ptr<Entry> create(const Widget& widget);
    void apply(Entry& entry,const Widget& widget,int order);
    void apply_rows(Entry& entry,const Widget& widget);
    void sync();
    void focus_native(re::Element* target);
    void focus_shared();
    void capture_editor(Editor& editor);
    void editor_clipboard(Editor& editor,const std::string& command);
    Size measure_text(const TextMeasureRequest& request);
    std::optional<WidgetKey> hit(Point point) const;
    bool raw_pointer(re::Event& event,PointerKind kind);
    bool wheel(re::Event& event);
    void key(re::Event& event);
    void open_popup(const WidgetKey& key);
    void close_popup();
    void select_popup(std::size_t index);
    void layout_popup();
    void layout_prompt();
    void finish_prompt(ServiceStatus status);
    void update_hover(Point point);
    void update_tooltip();
};

// The native text element retains Rev's glyph layout, selection geometry and
// pointer navigation. Text acceptance and replacement stay in the shared policy.
struct Adapter::Impl::Editor final : re::Text {
    Impl& impl;
    std::optional<WidgetKey> key;
    TextPolicy policy;
    std::function<void(std::string,TextSelection)> changed;
    std::function<void()> submit;
    Color selection_color{},caret_color{};
    bool reveal=false;
    int previous_cursor=-1;
    Editor(Impl& owner,re::Element* parent,std::optional<WidgetKey> target)
        :re::Text(parent,""),impl(owner),key(std::move(target)) {
        editable=true;selectable=true;tabStop=true;
        style->overflow=Overflow::Hide;style->scroll=Scroll::Both;
    }
    TextSelection selection() const {
        return TextSelection{std::size_t(std::max(0,selectAnchor)),std::size_t(std::max(0,cursor))}.clamped(content.get());
    }
    void selection(TextSelection value) {
        value=value.clamped(content.get());cursor=int(value.caret);selectEnd=cursor;selectAnchor=int(value.anchor);
        resetVerticalCursor();dirty.draw=true;
    }
    bool allowed() const {return !impl.owner.closed()&&!targetFlags.disabled&&(!key||impl.eligible(*key));}
    void replace(std::string value) {
        if(!allowed())return;
        if(key) {
            impl.owner.RetainedAdapter::text_selection(*key,selection());
            impl.owner.policy().replace(*key,std::move(value));
            if(!impl.owner.closed())if(const auto* widget=find_widget(impl.owner.snapshot(),*key)) {
                content=widget->state.text;selection(impl.owner.policy().text_selection(*key));
            }
        } else {
            auto replacement=replace_text(content.get(),selection(),value,policy);
            if(replacement.error.empty()) {
                content=replacement.text;selection(replacement.selection);
                if(changed)changed(replacement.text,replacement.selection);
            }
        }
        reveal=true;impl.dirty=true;refresh(*shared->event);
    }
    void textInput(re::Event& event) override {
        if(!targetFlags.focus||!allowed())return;
        const auto& input=event.keyboard.input;
        if((event.keyboard.ctrl&&!event.keyboard.alt)||(input.size()==1&&
            (static_cast<unsigned char>(input.front())<32||input.front()==127))) {event.propagate=false;return;}
        replace(input);event.propagate=false;
    }
    void keyDown(re::Event& event) override {
        if(!targetFlags.focus||!allowed())return;
        auto& keyboard=event.keyboard;
        if(keyboard.enter) {
            if(submit)submit();
            else if(key&&impl.owner.policy().enter(*key,keyboard.ctrl,keyboard.shift)){}
            else if(policy.multiline)replace("\n");
        } else if(keyboard.backspace||keyboard.del) {
            auto selected=selection();
            if(selected.anchor==selected.caret) {
                selected.anchor=keyboard.backspace?text_boundary(content.get(),selected.caret?selected.caret-1:0):next_boundary(content.get(),selected.caret);
                selection(selected);
            }
            replace("");
        } else if(keyboard.ctrl&&(keyboard.key=="v"||keyboard.key=="x"||keyboard.key=="c")) {
            impl.editor_clipboard(*this,keyboard.key);
        } else if(keyboard.arrows.left||keyboard.arrows.right) {
            auto selected=selection();const bool left=keyboard.arrows.left;
            selected.caret=left?text_boundary(content.get(),selected.caret?selected.caret-1:0):next_boundary(content.get(),selected.caret);
            if(!keyboard.shift)selected.anchor=selected.caret;selection(selected);
        } else {
            const bool previous=editable;editable=true;re::Text::keyDown(event);editable=previous;
            selection(selection());
        }
        if(key&&!impl.owner.closed()&&find_widget(impl.owner.snapshot(),*key))impl.owner.RetainedAdapter::text_selection(*key,selection());
        reveal=true;refresh(event);event.propagate=false;
    }
    void mouseUp(re::Event& event) override {
        re::Text::mouseUp(event);
        if(key&&allowed())impl.owner.RetainedAdapter::text_selection(*key,selection());
    }
    void computePrimitives(re::Event& event) override {
        re::Text::computePrimitives(event);
        const float inner_width=std::max(0.0f,resolved.getInner(Rev::Appearance::Axis::Horizontal));
        const float inner_height=std::max(0.0f,resolved.getInner(Rev::Appearance::Axis::Vertical));
        const float max_x=std::max(0.0f,width+1-inner_width),max_y=std::max(0.0f,height-inner_height);
        resolved.scroll.x=std::clamp(resolved.scroll.x,0.0f,max_x);
        resolved.scroll.y=std::clamp(resolved.scroll.y,0.0f,max_y);
        if(targetFlags.focus&&reveal&&!text->lines.empty()) {
            const auto& row=text->lines[cursorLineIndex()];
            const float caret_x=cursorOffsetOnLine(row),caret_y=row.rect.y-rect.y-resolved.pad.t.val;
            const auto reveal_axis=[](float offset,float at,float extent,float viewport,float maximum) {
                if(at<offset)offset=at;else if(at+extent>offset+viewport)offset=at+extent-viewport;
                return std::clamp(offset,0.0f,maximum);
            };
            resolved.scroll.x=reveal_axis(resolved.scroll.x,caret_x,1,inner_width,max_x);
            resolved.scroll.y=reveal_axis(resolved.scroll.y,caret_y,row.rect.h,inner_height,max_y);
        }
        reveal=false;previous_cursor=cursor;
        layout.rect={rect.x+resolved.pad.l.val-resolved.scroll.x,rect.y+resolved.pad.t.val-resolved.scroll.y,width,height};
        text->xPos-=resolved.scroll.x;text->yPos-=resolved.scroll.y;
        for(auto& row:text->lines){row.rect.x-=resolved.scroll.x;row.rect.y-=resolved.scroll.y;}
        for(auto& strip:line->lines) {
            const bool caret=strip.points.size()>1&&strip.points.front().x==strip.points.back().x;
            strip.color=primitive_color(caret?caret_color:selection_color);
            for(auto& point:strip.points){point.x-=resolved.scroll.x;point.y-=resolved.scroll.y;}
        }
        text->compute();if(!line->lines.empty())line->compute();
    }
};

struct Adapter::Impl::Bitmap final : re::Box {
    struct Video final : Rev::Primitives::Video {using Rev::Primitives::Video::Video;};
    Video* video;
    bool upload_pending=true;
    explicit Bitmap(re::Element* parent):re::Box(parent),video(new Video(shared->canvas)){}
    ~Bitmap() override {delete video;}
    void upload(const BitmapImage& source) {
        if(!source.width()||!source.height()){video->data->opacity=0;return;}
        BitmapImage rgb(source.width(),source.height(),PixelFormat::rgb24);rgb.blit(0,0,source.block());
        const auto& bytes=rgb.pixels();
        // Stage every changed texture. A failed transfer leaves both the prior
        // GPU resource and the pending CPU image available for the next retry.
        while(glGetError()!=GL_NO_ERROR){}
        auto replacement=std::make_unique<Rev::Graphics::Texture>(shared->canvas->context,Rev::Graphics::Texture::Params{
                .data=const_cast<unsigned char*>(bytes.data()),.width=source.width(),.height=source.height(),.channels=3,
                .filter=Rev::Graphics::Texture::Filter::Nearest});
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Native texture upload failed");
        delete video->texture;video->texture=replacement.release();
        video->data->opacity=1;upload_pending=false;
    }
    void computePrimitives(re::Event& event) override {
        re::Box::computePrimitives(event);
        const double scale=shared->canvas->details.scale;
        const auto pixels=device_rect({rect.x,rect.y,rect.w,rect.h},scale);
        video->data->rect={float(pixels.x/scale),float(pixels.y/scale),float(pixels.width/scale),float(pixels.height/scale)};
    }
    void draw(re::Event& event) override {re::Box::draw(event);if(video->texture)video->draw();}
};

struct Adapter::Impl::Window final : Rev::Window {
    Impl& impl;
    explicit Window(Impl& owner):Rev::Window(owner.windows,{.name="Generic GUI",.size={640,480,{1,1},{0,0}}}),impl(owner) {
        style->overflow=Overflow::Hide;spacing(style->padding,0);
    }
    void onClose(bool& reject) override {reject=true;impl.safely([&]{impl.send(CloseEvent{});});}
    void onResize(int width,int height) override {
        Rev::Window::onResize(width,height);
        if(impl.shown&&!impl.owner.closed())impl.safely([&]{impl.send(ResizeEvent{{double(width)/details.scale,double(height)/details.scale},details.scale});});
    }
    void onScale(float scale) override {
        Rev::Window::onScale(scale);
        if(impl.shown&&!impl.owner.closed())impl.safely([&]{impl.send(ResizeEvent{{double(window->size.w)/scale,double(window->size.h)/scale},scale});});
    }
    void keyDown(re::Event& event) override {impl.safely([&]{impl.key(event);});}
    void textInput(re::Event& event) override {
        impl.safely([&]{if(!impl.popup)Rev::Window::textInput(event);});
    }
    void mouseDown(re::Event& event) override {
        impl.safely([&]{
            impl.hover.reset();impl.update_tooltip();
            if(impl.prompt){if(impl.prompt->root)Rev::Window::mouseDown(event);return;}
            if(impl.popup) {
                if(!impl.popup->root)return;
                const auto& rect=impl.popup->root->rect;
                if(!gui::contains({rect.x,rect.y,rect.w,rect.h},{event.mouse.pos.x,event.mouse.pos.y}))impl.close_popup();
                else {Rev::Window::mouseDown(event);return;}
            }
            if(event.mouse.rb)if(const auto target=impl.hit({event.mouse.pos.x,event.mouse.pos.y})) {
                if(impl.owner.open_popup(*target)){event.propagate=false;return;}
            }
            if(impl.raw_pointer(event,event.mouse.lb.isDoubleClick()?PointerKind::double_click:PointerKind::click))return;
            if(const auto target=impl.hit({event.mouse.pos.x,event.mouse.pos.y})) {
                const auto* widget=find_widget(impl.owner.snapshot(),*target);
                impl.owner.RetainedAdapter::focus(*target);impl.dirty=true;
                if(event.mouse.rb||widget->spec.kind==Kind::choice||widget->spec.kind==Kind::menu) {
                    impl.owner.open_popup(*target);event.propagate=false;return;
                }
            }
            Rev::Window::mouseDown(event);
        });
    }
    void mouseUp(re::Event& event) override {impl.safely([&]{Rev::Window::mouseUp(event);});}
    void mouseMove(re::Event& event) override {
        impl.safely([&]{
            if(!impl.raw_pointer(event,PointerKind::move))Rev::Window::mouseMove(event);
            impl.update_hover({event.mouse.pos.x,event.mouse.pos.y});
        });
    }
    void mouseDrag(re::Event& event) override {impl.safely([&]{Rev::Window::mouseDrag(event);});}
    void mouseWheel(re::Event& event) override {
        impl.safely([&]{if(!impl.raw_pointer(event,PointerKind::wheel)&&!impl.wheel(event))Rev::Window::mouseWheel(event);});
    }
};

Adapter::Impl::Impl(Adapter& value):owner(value) {
    window=std::make_unique<Window>(*this);
    background=std::make_unique<re::Box>(window.get());background->style->zIndex=-1;
    measure=std::make_unique<re::Text>(window.get(),"");
    measure->style->visibility=Visibility::Hidden;
    tooltip=std::make_unique<re::Text>(window.get(),"");tooltip->style->visibility=Visibility::Hidden;
}
Adapter::Impl::~Impl() {
    *alive=false;clipboard.cancel();retired.clear();prompt.reset();popup.reset();entries.clear();tooltip.reset();measure.reset();background.reset();window.reset();
}
std::unique_ptr<Adapter::Impl::Entry> Adapter::Impl::create(const Widget& widget) {
    auto result=std::make_unique<Entry>();auto& entry=*result;entry.key=widget.spec.key;
    entry.clip=std::make_unique<re::Box>(window.get());entry.clip->style->overflow=Overflow::Hide;
    spacing(entry.clip->style->padding,0);entry.clip->style->scroll=Scroll::None;
    switch(widget.spec.kind) {
    case Kind::group:entry.control=new re::Box(entry.clip.get());break;
    case Kind::label:entry.control=entry.label=new re::Text(entry.clip.get(),"");break;
    case Kind::button:case Kind::menu:
        entry.control=entry.button=new re::Button(entry.clip.get(),re::Button::Params::Secondary(""));
        entry.activate=callback(entry,[this](const WidgetKey& key){
            const auto* current=find_widget(owner.snapshot(),key);
            if(current->spec.kind==Kind::menu)owner.open_popup(key);else send(WidgetEvent{key,Activate{}});
        });
        entry.button->onClick([call=entry.activate](re::Event&){call();});break;
    case Kind::toggle:
        entry.control=entry.toggle=new re::Checkbox(entry.clip.get(),{.label="",.def=false});
        entry.activate=callback(entry,[this](const WidgetKey& key){
            if(auto* current=this->entry(key))send(WidgetEvent{key,SetChecked{bool(current->toggle->value)}});
        });
        entry.toggle->checkbox->onClick([call=entry.activate](re::Event&){call();});break;
    case Kind::choice:
        entry.control=entry.choice=new re::Dropdown(entry.clip.get(),{.label="",.options={},.placeholder="",.value=""});
        entry.choice->label->style->visibility=Visibility::Hidden;
        entry.choice->optionsContainer->style->visibility=Visibility::Hidden;
        entry.activate=callback(entry,[this](const WidgetKey& key){owner.open_popup(key);});break;
    case Kind::text:
        entry.control=entry.editor=new Editor(*this,entry.clip.get(),entry.key);
        entry.label=new re::Text(entry.clip.get(),"");
        entry.label->style->overflow=Overflow::Hide;entry.label->interceptHits=false;break;
    case Kind::list:
        entry.control=new re::Box(entry.clip.get());entry.label=new re::Text(entry.control,"");break;
    case Kind::bitmap:entry.control=entry.bitmap=new Bitmap(entry.clip.get());break;
    }
    entry.control->tabStop=widget.spec.kind!=Kind::label&&widget.spec.kind!=Kind::group;
    return result;
}
void Adapter::Impl::apply_rows(Entry& entry,const Widget& widget) {
    std::set<std::string> present;
    for(const auto& record:widget.state.records)present.insert(record.id);
    for(auto it=entry.rows.begin();it!=entry.rows.end();) {
        if(!present.contains(it->first)){entry.cells.erase(it->first);delete it->second;it=entry.rows.erase(it);}else ++it;
    }
    const auto offset=owner.policy().scroll_offset(entry.key);
    const auto& palette=owner.snapshot().palette;
    entry.label->content=widget.state.placeholder;
    entry.label->style->visibility=widget.state.records.empty()?Visibility::Visible:Visibility::Hidden;
    place(entry.label,{4,4,std::max(0.0,widget.state.bounds.width-8),std::max(0.0,widget.state.bounds.height-8)});
    text_style(entry.label,widget.state.font,palette.muted);
    for(std::size_t index=0;index<widget.state.records.size();++index) {
        const auto& record=widget.state.records[index];auto*& row=entry.rows[record.id];
        if(!row) {
            row=new re::Button(entry.control,re::Button::Params::Secondary(""));
            auto call=callback(entry,[this,id=record.id](const WidgetKey& key){send(WidgetEvent{key,SelectRecord{id}});});
            std::weak_ptr<bool> live=entry.alive;
            row->onClick([this,live,key=entry.key,id=record.id,call](re::Event& event) mutable {
                const auto token=live.lock();if(!token||!*token||!eligible(key))return;
                call();
                if(event.mouse.lb.isDoubleClick()&&!event.keyboard.space&&!owner.closed())send(WidgetEvent{key,ActivateRecord{id}});
            });
            row->labelText->style->visibility=Visibility::Hidden;row->tabStop=false;
        }
        const double width=std::max(widget.state.bounds.width,widget.state.content_size.width);
        place(row,{-offset.x,double(index)*widget.spec.row_height-offset.y,width,widget.spec.row_height});
        flat(row,palette,widget.state.selected==record.id?palette.selection:palette.surface);
        row->style->border.width=0_px;row->setDisabled(!record.enabled);
        auto& cells=entry.cells[record.id];const auto count=std::max(std::size_t(1),record.cells.size());
        while(cells.size()>count){delete cells.back();cells.pop_back();}
        while(cells.size()<count)cells.push_back(new re::Text(row,""));
        for(std::size_t i=0;i<count;++i) {
            const Cell fallback{record.accessible_text,{4,0,std::max(0.0,width-8),widget.spec.row_height},widget.state.font};
            const auto& cell=record.cells.empty()?fallback:record.cells[i];
            cells[i]->content=cell.text;place(cells[i],cell.bounds);
            text_style(cells[i],cell.font,record.enabled&&owner.resolved_availability(entry.key).enabled?tone_color(palette,cell.font.tone):palette.muted,cell.wrap);
            cells[i]->style->overflow=Overflow::Hide;
        }
    }
    // Rev's hit ordering follows native child order as well as paint order.
    entry.control->children.clear();
    entry.control->children.push_back(entry.label);
    for(const auto& record:widget.state.records)entry.control->children.push_back(entry.rows.at(record.id));
}
void Adapter::Impl::apply(Entry& entry,const Widget& widget,int order) {
    const auto area=owner.resolved_availability(entry.key);const auto& palette=owner.snapshot().palette;
    entry.bounds=area.bounds;entry.clipping=area.clip;
    place(entry.clip.get(),area.clip);entry.clip->style->zIndex=order;
    entry.clip->style->visibility=area.visible?Visibility::Visible:Visibility::Hidden;
    entry.clip->setDisabled(!area.enabled||bool(prompt));
    const Rect local{area.bounds.x-area.clip.x,area.bounds.y-area.clip.y,area.bounds.width,area.bounds.height};
    place(entry.control,local);
    const Color foreground=area.enabled?tone_color(palette,widget.state.font.tone):palette.muted;
    if(entry.button) {
        flat(entry.button,palette,area.enabled?palette.surface:palette.disabled);
        entry.button->labelText->content=widget.state.label;
        text_style(entry.button->labelText,widget.state.font,foreground);
        Rect label{5,2,std::max(0.0,local.width-10),std::max(0.0,local.height-4)};
        if(widget.spec.kind==Kind::button) {
            const auto size=measure_text({widget.state.label,widget.state.font,label.width,owner.snapshot().display_scale,TextWrap::none});
            label.width=std::min(label.width,size.width);label.height=std::min(label.height,size.height);
            label.x=(local.width-label.width)/2;label.y=(local.height-label.height)/2;
        }
        place(entry.button->labelText,label);entry.button->labelText->style->overflow=Overflow::Hide;
    }
    if(entry.toggle) {
        entry.toggle->value=widget.state.checked;entry.toggle->label->content=widget.state.label;
        place(entry.toggle->checkbox,{0,3,std::min(20.0,local.width),std::min(20.0,std::max(0.0,local.height-3))});
        place(entry.toggle->label,{26,2,std::max(0.0,local.width-26),std::max(0.0,local.height-4)});
        text_style(entry.toggle->label,widget.state.font,foreground);
        flat(entry.toggle->checkbox,palette,area.enabled?(widget.state.checked?palette.accent:palette.surface):palette.disabled);
    }
    if(entry.choice) {
        place(entry.choice->dropdown,{0,0,local.width,local.height});
        flat(entry.choice->dropdown,palette,area.enabled?palette.surface:palette.disabled);
        entry.choice->params.placeholder=visible_text(widget);entry.choice->params.value="";
        entry.choice->params.options.clear();entry.choice->dropdownText->content=visible_text(widget);
        text_style(entry.choice->dropdownText,widget.state.font,foreground);
        entry.choice->closeMenu();
    }
    if(widget.spec.kind==Kind::label) {
        entry.label->content=widget.state.text;text_style(entry.label,widget.state.font,foreground,widget.state.wrap);
        entry.label->style->overflow=Overflow::Hide;
    }
    if(entry.editor) {
        auto& edit=*entry.editor;edit.policy=widget.spec.text_policy;edit.editable=!edit.policy.read_only;
        if(edit.content.get()!=widget.state.text)edit.content=widget.state.text;
        edit.selection(owner.policy().text_selection(entry.key));
        const auto offset=owner.scroll_offset(entry.key);edit.resolved.scroll={float(offset.x),float(offset.y)};
        text_style(&edit,widget.state.font,foreground,widget.state.wrap);
        flat(&edit,palette,area.enabled?palette.surface:palette.disabled);
        spacing(edit.style->padding,4);
        edit.caret_color=foreground;edit.selection_color=palette.selection;
        entry.label->content=widget.state.placeholder;entry.label->style->visibility=widget.state.text.empty()?Visibility::Visible:Visibility::Hidden;
        place(entry.label,{local.x+4,local.y+4,std::max(0.0,local.width-8),std::max(0.0,local.height-8)});
        text_style(entry.label,widget.state.font,palette.muted);
    }
    if(widget.spec.kind==Kind::list) {
        flat(entry.control,palette,palette.surface);entry.control->style->overflow=Overflow::Hide;apply_rows(entry,widget);
    }
    if(widget.spec.kind==Kind::group) {
        flat(entry.control,palette,palette.surface);
    }
    if(entry.bitmap&&area.visible) {
        if(owner.policy().repaint(entry.key))entry.bitmap->upload_pending=true;
        if(entry.bitmap->upload_pending)entry.bitmap->upload(owner.policy().image(entry.key));
    }
    if(owner.focused()==entry.key&&area.enabled&&!prompt) {
        auto* target=entry.toggle?static_cast<re::Element*>(entry.toggle->checkbox):entry.choice?static_cast<re::Element*>(entry.choice->dropdown):entry.control;
        target->style->border.color=color(palette.accent);
        if(widget.spec.kind!=Kind::bitmap)target->style->border.width=1_px;
    }
}
void Adapter::Impl::focus_native(re::Element* target) {
    if(!window)return;
    const auto clear=[&](auto&& self,re::Element* element)->void {
        element->targetFlags.focus=false;element->dirty.style=true;
        for(auto* child:element->children)self(self,child);
    };
    clear(clear,window.get());window->shared->focusedText=nullptr;
    if(target) {
        window->shared->focusedText=dynamic_cast<Editor*>(target);
        for(auto* current=target;current&&current!=window.get();current=current->parent) {
            current->targetFlags.focus=true;current->dirty.style=true;
        }
    }
    window->refresh(window->event);
}
void Adapter::Impl::focus_shared() {
    if(prompt) {
        if(prompt->editor&&!prompt->editor->targetFlags.focus&&!prompt->accept->targetFlags.focus&&!prompt->cancel->targetFlags.focus)
            focus_native(prompt->editor);
        return;
    }
    if(popup)return;
    auto key=owner.policy().focused();auto* current=key?entry(*key):nullptr;
    re::Element* target=current?current->control:nullptr;
    if(current&&current->toggle)target=current->toggle->checkbox;
    if(current&&current->choice)target=current->choice->dropdown;
    focus_native(target);
}
void Adapter::Impl::capture_editor(Editor& editor) {
    if(!editor.key||owner.closed())return;
    if(const auto* current=find_widget(owner.snapshot(),*editor.key);current&&current->state.text==editor.content.get()) {
        owner.RetainedAdapter::text_selection(*editor.key,editor.selection());
        editor_offsets[editor.key->id]={*editor.key,{editor.resolved.scroll.x,editor.resolved.scroll.y}};
    }
}
void Adapter::Impl::editor_clipboard(Editor& editor,const std::string& command) {
    if(!editor.allowed())return;
    const auto selected=editor.selection();
    if(command=="c"||command=="x") {
        const auto first=std::min(selected.anchor,selected.caret),last=std::max(selected.anchor,selected.caret);
        if(first==last)return;
        try {
            clipboard.copy(editor.content.get().substr(first,last-first));
            if(command=="x")editor.replace("");
        } catch(const std::exception&) {}
        return;
    }
    if(editor.policy.read_only)return;
    const auto target=editor.key;
    const auto base=editor.content.get();
    const auto prompt_id=prompt?std::optional<std::uint64_t>{prompt->request.id}:std::nullopt;
    std::weak_ptr<bool> live=alive;
    auto reply=[this,live,target,base,selected,prompt_id](ClipboardResult result) {
        const auto token=live.lock();if(!token||!*token||owner.closed()||!result.text||!result.error.empty())return;
        Editor* current=nullptr;
        if(target) {
            if(!eligible(*target)||owner.focused()!=target)return;
            if(auto* found=entry(*target))current=found->editor;
        } else if(prompt_id&&prompt&&prompt->request.id==*prompt_id)current=prompt->editor;
        if(!current||!current->targetFlags.focus||current->content.get()!=base||current->selection()!=selected)return;
        current->replace(std::move(*result.text));dirty=true;
    };
    if(hold_clipboard){clipboard_reply=std::move(reply);return;}
    try{clipboard.paste(std::move(reply));}catch(const std::exception&){}
}
Size Adapter::Impl::measure_text(const TextMeasureRequest& request) {
    validate_measure_request(request);
    if(!window||!measure)return {};
    if(measuring_depth)throw std::logic_error("Recursive native text measurement");
    Depth guard(measuring_depth);
    measure->content=request.text;text_style(measure.get(),request.font,owner.snapshot().palette.text,request.wrap);
    measure->resolveStyle(window->event);
    measure->maxWidth=float(request.available_width);measure->allocatedTextWidth=float(request.available_width);measure->layoutText();
    return {double(measure->width),double(measure->height)};
}
void Adapter::Impl::sync() {
    if(depth)return;
    retired.clear();
    if(owner.closed()) {
        popup.reset();prompt.reset();entries.clear();tabs.clear();tooltip.reset();measure.reset();background.reset();window.reset();return;
    }
    if(prompt&&prompt->result) {
        auto result=std::move(*prompt->result);auto callback=std::move(prompt->complete);prompt.reset();dirty=true;
        if(callback)callback(std::move(result));
        if(owner.closed()){sync();return;}
    }
    if(!dirty)return;
    const auto& view=owner.snapshot();window->setTitle(view.title);
    window->style->background.color=color(view.palette.background);
    place(background.get(),{0,0,view.client_size.width,view.client_size.height});
    background->style->background.color=color(view.palette.background);background->style->border.width=0_px;
    const auto width=std::max(1,int(std::ceil(view.client_size.width*window->details.scale)));
    const auto height=std::max(1,int(std::ceil(view.client_size.height*window->details.scale)));
    if(window->window->size.w!=width||window->window->size.h!=height) {
        window->window->setSize(width,height);window->Rev::Window::onResize(width,height);
    }
    for(auto it=entries.begin();it!=entries.end();) {
        if(!find_widget(view,it->second->key))it=entries.erase(it);else ++it;
    }
    for(const auto& widget:view.widgets)if(!entries.contains(widget.spec.key.id))entries.emplace(widget.spec.key.id,create(widget));
    int order=1;
    for(const auto& key:paint_order(view))apply(*entry(key),*find_widget(view,key),order++);
    const auto pages=page_tabs(view);std::set<std::string> page_ids;
    for(const auto& tab:pages)page_ids.insert(tab.id);
    for(auto it=tabs.begin();it!=tabs.end();) {
        if(!page_ids.contains(it->first)){delete it->second;it=tabs.erase(it);}else ++it;
    }
    for(const auto& tab:pages) {
        auto*& button=tabs[tab.id];
        if(!button) {
            button=new re::Button(window.get(),re::Button::Params::Secondary(tab.label));
            button->onClick([this,id=tab.id](re::Event&){if(!prompt)send(PageEvent{id});});
        }
        button->labelText->content=tab.label;text_style(button->labelText,Font{},view.palette.text);
        place(button,tab.bounds);button->style->zIndex=order;
        flat(button,view.palette,tab.selected?view.palette.selection:view.palette.surface);
        button->setDisabled(!tab.enabled||bool(prompt));
    }
    if(popup) {
        if(!eligible(popup->key))close_popup();else layout_popup();
    }
    if(prompt)layout_prompt();
    update_tooltip();
    focus_shared();window->shared->layoutDirty=true;window->refresh(window->event);dirty=false;
}

std::optional<WidgetKey> Adapter::Impl::hit(Point point) const {
    const auto order=paint_order(owner.snapshot());
    for(auto it=order.rbegin();it!=order.rend();++it) {
        const auto area=owner.resolved_availability(*it);
        if(area.enabled&&area.visible&&contains(area.clip,point))return *it;
    }
    return std::nullopt;
}
bool Adapter::Impl::raw_pointer(re::Event& event,PointerKind kind) {
    if(owner.closed()||prompt||popup)return false;
    const auto target=hit({event.mouse.pos.x,event.mouse.pos.y});
    if(!target||!find_widget(owner.snapshot(),*target)->spec.pointer_input)return false;
    owner.RetainedAdapter::focus(*target);
    send(WidgetEvent{*target,PointerInput{kind,{event.mouse.pos.x,event.mouse.pos.y},
        kind==PointerKind::wheel?-event.mouse.wheel.x/120.0:0,kind==PointerKind::wheel?event.mouse.wheel.y/120.0:0,
        bool(event.keyboard.ctrl),bool(event.keyboard.shift),bool(event.keyboard.alt)}});
    event.propagate=false;return true;
}
bool Adapter::Impl::wheel(re::Event& event) {
    if(owner.closed()||prompt||popup)return false;
    const auto target=hit({event.mouse.pos.x,event.mouse.pos.y});if(!target)return false;
    const auto* widget=find_widget(owner.snapshot(),*target);
    while(widget) {
        if(widget->spec.kind==Kind::group||widget->spec.kind==Kind::list||widget->spec.kind==Kind::text) {
            auto offset=owner.scroll_offset(widget->spec.key);const auto previous=offset;
            const auto dx=event.keyboard.shift&&event.mouse.wheel.x==0?event.mouse.wheel.y:event.mouse.wheel.x;
            const auto dy=event.keyboard.shift&&event.mouse.wheel.x==0?0:event.mouse.wheel.y;
            owner.scroll(widget->spec.key,{offset.x-dx*32/120,offset.y-dy*32/120});
            if(owner.scroll_offset(widget->spec.key)!=previous){event.propagate=false;return true;}
        }
        const auto parent=widget->spec.parent;widget=nullptr;
        for(const auto& candidate:owner.snapshot().widgets)if(candidate.spec.key.id==parent){widget=&candidate;break;}
    }
    return false;
}
void Adapter::Impl::key(re::Event& event) {
    if(owner.closed())return;
    auto& keyboard=event.keyboard;
    if(prompt) {
        if(!prompt->root){event.propagate=false;return;}
        if(keyboard.escape)finish_prompt(ServiceStatus::cancelled);
        else if(keyboard.tab) {
            std::array<re::Element*,3> controls{prompt->editor,prompt->accept,prompt->cancel};
            std::size_t index=0;for(std::size_t i=0;i<controls.size();++i)if(controls[i]->targetFlags.focus)index=i;
            focus_native(controls[(index+(keyboard.shift?2:1))%3]);
        } else if((keyboard.enter||keyboard.space)&&prompt->accept->targetFlags.focus)prompt->accept->click(event);
        else if((keyboard.enter||keyboard.space)&&prompt->cancel->targetFlags.focus)prompt->cancel->click(event);
        else window->Rev::Window::keyDown(event);
        event.propagate=false;return;
    }
    if(popup) {
        if(keyboard.escape)close_popup();
        else if(keyboard.enter||keyboard.space)select_popup(popup->highlight);
        else if(keyboard.arrows.up||keyboard.arrows.down) {
            const auto count=popup->options.size();
            for(std::size_t i=0;i<count;++i) {
                popup->highlight=(popup->highlight+(keyboard.arrows.up?count-1:1))%count;
                if(popup->options[popup->highlight].enabled)break;
            }
            layout_popup();
        }
        event.propagate=false;return;
    }
    if(keyboard.tab) {
        if(keyboard.ctrl) {
            const auto pages=page_tabs(owner.snapshot());
            auto current=std::find_if(pages.begin(),pages.end(),[](const PageTab& page){return page.selected;});
            if(!pages.empty()) {
                std::size_t index=current==pages.end()?0:std::size_t(current-pages.begin());
                for(std::size_t i=0;i<pages.size();++i) {
                    index=(index+(keyboard.shift?pages.size()-1:1))%pages.size();
                    if(pages[index].enabled){send(PageEvent{pages[index].id});break;}
                }
            }
        } else owner.focus_next(keyboard.shift);
        event.propagate=false;return;
    }
    std::optional<ShortcutKey> shortcut;
    if(keyboard.escape)shortcut=ShortcutKey::escape;else if(keyboard.enter)shortcut=ShortcutKey::enter;
    else for(int index=1;index<=12;++index)if(keyboard.key=="f"+std::to_string(index))shortcut=ShortcutKey(int(ShortcutKey::f1)+index-1);
    if(shortcut&&owner.policy().send(ShortcutEvent{*shortcut,bool(keyboard.ctrl),bool(keyboard.shift),bool(keyboard.alt)})==Delivery::delivered) {
        dirty=true;event.propagate=false;return;
    }
    const auto focused=owner.policy().focused();auto* control=focused?entry(*focused):nullptr;
    if(!control||!eligible(control->key))return;
    const auto* widget=find_widget(owner.snapshot(),control->key);
    const bool popup_control=widget->spec.kind==Kind::choice||widget->spec.kind==Kind::menu||widget->spec.kind==Kind::bitmap;
    if((keyboard.alt&&keyboard.arrows.down)||(popup_control&&(keyboard.enter||keyboard.space||keyboard.arrows.down)))owner.open_popup(control->key);
    else if(widget->spec.kind==Kind::list) {
        std::optional<ListKey> input;
        if(keyboard.arrows.up)input=ListKey::up;else if(keyboard.arrows.down)input=ListKey::down;
        else if(keyboard.enter)input=ListKey::enter;else if(keyboard.space)input=ListKey::space;
        if(input){owner.policy().list_key(control->key,*input);dirty=true;}
    } else if(control->toggle&&(keyboard.enter||keyboard.space))control->toggle->checkbox->click(event);
    else if(control->button&&(keyboard.enter||keyboard.space))control->button->click(event);
    else if(control->editor)control->editor->keyDown(event);
    event.propagate=false;
}

void Adapter::Impl::open_popup(const WidgetKey& key) {
    close_popup();
    if(!eligible(key)||!owner.policy().open_popup(key))return;
    const auto* widget=find_widget(owner.snapshot(),key);
    popup=std::make_unique<Popup>();popup->key=key;
    popup->options=widget->spec.kind==Kind::bitmap?widget->state.actions:widget->state.options;
    for(std::size_t i=0;i<popup->options.size();++i)if(popup->options[i].enabled){popup->highlight=i;break;}
    if(widget->state.selected)for(std::size_t i=0;i<popup->options.size();++i)
        if(popup->options[i].enabled&&popup->options[i].id==*widget->state.selected)popup->highlight=i;
    dirty=true;
}
void Adapter::Impl::close_popup() {
    if(!popup)return;
    if(!owner.closed()&&find_widget(owner.snapshot(),popup->key))owner.policy().close_popup(popup->key);
    // Native trees are retired only outside their callback stack. Hide now,
    // retaining the allocation until the next synchronization boundary.
    if(depth&&popup->root) {
        popup->root->style->visibility=Visibility::Hidden;
        popup->root->setDisabled(true);
        // Moving the tree to the window preserves its lifetime through dispatch.
        retired.push_back(std::move(popup->root));
    }
    popup.reset();dirty=true;
}
void Adapter::Impl::select_popup(std::size_t index) {
    if(!popup||index>=popup->options.size()||!popup->options[index].enabled)return;
    const auto key=popup->key;
    const auto token=popup->token;
    owner.policy().choose_popup(key,index);
    // An application callback may open another popup, even for the same key.
    // Completing this invocation must retire only the popup that sent it.
    if(popup&&popup->token==token)close_popup();
    dirty=true;
}
void Adapter::Impl::layout_popup() {
    if(!popup)return;
    const auto& view=owner.snapshot();const auto& palette=view.palette;
    const double height=gui::popup_row_height(view.client_size,popup->options.size(),24);
    const auto bounds=gui::popup_bounds(view.client_size,owner.resolved_availability(popup->key).bounds,popup->options.size(),height);
    if(!popup->root) {
        popup->root=std::make_unique<re::Box>(window.get());
        popup->root->interceptHits=true;
        for(std::size_t i=0;i<popup->options.size();++i) {
            auto* button=new re::Button(popup->root.get(),re::Button::Params::Secondary(popup->options[i].label));
            button->onClick([this,key=popup->key,index=i](re::Event&){if(popup&&popup->key==key)select_popup(index);});
            popup->rows.push_back(button);
        }
    }
    place(popup->root.get(),bounds);flat(popup->root.get(),palette,palette.surface);
    popup->root->style->zIndex=int(view.widgets.size()+view.pages.size()+10);
    for(std::size_t i=0;i<popup->rows.size();++i) {
        auto* row=popup->rows[i];place(row,{0,double(i)*height,bounds.width,height});
        flat(row,palette,popup->highlight==i?palette.selection:palette.surface);
        row->setDisabled(!popup->options[i].enabled);
        text_style(row->labelText,Font{},popup->options[i].enabled?palette.text:palette.muted);
        place(row->labelText,{5,1,std::max(0.0,bounds.width-10),std::max(0.0,height-2)});
    }
    window->shared->layoutDirty=true;window->refresh(window->event);
}
void Adapter::Impl::layout_prompt() {
    if(!prompt)return;
    const auto& view=owner.snapshot();const auto& palette=view.palette;
    if(!prompt->root) {
        prompt->root=std::make_unique<re::Box>(window.get());prompt->root->interceptHits=true;
        prompt->frame=new re::Box(prompt->root.get());prompt->frame->interceptHits=true;
        prompt->title=new re::Text(prompt->frame,prompt->request.title);
        prompt->editor=new Editor(*this,prompt->frame,std::nullopt);
        prompt->editor->policy={false,false,prompt->request.byte_limit,SubmitKey::enter};
        prompt->editor->content=prompt->request.value;prompt->editor->selection({0,prompt->request.value.size()});
        prompt->editor->changed=[this](std::string value,TextSelection){if(prompt)prompt->request.value=std::move(value);};
        prompt->editor->submit=[this]{finish_prompt(ServiceStatus::success);};
        prompt->accept=new re::Button(prompt->frame,re::Button::Params::Secondary("OK"));
        prompt->cancel=new re::Button(prompt->frame,re::Button::Params::Secondary("Cancel"));
        prompt->accept->onClick([this](re::Event&){finish_prompt(ServiceStatus::success);});
        prompt->cancel->onClick([this](re::Event&){finish_prompt(ServiceStatus::cancelled);});
    }
    const auto bounds=gui::prompt_bounds(view.client_size);
    const auto local=[&](Rect area){area.x-=bounds.x;area.y-=bounds.y;return area;};
    place(prompt->root.get(),{0,0,view.client_size.width,view.client_size.height});
    prompt->root->style->zIndex=int(view.widgets.size()+view.pages.size()+20);
    place(prompt->frame,bounds);flat(prompt->frame,palette,palette.background);
    place(prompt->title,{12,10,std::max(0.0,bounds.width-24),28});text_style(prompt->title,{14,true},palette.text,TextWrap::word);
    place(prompt->editor,local(gui::prompt_field_bounds(view.client_size)));
    flat(prompt->editor,palette,palette.surface);spacing(prompt->editor->style->padding,4);
    text_style(prompt->editor,Font{},palette.text);
    prompt->editor->caret_color=palette.text;prompt->editor->selection_color=palette.selection;
    place(prompt->accept,local(gui::prompt_accept_bounds(view.client_size)));
    place(prompt->cancel,local(gui::prompt_cancel_bounds(view.client_size)));
    for(auto* button:{prompt->accept,prompt->cancel}) {
        flat(button,palette,palette.surface);text_style(button->labelText,Font{},palette.text);button->tabStop=true;
    }
}
void Adapter::Impl::finish_prompt(ServiceStatus status) {
    if(!prompt||prompt->result)return;
    prompt->result=ServiceResult{prompt->request.id,status,status==ServiceStatus::success?prompt->request.value:std::string{}, {}};
    dirty=true;
}
void Adapter::Impl::update_hover(Point point) {
    if(owner.closed())return;
    const auto next=prompt||popup?std::nullopt:hit(point);
    if(next!=hover){hover=next;hover_started=std::chrono::steady_clock::now();tooltip_visible=false;}
    hover_point=point;update_tooltip();
}
void Adapter::Impl::update_tooltip() {
    if(owner.closed()||!tooltip)return;
    const auto* widget=hover?find_widget(owner.snapshot(),*hover):nullptr;
    const bool visible=widget&&eligible(*hover)&&!popup&&!widget->state.help.empty()&&
        std::chrono::steady_clock::now()-hover_started>=std::chrono::milliseconds(500);
    if(!visible) {
        if(tooltip->style->visibility!=Visibility::Hidden){tooltip->style->visibility=Visibility::Hidden;window->refresh(window->event);}
        tooltip_visible=false;return;
    }
    if(tooltip_visible&&tooltip->content.get()==widget->state.help)return;
    tooltip_visible=true;tooltip->content=widget->state.help;
    const auto& view=owner.snapshot();const auto width=std::min(320.0,view.client_size.width);
    const auto text=measure_text({widget->state.help,{12},std::max(0.0,width-12),view.display_scale,TextWrap::word});
    const auto height=std::min(text.height+12,view.client_size.height);
    const Rect bounds{std::clamp(hover_point.x,0.0,std::max(0.0,view.client_size.width-width)),
        std::clamp(hover_point.y+20,0.0,std::max(0.0,view.client_size.height-height)),width,height};
    place(tooltip.get(),bounds);flat(tooltip.get(),view.palette,view.palette.surface);spacing(tooltip->style->padding,6);
    text_style(tooltip.get(),{12},view.palette.text,TextWrap::word);tooltip->style->visibility=Visibility::Visible;
    tooltip->style->zIndex=int(view.widgets.size()+view.pages.size()+30);tooltip->style->overflow=Overflow::Hide;
    window->shared->layoutDirty=true;window->refresh(window->event);
}

Adapter::Adapter(EventSink sink)
    :RetainedAdapter(std::move(sink),[this](const TextMeasureRequest& request){return measure_text(request);}),impl_(std::make_unique<Impl>(*this)){}
Adapter::~Adapter()=default;
void Adapter::present(Snapshot snapshot) {
    RetainedAdapter::present(std::move(snapshot));impl_->dirty=true;impl_->tooltip_visible=false;
    for(auto it=impl_->editor_offsets.begin();it!=impl_->editor_offsets.end();) {
        if(!find_widget(this->snapshot(),it->second.first))it=impl_->editor_offsets.erase(it);else ++it;
    }
}
Size Adapter::measure_text(const TextMeasureRequest& request) const {
    if(closed())throw std::logic_error("Cannot measure text on a closed Rev adapter");
    return impl_->measure_text(request);
}
bool Adapter::focus(std::optional<WidgetKey> key) {
    const bool result=RetainedAdapter::focus(std::move(key));impl_->dirty=true;
    if(!impl_->depth&&!closed())impl_->focus_shared();return result;
}
bool Adapter::focus_next(bool reverse) {
    const bool result=RetainedAdapter::focus_next(reverse);impl_->dirty=true;
    if(!impl_->depth&&!closed())impl_->focus_shared();return result;
}
void Adapter::text_selection(const WidgetKey& key,TextSelection selection) {
    RetainedAdapter::text_selection(key,selection);impl_->dirty=true;
    if(auto* entry=impl_->entry(key);entry&&entry->editor) {
        entry->editor->selection(policy().text_selection(key));entry->editor->reveal=true;
    }
}
TextSelection Adapter::text_selection(const WidgetKey& key) const {return RetainedAdapter::text_selection(key);}
void Adapter::scroll(const WidgetKey& key,Point offset) {
    RetainedAdapter::scroll(key,offset);impl_->dirty=true;
    const auto* widget=find_widget(snapshot(),key);
    if(widget&&widget->spec.kind==Kind::text) {
        const auto measured=measure_text({widget->state.text,widget->state.font,std::max(0.0,widget->state.bounds.width-8),snapshot().display_scale,widget->state.wrap});
        const auto maximum=Point{std::max(0.0,measured.width+1-std::max(0.0,widget->state.bounds.width-8)),
            std::max(0.0,measured.height-std::max(0.0,widget->state.bounds.height-8))};
        impl_->editor_offsets[key.id]={key,{std::clamp(offset.x,0.0,maximum.x),std::clamp(offset.y,0.0,maximum.y)}};
    }
    if(auto* entry=impl_->entry(key);entry&&entry->editor) {
        const auto actual=scroll_offset(key);entry->editor->resolved.scroll={float(actual.x),float(actual.y)};entry->editor->reveal=false;
    }
}
Point Adapter::scroll_offset(const WidgetKey& key) const {
    const auto retained=RetainedAdapter::scroll_offset(key);
    const auto found=impl_->editor_offsets.find(key.id);
    return found!=impl_->editor_offsets.end()&&found->second.first==key?found->second.second:retained;
}
bool Adapter::open_popup(const WidgetKey& key) {
    policy().require_interaction();impl_->open_popup(key);return impl_->popup&&impl_->popup->key==key;
}
void Adapter::close_popup(const WidgetKey& key) {
    RetainedAdapter::close_popup(key);if(impl_->popup&&impl_->popup->key==key)impl_->close_popup();
}
void Adapter::invalidate(const WidgetKey& key,PixelRect damage) {RetainedAdapter::invalidate(key,damage);impl_->dirty=true;}
void Adapter::close() {
    RetainedAdapter::close();impl_->closing=true;impl_->dirty=true;
    impl_->clipboard.cancel();impl_->clipboard_reply={};
    // The service owner receives one terminal outcome; native controls survive
    // the current event stack and cannot deliver a second response.
    if(impl_->prompt) {
        auto completion=std::move(impl_->prompt->complete);
        const auto id=impl_->prompt->request.id;
        if(completion)completion({id,ServiceStatus::cancelled,{},{}});
    }
}
void Adapter::show() {
    policy().require_interaction();
    sync();if(closed()||!impl_->window)return;
    impl_->shown=true;impl_->window->show();
    const auto& window=*impl_->window;
    if(snapshot().display_scale!=window.details.scale) {
        impl_->send(ResizeEvent{{double(window.window->size.w)/window.details.scale,double(window.window->size.h)/window.details.scale},window.details.scale});sync();
    }
}
void Adapter::sync() {if(!closed())policy().require_interaction();impl_->sync();}
bool Adapter::pump() {
    sync();if(closed())return false;
    impl_->safely([&]{if(!Rev::NativeWindow::pumpEvents())impl_->send(CloseEvent{});});
    if(!closed())impl_->safely([&]{impl_->clipboard.poll();});
    if(!closed())impl_->update_tooltip();
    sync();
    if(!closed()&&impl_->window) {
        if(impl_->window->dirty.draw)impl_->window->draw(impl_->window->event);
        for(auto& [id,entry]:impl_->entries)if(entry->editor)impl_->capture_editor(*entry->editor);
    }
    return !closed();
}
const std::string& Adapter::error() const {return impl_->failure;}
bool Adapter::service_active() const {(void)closed();return bool(impl_->prompt);}
bool Adapter::service(ServiceRequest request,std::function<void(ServiceResult)> completion) {
    if(closed()||impl_->prompt||!completion)return false;
    policy().require_interaction();
    if(request.kind==ServiceKind::clipboard_write) {
        ServiceResult result{request.id,ServiceStatus::success,{},{}};
        try{impl_->clipboard.copy(request.value);}catch(const std::exception& error){result.status=ServiceStatus::error;result.error=error.what();}
        completion(std::move(result));return true;
    }
    if(request.kind!=ServiceKind::prompt) {
        completion({request.id,ServiceStatus::error,{},"This Rev host does not provide the requested platform service"});return true;
    }
    if(!valid_utf8(request.title)||!runtime_detail::input_error(request,request.value).empty())throw std::invalid_argument("Invalid prompt request");
    impl_->close_popup();impl_->prompt=std::make_unique<Impl::Prompt>();
    impl_->prompt->request=std::move(request);impl_->prompt->complete=std::move(completion);impl_->dirty=true;
    sync();return true;
}
BitmapImage Adapter::capture() {
    sync();if(closed()||!impl_->window)throw std::logic_error("Cannot capture a closed Rev window");
    auto& window=*impl_->window;window.draw(window.event);
    auto& target=*window.shared->canvas->frameBuffer;
    Rev::NativeWindow::requireContext(window.shared->canvas->context,"Native capture");
    const auto width=unsigned(target.params.width),height=unsigned(target.params.height);
    std::vector<unsigned char> pixels(std::size_t(width)*height*3);
    GLint previous=0,alignment=0;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous);glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,target.buffer);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,GLsizei(width),GLsizei(height),GL_RGB,GL_UNSIGNED_BYTE,pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT,alignment);glBindFramebuffer(GL_READ_FRAMEBUFFER,GLuint(previous));
    BitmapImage result(width,height,PixelFormat::rgb24);
    const auto stride=std::size_t(width)*3;
    for(unsigned y=0;y<height;++y)result.blit(0,y,{width,1,stride,PixelFormat::rgb24,
        std::span<const unsigned char>(pixels.data()+std::size_t(height-y-1)*stride,stride)});
    return result;
}

bool Probe::exists(Adapter& adapter,const WidgetKey& key){adapter.sync();return adapter.impl_->entry(key)!=nullptr;}
Rect Probe::bounds(Adapter& adapter,const WidgetKey& key) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);if(!entry)return {};
    // Rev prunes hidden subtrees before layout; only visible native allocations
    // have a rendered rectangle to inspect.
    if(entry->clip->style->visibility==Visibility::Hidden)return entry->bounds;
    adapter.impl_->window->draw(adapter.impl_->window->event);
    const auto& rect=entry->control->rect;return {rect.x,rect.y,rect.w,rect.h};
}
Rect Probe::clip(Adapter& adapter,const WidgetKey& key) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);if(!entry)return {};
    if(entry->clip->style->visibility==Visibility::Hidden)return entry->clipping;
    adapter.impl_->window->draw(adapter.impl_->window->event);
    const auto& rect=entry->clip->rect;return {rect.x,rect.y,rect.w,rect.h};
}
std::string Probe::text(Adapter& adapter,const WidgetKey& key) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);if(!entry)return {};
    if(entry->editor)return entry->editor->content.get();
    if(entry->choice)return entry->choice->dropdownText->content.get();
    if(entry->toggle)return entry->toggle->label->content.get();
    if(entry->button)return entry->button->labelText->content.get();
    return entry->label?entry->label->content.get():std::string{};
}
bool Probe::visible(Adapter& adapter,const WidgetKey& key) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);return entry&&entry->clip->style->visibility!=Visibility::Hidden;
}
bool Probe::enabled(Adapter& adapter,const WidgetKey& key) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);return entry&&!entry->clip->targetFlags.disabled;
}
TextSelection Probe::selection(Adapter& adapter,const WidgetKey& key) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);return entry&&entry->editor?entry->editor->selection():TextSelection{};
}
void Probe::activate(Adapter& adapter,const WidgetKey& key) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);if(!entry||!adapter.impl_->eligible(key))return;
    adapter.impl_->safely([&]{
        auto& event=adapter.impl_->window->event;event.resetBeforeDispatch();
        if(entry->button)entry->button->click(event);
        else if(entry->toggle)entry->toggle->checkbox->click(event);
        else if(entry->activate)entry->activate();
    });
}
void Probe::set_checked(Adapter& adapter,const WidgetKey& key,bool value) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);if(!entry||!entry->toggle||!adapter.impl_->eligible(key))return;
    adapter.impl_->safely([&]{entry->toggle->value=value;if(entry->activate)entry->activate();});
}
void Probe::edit_text(Adapter& adapter,const WidgetKey& key,std::string text,TextSelection selection) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);if(!entry||!entry->editor||!adapter.impl_->eligible(key))return;
    adapter.impl_->safely([&]{
        auto* editor=entry->editor;editor->selection({0,editor->content.get().size()});editor->replace(std::move(text));
        editor->selection(selection);adapter.RetainedAdapter::text_selection(key,editor->selection());
    });
}
void Probe::select_option(Adapter& adapter,const WidgetKey& key,std::string id) {
    adapter.sync();
    if(!adapter.impl_->popup||adapter.impl_->popup->key!=key)adapter.open_popup(key);
    adapter.sync();if(!adapter.impl_->popup)return;
    for(std::size_t i=0;i<adapter.impl_->popup->options.size();++i)if(adapter.impl_->popup->options[i].id==id) {
        auto* button=adapter.impl_->popup->rows[i];
        adapter.impl_->safely([&]{auto& event=adapter.impl_->window->event;event.resetBeforeDispatch();button->click(event);});return;
    }
}
void Probe::select_record(Adapter& adapter,const WidgetKey& key,std::string id,bool activate) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);if(!entry||!entry->rows.contains(id)||!adapter.impl_->eligible(key))return;
    adapter.impl_->safely([&]{
        auto& event=adapter.impl_->window->event;event.resetBeforeDispatch();
        event.mouse.lb.pressTimeDiff=activate?0:1000;event.mouse.lb.pressPosDiff={0,0};
        entry->rows.at(id)->click(event);event.mouse.lb.pressTimeDiff=1000;
    });
}
void Probe::scroll(Adapter& adapter,const WidgetKey& key,Point value){adapter.scroll(key,value);}
void Probe::pointer(Adapter& adapter,Point point,int button,bool double_click) {
    adapter.sync();if(adapter.closed())return;
    auto& window=*adapter.impl_->window;auto& event=window.event;event.resetBeforeDispatch();
    event.mouse.pos={float(point.x),float(point.y)};event.mouse.down=event.mouse.pos;
    event.mouse.lb.id=button==1?1:0;event.mouse.rb.id=button==3?1:0;
    event.mouse.lb.pressTimeDiff=double_click?0:1000;event.mouse.lb.pressPosDiff={0,0};
    window.setTargets(event);window.mouseDown(event);event.resetBeforeDispatch();window.mouseUp(event);
    event.mouse.lb.id=0;event.mouse.rb.id=0;
}
void Probe::wheel(Adapter& adapter,Point position,Point detents) {
    adapter.sync();if(adapter.closed())return;
    auto& window=*adapter.impl_->window;auto& event=window.event;event.resetBeforeDispatch();
    event.mouse.pos={float(position.x),float(position.y)};
    event.mouse.wheel={float(-detents.x*120),float(detents.y*120)};
    window.setTargets(event);window.mouseWheel(event);event.mouse.wheel={0,0};
}
void Probe::key(Adapter& adapter,std::string key,bool control,bool shift,bool alt) {
    adapter.sync();if(adapter.closed())return;
    auto& event=adapter.impl_->window->event;event.keyboard={};event.resetBeforeDispatch();
    std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});
    auto& input=event.keyboard;input.key=key;input.ctrl.id=control?1:0;input.shift.id=shift?1:0;input.alt.id=alt?1:0;
    input.enter.id=key=="enter"?1:0;input.escape.id=key=="escape"?1:0;input.space.id=key=="space"?1:0;
    input.tab.id=key=="tab"?1:0;input.backspace.id=key=="backspace"?1:0;input.del.id=key=="delete"?1:0;
    input.arrows.left.id=key=="left"?1:0;input.arrows.right.id=key=="right"?1:0;
    input.arrows.up.id=key=="up"?1:0;input.arrows.down.id=key=="down"?1:0;
    adapter.impl_->window->keyDown(event);event.keyboard={};
}
void Probe::page(Adapter& adapter,std::string id) {
    adapter.sync();const auto found=adapter.impl_->tabs.find(id);if(found==adapter.impl_->tabs.end())return;
    adapter.impl_->safely([&]{auto& event=adapter.impl_->window->event;event.resetBeforeDispatch();found->second->click(event);});
}
void Probe::complete_prompt(Adapter& adapter,std::string value,bool cancel) {
    adapter.sync();if(!adapter.impl_->prompt)return;
    adapter.impl_->safely([&]{
        auto& prompt=*adapter.impl_->prompt;
        if(!cancel){prompt.editor->selection({0,prompt.editor->content.get().size()});prompt.editor->replace(std::move(value));}
        auto& event=adapter.impl_->window->event;event.resetBeforeDispatch();(cancel?prompt.cancel:prompt.accept)->click(event);
    });
    adapter.sync();
}
std::function<void()> Probe::callback_for(Adapter& adapter,const WidgetKey& key) {
    adapter.sync();auto* entry=adapter.impl_->entry(key);return entry?entry->activate:std::function<void()>{};
}
void Probe::hold_clipboard_read(Adapter& adapter,bool hold) {
    adapter.impl_->hold_clipboard=hold;
    if(!hold)adapter.impl_->clipboard_reply={};
}
void Probe::complete_clipboard_read(Adapter& adapter,std::optional<std::string> text,std::string error) {
    auto reply=std::move(adapter.impl_->clipboard_reply);adapter.impl_->clipboard_reply={};
    if(reply)adapter.impl_->safely([&]{reply({std::move(text),std::move(error)});});
}
void Probe::batch(Adapter& adapter,const std::function<void()>& callback) {
    adapter.impl_->safely(callback);adapter.sync();
}
}
