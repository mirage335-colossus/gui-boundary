#include "gui/framebuffer.hpp"
#include "gui/layout.hpp"
#include "gui/terminal.hpp"
#include "gui/web.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
const gui::WidgetKey note{"draft:aa",1},route{"route#b",1},commit{"commit/7",1},rows{"history@r",1};
const gui::WidgetKey gate{"permission^k",1},opener{"review?f",1},clearer{"commands:z",1};
const gui::WidgetKey modal{"modal+u",1},dismiss{"dismiss=y",1},image{"image%r",1};
constexpr std::string_view primary="page/parcel",secondary="page/review";

// An independent application: all feature behavior and layout live here. None
// of its opaque identities occur in a renderer, host or shipped demonstration.
class ParcelFeature {
public:
    explicit ParcelFeature(gui::Adapter& adapter):adapter_(adapter) {
        view.title="Parcel review";view.pages={{std::string(primary),"Parcels"},{std::string(secondary),"Review"}};
        view.active_page=primary;
        add({"layout:q",1},gui::Kind::group,{});
        add({"caption:a",1},gui::Kind::label,{0,0,0,0},"layout:q").state.text="Parcel review";
        auto& editor=add(note,gui::Kind::text,{},"layout:q");editor.spec.text_policy={false,false,96,gui::SubmitKey::none};editor.state.text="Seed";
        auto& choice=add(route,gui::Kind::choice,{},"layout:q");
        choice.state.options={{"option/A","Northern route","",true},{"option/B","Southern route","",true}};choice.state.selected="option/B";
        add(commit,gui::Kind::button,{},"layout:q").state.label="Register parcel";
        auto& list=add(rows,gui::Kind::list,{},"layout:q");list.spec.row_height=32;list.spec.follow_tail=true;list.state.placeholder="No registrations";
        auto& bitmap=add(image,gui::Kind::bitmap,{440,64,128,128});
        bitmap.state.actions={{"paint/v2","Regenerate","",true}};bitmap.state.bitmap={"opaque-pixels",1,gui::solid_bitmap(20,60,100)};
        order_={"caption:a",note.id,route.id,commit.id,rows.id};publish();
    }
    gui::Snapshot view;
    std::vector<std::string> intents;
    unsigned registrations=0;
    gui::Widget& get(const gui::WidgetKey& key) {
        for(auto& widget:view.widgets)if(widget.spec.key==key)return widget;
        throw std::logic_error("Missing feature widget");
    }
    void extend() {
        add(gate,gui::Kind::toggle,{},"layout:q").state.label="Permit registration";
        add(opener,gui::Kind::button,{},"layout:q").state.label="Review registration";
        add(clearer,gui::Kind::menu,{},"layout:q").state.options={{"erase/all","Remove registrations","",true}};
        auto& overlay=add(modal,gui::Kind::group,{96,80,448,288});overlay.state.visible=false;
        add({"description:f",1},gui::Kind::label,{112,112,400,64},modal.id).state.text="Only this subtree accepts input";
        add(dismiss,gui::Kind::button,{320,304,176,32},modal.id).state.label="Return to parcels";
        auto& review=add({"summary:w",1},gui::Kind::label,{24,48,400,48});review.spec.page=secondary;review.state.text="Registration summary";
        order_={"caption:a",route.id,note.id,gate.id,commit.id,rows.id,opener.id,clearer.id};
        // Reordering declarations is legal independently of stable identities.
        std::swap(view.widgets[2],view.widgets[3]);
        get(commit).state.enabled=false;
        view.key_bindings={{gui::ShortcutKey::f3,opener},
                           {gui::ShortcutKey::escape,dismiss}};
        publish();
    }
    void handle(gui::Event event) {
        if(!gui::normalize_event(view,event,[&](const gui::WidgetKey& key){return adapter_.scroll_offset(key);}))return;
        if(auto* input=std::get_if<gui::WidgetEvent>(&event)) {
            auto& widget=get(input->target);
            std::string token=input->target.id+":"+std::to_string(input->input.index());
            std::visit([&](const auto& value) {
                using T=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<T,gui::EditText>) {widget.state.text=value.value;token+=":"+value.value;}
                else if constexpr(std::is_same_v<T,gui::SetChecked>) {widget.state.checked=value.value;get(commit).state.enabled=value.value;}
                else if constexpr(std::is_same_v<T,gui::ChooseOption>) {
                    token+=":"+value.id;
                    if(widget.spec.kind==gui::Kind::choice)widget.state.selected=value.id;
                    else if(widget.spec.key==clearer)get(rows).state.records.clear();
                } else if constexpr(std::is_same_v<T,gui::SelectRecord>||std::is_same_v<T,gui::ActivateRecord>) {
                    widget.state.selected=value.id;token+=":"+value.id;
                } else if constexpr(std::is_same_v<T,gui::InvokeAction>) {++widget.state.bitmap.revision;token+=":"+value.id;}
                else if constexpr(std::is_same_v<T,gui::Activate>) {
                    if(input->target==commit) {
                        gui::Record record;record.id="entry:"+std::to_string(++registrations);
                        record.accessible_text=get(note).state.text;record.activatable=true;
                        record.cells={{record.accessible_text,{4,0,176,32},{}},{get(route).state.selected.value_or(""),{184,0,168,32},{}}};
                        get(rows).state.records.push_back(std::move(record));
                    } else if(input->target==opener) {get(modal).state.visible=true;view.modal_root=modal;}
                    else if(input->target==dismiss) {get(modal).state.visible=false;view.modal_root.reset();}
                }
            },input->input);
            intents.push_back(std::move(token));publish();
        } else if(const auto* page=std::get_if<gui::PageEvent>(&event)) {
            view.active_page=page->id;intents.push_back("page:"+page->id);publish();
        }
    }
    void publish() {
        gui::LayoutNode layout;layout.id="layout:q";layout.kind=gui::LayoutKind::column;layout.padding={8,8,8,8};layout.gap=8;
        for(const auto& id:order_) {
            gui::LayoutNode child;child.id=id;child.height=id==rows.id?96:id=="caption:a"?16:32;
            layout.children.push_back(std::move(child));
        }
        const auto boxes=gui::flatten_layout(gui::compose_layout(layout,{16,16,384,416},
            [](std::string_view,double width){return gui::Size{width,16};}));
        for(const auto& box:boxes)for(auto& widget:view.widgets)if(widget.spec.key.id==box.id)widget.state.bounds=box.bounds;
        view.page_bar={16,448,608,24};++view.revision;adapter_.present(view);
    }
    void remove(const gui::WidgetKey& key) {
        std::erase(order_,key.id);
        std::erase_if(view.widgets,[&](const gui::Widget& widget){return widget.spec.key==key;});
        publish();
    }
