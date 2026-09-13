#pragma once
#include "gui/contract.hpp"
#include "gui/layout.hpp"
#include "gui/runtime.hpp"
#include <type_traits>

// This shared application can be compiled with only the public boundary headers.
// The composition root supplies an Adapter; no native implementation is named.
class Example {
public:
    explicit Example(gui::Adapter& adapter):adapter_(adapter) {
        view_.title="General-purpose controls";
        view_.pages={{"main","Controls"},{"other","Other page"}};view_.active_page="main";
        add("panel",gui::Kind::group,{});
        add("heading",gui::Kind::label,{},"panel").state.text="Widget and bitmap example";
        auto& choice=add("choice",gui::Kind::choice,{},"panel");
        choice.state.options={{"first","First","",true},{"second","Second","",true}};
        choice.state.selected="first";
        auto& editor=add("editor",gui::Kind::text,{},"panel");
        editor.spec.text_policy={false,false,256,gui::SubmitKey::enter};editor.state.text="Example text";
        editor.state.options={{"short","Short text","Hello",true},{"empty","Empty text","",true}};
        add("toggle",gui::Kind::toggle,{},"panel").state.label="Enable action";
        add("button",gui::Kind::button,{},"panel").state.label="Add row";
        auto& menu=add("menu",gui::Kind::menu,{},"panel");
        menu.state.options={{"clear","Clear rows","",true}};
        auto& list=add("list",gui::Kind::list,{},"panel");
        list.spec.follow_tail=true;list.state.placeholder="No rows";
        auto& bitmap=add("bitmap",gui::Kind::bitmap,{440,48,160,160});
        bitmap.spec.pointer_input=true;bitmap.state.accessible_name="Color rectangle";
        bitmap.state.actions={{"refresh","Refresh image","",true}};
        bitmap.state.bitmap={"rectangle",0,gui::solid_bitmap(64,128,192)};
        add("caption",gui::Kind::label,{440,216,160,48}).state.text="A bitmap area";
        publish();
    }
    const gui::Snapshot& view() const {return view_;}
    std::optional<gui::ServiceRequest> next_service() {return services_.begin_next();}
    bool complete_service(gui::ServiceResult result) {
        if(!services_.complete(result))return false;
        if(result.status==gui::ServiceStatus::success) {
            get("caption").state.text=result.value;publish();
        }
        return true;
    }
    void select_editor() {
        if(adapter_.focus(gui::WidgetKey{"editor",1}))
            adapter_.text_selection({"editor",1},{0,get("editor").state.text.size()});
    }
    void set_caption(std::string text) {get("caption").state.text=std::move(text);publish();}
    void handle(const gui::Event& event) {
        if(adapter_.closed())return;
        if(const auto* e=std::get_if<gui::WidgetEvent>(&event)) {
            const auto* current=gui::find_widget(view_,e->target);
            const auto available=adapter_.resolved_availability(e->target);
            if(!current||!available.enabled||!available.visible)return;
            auto& w=get(e->target.id);
            std::visit([&](const auto& input) {
                using T=std::decay_t<decltype(input)>;
                if constexpr(std::is_same_v<T,gui::SetChecked>)w.state.checked=input.value;
                else if constexpr(std::is_same_v<T,gui::EditText>) {
                    if(input.base_text==w.state.text)w.state.text=input.value;
                } else if constexpr(std::is_same_v<T,gui::ChooseOption>) {
                    for(const auto& option:w.state.options)if(option.id==input.id&&option.enabled) {
                        if(w.spec.kind==gui::Kind::text)w.state.text=option.value;
                        else if(w.spec.kind==gui::Kind::choice)w.state.selected=option.id;
                        else if(w.spec.kind==gui::Kind::menu)get("list").state.records.clear();
                    }
                } else if constexpr(std::is_same_v<T,gui::Activate>)add_row();
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
        } else if(const auto* page=std::get_if<gui::PageEvent>(&event)) {
            for(const auto& current:view_.pages)if(current.id==page->id&&current.visible&&current.enabled) {
                view_.active_page=current.id;publish();break;
            }
        } else if(const auto* resize=std::get_if<gui::ResizeEvent>(&event)) {
            view_.client_size=resize->client_size;view_.display_scale=resize->display_scale;publish();
        } else if(std::holds_alternative<gui::CloseEvent>(event)) {services_.shutdown();adapter_.close();}
    }
private:
    gui::Snapshot view_;
    gui::Adapter& adapter_;
    gui::ServiceQueue services_;
    unsigned next_row_=1;
    std::uint64_t next_service_=1;
    gui::Widget& add(std::string id,gui::Kind kind,gui::Rect rect,std::string parent={}) {
        gui::Widget w;w.spec.key.id=id;w.spec.binding=id;w.spec.kind=kind;w.spec.parent=std::move(parent);
        w.spec.page="main";w.state.bounds=rect;w.state.label=id;
        view_.widgets.push_back(std::move(w));return view_.widgets.back();
    }
    gui::Widget& get(const std::string& id) {
        for(auto& w:view_.widgets)if(w.spec.key.id==id)return w;
        throw std::out_of_range("Unknown widget");
    }
    void add_row() {
        gui::Record row;row.id=std::to_string(next_row_++);row.accessible_text=get("editor").state.text;
        row.cells.push_back({row.accessible_text,{4,0,352,28},{}});row.activatable=true;
        get("list").state.records.push_back(std::move(row));
    }
    void publish() {
        gui::LayoutNode panel;panel.id="panel";panel.kind=gui::LayoutKind::column;
        panel.padding={8,8,8,8};panel.gap=8;
        for(const auto* id:{"heading","choice","editor","toggle","button","menu","list"}) {
            gui::LayoutNode child;child.id=id;
            if(child.id!="heading")child.height=child.id=="list"?112:28;
            panel.children.push_back(std::move(child));
        }
        const auto width=std::min(384.0,std::max(0.0,view_.client_size.width-32));
        const auto height=std::max(0.0,view_.client_size.height-32);
        const auto layout=gui::compose_layout(panel,{16,16,width,height},[&](std::string_view id,double available) {
            const auto& w=get(std::string(id));
            if(w.spec.kind!=gui::Kind::label)return gui::Size{available,0};
            return adapter_.measure_text({w.state.text,w.state.font,available,view_.display_scale,w.state.wrap});
        });
        for(const auto& box:gui::flatten_layout(layout))get(box.id).state.bounds=box.bounds;
        auto& group=get("panel").state;
        group.content_size={layout.content.width,layout.content.height};
        group.bounds.height=std::min(height,layout.bounds.height);
        group.content_clip=gui::Rect{8,8,std::max(0.0,group.bounds.width-16),std::max(0.0,group.bounds.height-16)};
        get("button").state.enabled=get("toggle").state.checked;
        ++view_.revision;adapter_.present(view_);
    }
};
