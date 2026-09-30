#include "gui/presentation.hpp"
#include "gui/memory_adapter.hpp"
#include "application.hpp"
#include <iostream>

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F operation) {try{operation();}catch(const std::invalid_argument&){return;}throw std::runtime_error("Invalid presentation accepted");}
gui::Widget item(std::string id,gui::Kind kind,gui::Rect box,std::string parent={}) {
    gui::Widget w;w.spec.key={std::move(id),1};w.spec.kind=kind;w.spec.parent=std::move(parent);w.state.bounds=box;return w;
}
void modal_and_chrome() {
    gui::Snapshot view;view.pages={{"a","One"},{"hidden","Hidden",true,false},{"b","Two",false,true}};view.active_page="a";
    view.page_bar={16,448,608,24};
    view.widgets={item("floating",gui::Kind::group,{80,80,400,200}),item("done",gui::Kind::button,{100,100,100,30},"floating"),
        item("underneath",gui::Kind::button,{100,100,100,30})};
    view.modal_root=gui::WidgetKey{"floating",1};view.key_bindings.push_back({gui::ShortcutKey::escape,{"done",1}});
    gui::validate_snapshot(view);
    const auto tabs=gui::page_tabs(view);
    check(tabs.size()==2&&tabs[0].bounds==gui::Rect{16,448,304,24}&&tabs[1].bounds==gui::Rect{320,448,304,24},"Chrome geometry diverged");
    check(!tabs[0].enabled&&!tabs[1].enabled,"Modal page switching enabled");
    const auto order=gui::paint_order(view);
    check(order[0].id=="underneath"&&order.back().id=="done","Modal did not paint above later background siblings");
    check(gui::availability(view,{"done",1}).enabled&&!gui::availability(view,{"underneath",1}).enabled,"Modal scope eligibility incorrect");
    gui::Event key=gui::ShortcutEvent{gui::ShortcutKey::escape};
    check(gui::normalize_event(view,key)&&std::get<gui::WidgetEvent>(key).target.id=="done","Shortcut did not normalize through ordinary activation");
    gui::Event page=gui::PageEvent{"a"};check(!gui::normalize_event(view,page),"Modal accepted page switch");
    gui::MemoryAdapter adapter;adapter.present(view);
    check(!adapter.focus(gui::WidgetKey{"underneath",1})&&adapter.focus_next()&&adapter.focused()->id=="done","Modal focus escaped");
    auto invalid=view;invalid.widgets[0].state.visible=false;rejects([&]{adapter.present(invalid);});
    invalid=view;invalid.widgets[0].state.enabled=false;rejects([&]{adapter.present(invalid);});
    invalid=view;invalid.key_bindings.push_back(invalid.key_bindings[0]);rejects([&]{adapter.present(invalid);});
    invalid=view;invalid.key_bindings[0].target.generation=2;rejects([&]{adapter.present(invalid);});
    check(adapter.snapshot().modal_root==view.modal_root,"Invalid modal publication changed current state");
    view.modal_root.reset();adapter.present(view);check(adapter.focus(gui::WidgetKey{"underneath",1}),"Closed modal retained input trap");
}
void scrolled_modality() {
    gui::Snapshot view;view.widgets={item("scroller",gui::Kind::group,{0,0,240,200}),
        item("modal",gui::Kind::group,{0,0,200,100},"scroller")};
    view.widgets[0].state.content_size={240,600};
    gui::MemoryAdapter adapter;adapter.present(view);adapter.scroll({"scroller",1},{0,200});
    view.modal_root=gui::WidgetKey{"modal",1};
    rejects([&]{adapter.present(view);});
    check(!adapter.snapshot().modal_root,"Invisible modal partially committed");
    adapter.scroll({"scroller",1},{0,0});adapter.present(view);
    rejects([&]{adapter.scroll({"scroller",1},{0,200});});
    check(adapter.scroll_offset({"scroller",1}).y==0,"Rejected modal scroll partially committed");
    view.client_size={0,0};adapter.present(view);view.client_size={640,480};adapter.present(view);
    check(adapter.resolved_availability({"modal",1}).visible,"Minimize lost retained modal");
}
void example_modality() {
    Example* app=nullptr;gui::MemoryAdapter adapter([&](const gui::Event& e){app->handle(e);},64*1024*1024,
        [](const gui::TextMeasureRequest& r){return gui::Size{r.available_width,16};});
    Example example(adapter);app=&example;example.select_editor();
    adapter.send(gui::WidgetEvent{{"details",1},gui::Activate{}});
    check(example.view().modal_root&&adapter.focused()==gui::WidgetKey{"dismiss",1},"Opening did not establish focus");
    adapter.send(gui::ShortcutEvent{gui::ShortcutKey::escape});
    check(!example.view().modal_root&&adapter.focused()==gui::WidgetKey{"editor",1},"Dismiss did not restore prior focus");
    check(example.view().page_bar==gui::Rect{16,448,608,24},"Example chrome is not shared client geometry");
}
}
int main() {
    try {modal_and_chrome();scrolled_modality();example_modality();std::cout<<"Shared chrome, modal, shortcut and focus checks passed\n";}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
