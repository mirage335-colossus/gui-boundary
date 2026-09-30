#pragma once
#include "gui/contract.hpp"
#include "gui/layout.hpp"
#include "gui/runtime.hpp"
#include "gui/presentation.hpp"
#include <type_traits>

// This shared application can be compiled with only the public boundary headers.
// The composition root supplies an Adapter; no native implementation is named.
class Example {
public:
    explicit Example(gui::Adapter& adapter):adapter_(adapter) {
        view_.title="Boundary Workshop";
        view_.pages={{"main","Controls"},{"other","Other page"}};view_.active_page="main";
        add("panel",gui::Kind::group,{});
        add("heading",gui::Kind::label,{},"panel").state.text="Shared controls";
        auto& choice=add("choice",gui::Kind::choice,{},"panel");
        choice.state.options={{"first","First","",true},{"second","Second","",true}};
        choice.state.selected="first";
        auto& editor=add("editor",gui::Kind::text,{},"panel");
        editor.spec.text_policy={false,false,256,gui::SubmitKey::enter};editor.state.text="Example text";
        editor.state.options={{"short","Short text","Hello",true},{"empty","Empty text","",true}};
        add("toggle",gui::Kind::toggle,{},"panel").state.label="Enable action";
        add("button",gui::Kind::button,{},"panel").state.label="Add row";
        auto& menu=add("menu",gui::Kind::menu,{},"panel");
        menu.state.label="Actions";
        menu.state.options={{"clear","Clear rows","",true}};
        auto& list=add("list",gui::Kind::list,{},"panel");
        list.spec.follow_tail=true;list.state.placeholder="No rows";
        list.state.content_size.width=360;
        auto& bitmap=add("bitmap",gui::Kind::bitmap,{440,48,160,160});
        bitmap.spec.pointer_input=true;bitmap.state.accessible_name="Color rectangle";
        bitmap.state.actions={{"refresh","Refresh image","",true}};
        bitmap.state.bitmap={"rectangle",0,gui::solid_bitmap(64,128,192)};
        add("caption",gui::Kind::label,{440,216,160,48}).state.text="A bitmap area";
        add("details",gui::Kind::button,{440,280,160,32}).state.label="Show details";
        auto& overlay=add("dialog",gui::Kind::group,{112,96,416,240});overlay.state.visible=false;
        add("dialog-title",gui::Kind::label,{136,112,368,32},"dialog").state.text="A shared modal view";
        auto& explanation=add("dialog-body",gui::Kind::label,{136,156,368,80},"dialog");
        explanation.state.text="This view uses the same declarations in every backend. Escape closes it; background controls cannot receive input.";
        explanation.state.wrap=gui::TextWrap::word;
        add("dismiss",gui::Kind::button,{336,272,168,32},"dialog").state.label="Close details";
        view_.key_bindings.push_back({gui::ShortcutKey::escape,{"dismiss",1}});
        publish();
    }
    const gui::Snapshot& view() const {return view_;}
    std::optional<gui::ServiceRequest> next_service() {return services_.begin_next();}
    bool complete_service(gui::ServiceResult result) {
        if(!services_.complete(result))return false;
        if(result.status!=gui::ServiceStatus::cancelled) {
            get("caption").state.text=result.status==gui::ServiceStatus::success?result.value:result.error;publish();
        }
        return true;
    }
    void select_editor() {
        if(adapter_.focus(gui::WidgetKey{"editor",1}))
            adapter_.text_selection({"editor",1},{0,get("editor").state.text.size()});
    }
    bool show_bitmap_actions() {return adapter_.open_popup(get("bitmap").spec.key);}
    bool presentation_pending() const {return presentation_pending_;}
    void retry_presentation() {if(presentation_pending_&&!adapter_.closed())publish();}
    void set_caption(std::string text) {
        if(!gui::valid_utf8(text))throw std::invalid_argument("Invalid caption text");
        if(adapter_.closed())return;
        get("caption").state.text=std::move(text);publish();
    }
    void handle(gui::Event event) {
        if(adapter_.closed())return;
        // Closing must make progress even when measurement or painting fails.
        if(std::holds_alternative<gui::CloseEvent>(event)) {
            services_.shutdown();presentation_pending_=false;adapter_.close();return;
        }
        if(!std::holds_alternative<gui::ResizeEvent>(event))retry_presentation();
        // Reuse the same input policy against authoritative state. This also
        // protects delivery through a queued facade after the view changes.
        if(!gui::normalize_event(view_,event,[&](const gui::WidgetKey& key) {
            return adapter_.scroll_offset(key);
        }))return;
        if(const auto* e=std::get_if<gui::WidgetEvent>(&event)) {
            const auto previous_modal=view_.modal_root;
            const auto available=adapter_.resolved_availability(e->target);
            auto& w=get(e->target.id);
            std::visit([&](const auto& input) {
                using T=std::decay_t<decltype(input)>;
                if constexpr(std::is_same_v<T,gui::SetChecked>)w.state.checked=input.value;
                else if constexpr(std::is_same_v<T,gui::EditText>)w.state.text=input.value;
                else if constexpr(std::is_same_v<T,gui::ChooseOption>) {
                    for(const auto& option:w.state.options)if(option.id==input.id) {
                        if(w.spec.kind==gui::Kind::text)w.state.text=option.value;
                        else if(w.spec.kind==gui::Kind::choice)w.state.selected=option.id;
                        else if(w.spec.kind==gui::Kind::menu)get("list").state.records.clear();
                    }
                } else if constexpr(std::is_same_v<T,gui::Activate>) {
                    if(w.spec.key.id=="details") {
                        previous_focus_=adapter_.focused();get("dialog").state.visible=true;view_.modal_root=gui::WidgetKey{"dialog",1};
                    } else if(w.spec.key.id=="dismiss") {
                        get("dialog").state.visible=false;view_.modal_root.reset();
                    } else add_row();
                }
                else if constexpr(std::is_same_v<T,gui::SubmitText>) {
                    gui::ServiceRequest request;request.id=next_service_++;request.kind=gui::ServiceKind::prompt;
                    request.title="Enter text";request.byte_limit=80;
                    if(!services_.enqueue(std::move(request)))throw std::runtime_error("Service request rejected");
                } else if constexpr(std::is_same_v<T,gui::SelectRecord>||std::is_same_v<T,gui::ActivateRecord>)w.state.selected=input.id;
                else if constexpr(std::is_same_v<T,gui::InvokeAction>) {
                    if(input.id=="refresh")++w.state.bitmap.revision;
                } else if constexpr(std::is_same_v<T,gui::PointerInput>) {
                    const auto grid=gui::device_rect(available.bounds,view_.display_scale);
                    if(const auto pixel=gui::pixel_at(available.bounds,grid.width,grid.height,input.position))
                        get("caption").state.text="Pixel "+std::to_string(unsigned(pixel->x))+", "+std::to_string(unsigned(pixel->y));
                }
            },e->input);
            publish();
            if(view_.modal_root!=previous_modal) {
                if(view_.modal_root)adapter_.focus(gui::WidgetKey{"dismiss",1});
                else {adapter_.focus(previous_focus_);previous_focus_.reset();}
            }
        } else if(const auto* page=std::get_if<gui::PageEvent>(&event)) {
            view_.active_page=page->id;publish();
        } else if(const auto* resize=std::get_if<gui::ResizeEvent>(&event)) {
            view_.client_size=resize->client_size;view_.display_scale=resize->display_scale;publish();
        }
    }
private:
    gui::Snapshot view_;
    gui::Adapter& adapter_;
    gui::ServiceQueue services_;
    unsigned next_row_=1;
    std::uint64_t next_service_=1;
    bool presentation_pending_=false;
    std::optional<gui::WidgetKey> previous_focus_;
    gui::Widget& add(std::string id,gui::Kind kind,gui::Rect rect,std::string parent={}) {
        gui::Widget w;w.spec.key.id=id;w.spec.binding=id;w.spec.kind=kind;w.spec.parent=std::move(parent);
        w.spec.page="main";w.state.bounds=rect;w.state.label=id;
        view_.widgets.push_back(std::move(w));return view_.widgets.back();
    }
    static gui::Widget& lookup(gui::Snapshot& view,std::string_view id) {
        for(auto& w:view.widgets)if(w.spec.key.id==id)return w;
        throw std::out_of_range("Unknown widget");
    }
    gui::Widget& get(std::string_view id) {return lookup(view_,id);}
    void add_row() {
        auto& list=get("list");
        if(double(list.state.records.size()+1)*list.spec.row_height>gui::coordinate_limit)
            throw std::length_error("Example list reached the supported content height");
        gui::Record row;row.id=std::to_string(next_row_++);row.accessible_text=get("editor").state.text;
        row.cells.push_back({row.accessible_text,{4,0,352,28},{}});row.activatable=true;
        list.state.records.push_back(std::move(row));
    }
    void publish() {
        // Values and accepted service replies remain authoritative on failure.
        // Stage layout separately and retain repaint debt for the host's next
        // tick/error recovery. A smaller resize can replace a failed large one.
        presentation_pending_=true;
        auto next=view_;
        next.page_bar={16,std::max(0.0,next.client_size.height-32),std::max(0.0,next.client_size.width-32),24};
        // Modal placement is shared and remains reachable after a small resize.
        const double mx=std::min(16.0,next.client_size.width/8),my=std::min(16.0,next.client_size.height/8);
        const double dw=std::min(416.0,std::max(0.0,next.client_size.width-2*mx));
        const double dh=std::min(240.0,std::max(0.0,next.client_size.height-2*my));
        const double dx=(next.client_size.width-dw)/2,dy=(next.client_size.height-dh)/2;
        lookup(next,"dialog").state.bounds={dx,dy,dw,dh};
        lookup(next,"dialog-title").state.bounds={dx+std::min(24.0,dw/8),dy+std::min(16.0,dh/8),dw*.85,std::min(32.0,dh/4)};
        lookup(next,"dialog-body").state.bounds={dx+dw*.06,dy+dh*.25,dw*.88,dh*.35};
        lookup(next,"dismiss").state.bounds={dx+dw*.54,dy+dh*.73,dw*.4,dh*.2};
        gui::LayoutNode panel;panel.id="panel";panel.kind=gui::LayoutKind::column;
        panel.padding={8,8,8,8};panel.gap=8;
        for(const auto* id:{"heading","choice","editor","toggle","button","menu","list"}) {
            gui::LayoutNode child;child.id=id;
            if(child.id!="heading")child.height=child.id=="list"?112:28;
            panel.children.push_back(std::move(child));
        }
        const auto width=std::min(384.0,std::max(0.0,next.client_size.width-32));
        const auto height=std::max(0.0,next.client_size.height-64);
        const auto layout=gui::compose_layout(panel,{16,16,width,height},[&](std::string_view id,double available) {
            const auto& w=lookup(next,id);
            if(w.spec.kind!=gui::Kind::label)return gui::Size{available,0};
            return adapter_.measure_text({w.state.text,w.state.font,available,next.display_scale,w.state.wrap});
        });
        for(const auto& box:gui::flatten_layout(layout))lookup(next,box.id).state.bounds=box.bounds;
        auto& group=lookup(next,"panel").state;
        group.content_size={layout.content.width,layout.content.height};
        group.bounds.height=std::min(height,layout.bounds.height);
        group.content_clip=gui::Rect{8,8,std::max(0.0,group.bounds.width-16),std::max(0.0,group.bounds.height-16)};
        lookup(next,"button").state.enabled=lookup(next,"toggle").state.checked;
        ++next.revision;adapter_.present(next);
        view_=std::move(next);presentation_pending_=false;
    }
};