private:
    gui::Adapter& adapter_;
    std::vector<std::string> order_;
    gui::Widget& add(gui::WidgetKey key,gui::Kind kind,gui::Rect bounds,std::string parent={}) {
        gui::Widget widget;widget.spec.key=std::move(key);widget.spec.kind=kind;widget.spec.parent=std::move(parent);
        widget.spec.page=primary;widget.state.bounds=bounds;view.widgets.push_back(std::move(widget));return view.widgets.back();
    }
};

template<class Adapter> gui::MemoryAdapter& policy(Adapter& adapter) {
    if constexpr(std::is_same_v<Adapter,gui::MemoryAdapter>)return adapter;
    else return adapter.policy();
}

// Each driver enters through its actual input boundary: semantic reference,
// terminal bytes/shared keys, software pointer input, or sequenced browser JSON.
template<class Adapter> class Driver {
public:
    explicit Driver(Adapter& value):adapter(value) {
        if constexpr(std::is_same_v<Adapter,gui::WebAdapter>)web=std::make_unique<gui::WebSession>(adapter,"synthetic-epoch");
    }
    Adapter& adapter;
    void activate(const gui::WidgetKey& key) {
        if constexpr(std::is_same_v<Adapter,gui::TerminalAdapter>) {adapter.focus(key);adapter.input("\r");}
        else if constexpr(std::is_same_v<Adapter,gui::FramebufferAdapter>) {
            const auto area=adapter.resolved_availability(key).bounds;
            adapter.pointer({gui::PointerKind::click,{area.x+area.width/2,area.y+area.height/2}});
        } else event(key,gui::Activate{});
    }
    void edit(const gui::WidgetKey& key,const std::string& value) {
        if constexpr(std::is_base_of_v<gui::InteractiveAdapter,Adapter>) {
            adapter.focus(key);adapter.key(gui::Key::select_all);
            if constexpr(std::is_same_v<Adapter,gui::TerminalAdapter>)adapter.input("\x1b[200~"+value+"\x1b[201~");
            else adapter.text(value);
        } else event(key,gui::EditText{value,gui::find_widget(adapter.snapshot(),key)->state.text});
    }
    void toggle(const gui::WidgetKey& key,bool value) {
        if constexpr(std::is_base_of_v<gui::InteractiveAdapter,Adapter>) {adapter.focus(key);adapter.key(gui::Key::space);}
        else event(key,gui::SetChecked{value});
    }
    void open(const gui::WidgetKey& key) {
        if constexpr(std::is_same_v<Adapter,gui::WebAdapter>)wire({{"type","popupOpen"},{"key",gui::web_detail::key(key)}});
        else check(adapter.open_popup(key),"Synthetic popup did not open");
    }
    void choose(const gui::WidgetKey& key,std::size_t displayed_index,std::string id) {
        if constexpr(std::is_base_of_v<gui::InteractiveAdapter,Adapter>) {
            for(std::size_t count=0;adapter.popup()&&adapter.popup()->index!=displayed_index&&count<16;++count)adapter.key(gui::Key::down);
            adapter.key(gui::Key::enter);
        } else if constexpr(std::is_same_v<Adapter,gui::WebAdapter>)wire({{"type","popupChoice"},{"key",gui::web_detail::key(key)},{"id",std::move(id)}});
        else adapter.choose_popup(key,displayed_index);
    }
    void list_down(const gui::WidgetKey& key) {
        if constexpr(std::is_base_of_v<gui::InteractiveAdapter,Adapter>) {adapter.focus(key);adapter.key(gui::Key::down);}
        else if constexpr(std::is_same_v<Adapter,gui::WebAdapter>)wire({{"type","listKey"},{"key",gui::web_detail::key(key)},{"value","down"}});
        else adapter.list_key(key,gui::ListKey::down);
    }
    void shortcut(gui::ShortcutKey key) {
        if constexpr(std::is_base_of_v<gui::InteractiveAdapter,Adapter>)adapter.key(key==gui::ShortcutKey::escape?gui::Key::escape:gui::Key::f3);
        else if constexpr(std::is_same_v<Adapter,gui::WebAdapter>)wire({{"type","shortcut"},{"key",int(key)},{"control",false},{"shift",false},{"alt",false}});
        else adapter.send(gui::ShortcutEvent{key});
    }
    void page(std::string id) {
        if constexpr(std::is_same_v<Adapter,gui::WebAdapter>)wire({{"type","page"},{"id",std::move(id)}});
        else policy(adapter).send(gui::PageEvent{std::move(id)});
    }
    void event(const gui::WidgetKey& key,gui::Input input) {
        if constexpr(std::is_same_v<Adapter,gui::WebAdapter>) {
            gui::web_detail::Json::Object operation{{"key",gui::web_detail::key(key)}};
            std::visit([&](const auto& value) {
                using T=std::decay_t<decltype(value)>;
                if constexpr(std::is_same_v<T,gui::Activate>)operation["type"]="activate";
                else if constexpr(std::is_same_v<T,gui::EditText>) {operation["type"]="edit";operation["value"]=value.value;operation["base"]=value.base_text;}
                else if constexpr(std::is_same_v<T,gui::SetChecked>) {operation["type"]="checked";operation["value"]=value.value;}
                else if constexpr(std::is_same_v<T,gui::ChooseOption>) {operation["type"]="choose";operation["id"]=value.id;}
                else if constexpr(std::is_same_v<T,gui::SelectRecord>) {operation["type"]="select";operation["id"]=value.id;}
                else if constexpr(std::is_same_v<T,gui::InvokeAction>) {operation["type"]="action";operation["id"]=value.id;}
                else throw std::logic_error("Fixture event has no wire encoding");
            },input);wire(std::move(operation));
        } else policy(adapter).send(gui::WidgetEvent{key,std::move(input)});
    }
    void paint() {
        if constexpr(std::is_same_v<Adapter,gui::TerminalAdapter>)check(!adapter.render().empty(),"Synthetic terminal render failed");
        else if constexpr(std::is_same_v<Adapter,gui::FramebufferAdapter>)check(bool(adapter.frame().pixels),"Synthetic framebuffer render failed");
        else if constexpr(std::is_same_v<Adapter,gui::WebAdapter>) {
            const auto output=adapter.presentation();
            check(output.at("widgets").array().size()==adapter.snapshot().widgets.size(),"Browser omitted synthetic declarations");
            for(const auto& widget:output.at("widgets").array()) {
                const auto key=gui::web_detail::key(widget.at("key"));const auto area=adapter.resolved_availability(key).bounds;
                const auto& bounds=widget.at("bounds").array();
                check(bounds[0].number()==area.x&&bounds[1].number()==area.y&&bounds[2].number()==area.width&&bounds[3].number()==area.height,
                      "Browser relocated synthetic shared geometry");
            }
        }
    }
private:
    std::unique_ptr<gui::WebSession> web;
    std::uint64_t sequence=0;
    void wire(gui::web_detail::Json::Object operation) {
        using namespace gui::web_detail;
        const auto response=Parser(web->receive(encode(Json::Object{{"epoch","synthetic-epoch"},{"seq",std::to_string(++sequence)},{"operation",std::move(operation)}}))).parse();
        check(integer(response.at("ack"))==sequence,"Synthetic browser operation was not acknowledged");
    }
};

