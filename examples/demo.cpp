#include "gui/memory_adapter.hpp"
#include "gui/runtime.hpp"
#include <iostream>

// The shared application owns values and event meaning. It sees no native
// toolkit objects. Each widget uses a stable key and an opaque binding name.
class Example {
public:
    Example():adapter_([this](const gui::Event& event){handle(event);}) {
        view_.title="General-purpose controls";
        add("heading",gui::Kind::label,{16,8,400,24}).state.text="Widget and bitmap example";
        auto& choice=add("choice",gui::Kind::choice,{16,48,180,30});
        choice.state.options={{"first","First","",true},{"second","Second","",true}};
        choice.state.selected="first";
        auto& editor=add("editor",gui::Kind::text,{16,92,300,30});
        editor.spec.text_policy={false,false,256,gui::SubmitKey::enter};editor.state.text="Example text";
        editor.state.options={{"short","Short text","Hello",true},{"empty","Empty text","",true}};
        add("toggle",gui::Kind::toggle,{16,136,180,30}).state.label="Enable action";
        add("button",gui::Kind::button,{16,180,180,30}).state.label="Add row";
        auto& menu=add("menu",gui::Kind::menu,{216,180,180,30});
        menu.state.options={{"clear","Clear rows","",true}};
        auto& list=add("list",gui::Kind::list,{16,224,380,112});
        list.spec.follow_tail=true;list.state.placeholder="No rows";
        auto& bitmap=add("bitmap",gui::Kind::bitmap,{420,48,160,160});
        bitmap.spec.pointer_input=true;bitmap.state.accessible_name="Color rectangle";
        bitmap.state.bitmap={"rectangle",0,gui::solid_bitmap(64,128,192)};
        add("caption",gui::Kind::label,{420,216,160,48}).state.text="A bitmap area";
        publish();
    }
    void demonstrate() {
        adapter_.send(gui::WidgetEvent{{"toggle",1},gui::SetChecked{true}});
        adapter_.open_popup({"choice",1});adapter_.choose_popup({"choice",1},1);
        adapter_.text_selection({"editor",1},{0,get("editor").state.text.size()});
        adapter_.replace({"editor",1},"Updated text");
        adapter_.send(gui::WidgetEvent{{"button",1},gui::Activate{}});
        adapter_.enter({"editor",1});
        adapter_.repaint({"bitmap",1});

        // A host implementation starts the actual dialog when begin_next()
        // returns a request. This example explicitly supplies a test response.
        gui::ServiceRequest request;request.id=1;request.kind=gui::ServiceKind::prompt;
        request.title="Enter text";request.byte_limit=80;services_.enqueue(request);
        if(const auto current=services_.begin_next()) {
            gui::ServiceResult reply;reply.id=current->id;reply.value="Entered text";
            if(services_.complete(reply)&&reply.status==gui::ServiceStatus::success)
                std::cout<<"Service result: "<<reply.value<<'\n';
        }
        gui::UiQueue queue;
        queue.post([this]{get("caption").state.text="Updated on the UI thread";publish();});
        const auto drained=queue.drain(8);
        if(!drained.errors.empty())std::rethrow_exception(drained.errors.front());
        std::cout<<"Selected option: "<<*get("choice").state.selected
                 <<"\nText: "<<get("editor").state.text
                 <<"\nRows: "<<get("list").state.records.size()
                 <<"\nBitmap: "<<adapter_.image({"bitmap",1}).width()<<'x'
                 <<adapter_.image({"bitmap",1}).height()<<'\n';
        queue.shutdown();services_.shutdown();adapter_.close();
    }
private:
    gui::Snapshot view_;
    gui::MemoryAdapter adapter_;
    gui::ServiceQueue services_;
    unsigned next_row_=1;
    gui::Widget& add(std::string id,gui::Kind kind,gui::Rect rect) {
        gui::Widget w;w.spec.key.id=id;w.spec.binding=id;w.spec.kind=kind;w.state.bounds=rect;
        w.state.label=id;view_.widgets.push_back(std::move(w));return view_.widgets.back();
    }
    gui::Widget& get(const std::string& id) {
        for(auto& w:view_.widgets)if(w.spec.key.id==id)return w;
        throw std::out_of_range("Unknown widget");
    }
    void add_row() {
        gui::Record row;row.id=std::to_string(next_row_++);row.accessible_text=get("editor").state.text;
        row.cells.push_back({row.accessible_text,{4,0,360,28},{}});row.activatable=true;
        get("list").state.records.push_back(std::move(row));
    }
    void publish() {
        get("button").state.enabled=get("toggle").state.checked;
        ++view_.revision;adapter_.present(view_);
    }
    void handle(const gui::Event& event) {
        if(const auto* e=std::get_if<gui::WidgetEvent>(&event)) {
            auto& w=get(e->target.id);
            // Repeat authoritative eligibility checks in a real application
            // when its model can advance independently of the last snapshot.
            if(e->target!=w.spec.key||!w.state.enabled||!w.state.visible)return;
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
                } else if constexpr(std::is_same_v<T,gui::Activate>||std::is_same_v<T,gui::SubmitText>)add_row();
                else if constexpr(std::is_same_v<T,gui::SelectRecord>||std::is_same_v<T,gui::ActivateRecord>)w.state.selected=input.id;
            },e->input);
            publish();
        } else if(const auto* resize=std::get_if<gui::ResizeEvent>(&event)) {
            view_.client_size=resize->client_size;view_.display_scale=resize->display_scale;publish();
        } else if(std::holds_alternative<gui::CloseEvent>(event)) {services_.shutdown();adapter_.close();}
    }
};
int main() {
    try {Example example;example.demonstrate();}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