struct Result {std::vector<std::string> intents;std::vector<std::pair<std::string,gui::Rect>> geometry;};
template<class Adapter> Result extension_scenario() {
    ParcelFeature* app=nullptr;
    Adapter adapter([&](const gui::Event& event){if(app)app->handle(event);});
    ParcelFeature feature(adapter);app=&feature;Driver<Adapter> driver(adapter);driver.paint();
    driver.edit(note,"Parcel \xce\xbb");driver.activate(commit);
    check(feature.registrations==1,"Initial synthetic application did not register data");
    feature.extend();driver.paint();
    const auto held=feature.intents.size();driver.event(commit,gui::Activate{});
    check(feature.intents.size()==held,"Added feature failed to disable prior operation");
    driver.toggle(gate,true);driver.activate(commit);
    check(feature.registrations==2,"Added toggle did not control prior feature");

    driver.open(route);
    std::reverse(feature.get(route).state.options.begin(),feature.get(route).state.options.end());
    feature.get(route).state.options[1].label="Renamed route";feature.publish();
    driver.choose(route,0,"option/A");
    check(feature.get(route).state.selected=="option/A","Open popup lost stable option identity after reorder");
    driver.open(route); // Current displayed order is B, A.
    feature.get(route).state.options.erase(feature.get(route).state.options.begin());feature.publish();
    const auto removed=feature.intents.size();driver.choose(route,0,"option/B");
    check(feature.intents.size()==removed,"Removed popup option still activated");

    driver.list_down(rows);
    const auto selected=feature.get(rows).state.selected;
    std::reverse(feature.get(rows).state.records.begin(),feature.get(rows).state.records.end());feature.publish();
    check(feature.get(rows).state.selected==selected,"Record reorder changed stable selection");
    feature.get(rows).state.records.erase(std::remove_if(feature.get(rows).state.records.begin(),feature.get(rows).state.records.end(),
        [&](const gui::Record& record){return selected==record.id;}),feature.get(rows).state.records.end());feature.publish();
    const auto dropped=feature.intents.size();driver.event(rows,gui::SelectRecord{*selected});
    check(feature.intents.size()==dropped,"Removed record accepted stale selection");

    driver.shortcut(gui::ShortcutKey::f3);driver.paint();
    check(feature.view.modal_root==modal,"Added shortcut did not open the shared modal subtree");
    const auto modal_count=feature.intents.size();
    driver.event(commit,gui::Activate{});driver.page(std::string(secondary));
    check(feature.intents.size()==modal_count,"Background operation escaped modal eligibility");
    check(!adapter.focus(commit),"Modal focus escaped to background widget");
    driver.shortcut(gui::ShortcutKey::escape);check(!feature.view.modal_root,"Modal Escape did not activate its shared target");
    driver.page(std::string(secondary));driver.paint();
    const auto hidden=feature.intents.size();driver.event(commit,gui::Activate{});
    check(feature.intents.size()==hidden,"Inactive-page input was delivered");
    driver.page(std::string(primary));
    driver.open(image);driver.choose(image,0,"paint/v2");
    check(feature.get(image).state.bitmap.revision==2,"Arbitrary bitmap action was not delivered");
    driver.open(clearer);driver.choose(clearer,0,"erase/all");
    check(feature.get(rows).state.records.empty(),"Added menu operation failed");

    adapter.focus(note);adapter.text_selection(note,{0,2});
    auto& replacement=feature.get(note);replacement.spec.key.generation=2;replacement.spec.kind=gui::Kind::label;
    replacement.spec.text_policy={};replacement.state.text="Archived parcel field";feature.publish();
    const auto replaced=feature.intents.size();driver.event(note,gui::EditText{"Late","Parcel \xce\xbb"});
    check(feature.intents.size()==replaced&&!adapter.focus(note),"Replaced generation retained obsolete authority");
    const auto newer=gui::WidgetKey{note.id,2};
    check(gui::find_widget(adapter.snapshot(),newer)->spec.kind==gui::Kind::label,"Kind replacement failed");
    feature.remove(clearer);driver.paint();
    const auto absent=feature.intents.size();driver.event(clearer,gui::ChooseOption{"erase/all"});
    check(feature.intents.size()==absent,"Removed menu retained authority");
    Result result{feature.intents,{}};
    for(const auto& widget:adapter.snapshot().widgets)result.geometry.emplace_back(widget.spec.key.id,widget.state.bounds);
    return result;
}
}

int main() {
    try {
        const auto memory=extension_scenario<gui::MemoryAdapter>();
        const auto terminal=extension_scenario<gui::TerminalAdapter>();
        const auto framebuffer=extension_scenario<gui::FramebufferAdapter>();
        const auto web=extension_scenario<gui::WebAdapter>();
        for(const auto* candidate:{&terminal,&framebuffer,&web}) {
            check(candidate->intents==memory.intents,"Renderer changed normalized application intent");
            check(candidate->geometry==memory.geometry,"Renderer changed the shared feature layout");
        }
        std::cout<<"Shared extension passed across memory, terminal, framebuffer and browser adapters\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
