#include "gui/memory_adapter.hpp"
#include "../examples/application.hpp"
#include <iostream>
#include <limits>

namespace {
using namespace gui;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action) {
    bool threw=false;try {action();}catch(const std::exception&){threw=true;}
    check(threw,"Expected rejection");
}
Widget make(std::string id,Kind kind,Rect bounds) {
    Widget w;w.spec.key.id=std::move(id);w.spec.kind=kind;w.state.bounds=bounds;return w;
}
void public_commands() {
    unsigned events=0;
    MemoryAdapter memory([&](const Event&){++events;},4096,[](const TextMeasureRequest& request) {
        check(request.text=="Caption"&&request.font.bold&&request.wrap==TextWrap::word&&request.display_scale==1.5,
              "Measurement lost presentation values");
        return Size{request.available_width,18};
    });
    Adapter& adapter=memory;
    Snapshot view;
    auto editor=make("editor",Kind::text,{0,0,40,20});editor.state.text="abcd";editor.state.content_size={80,40};
    editor.state.options={{"a","A","a",true}};
    auto choice=make("choice",Kind::choice,{0,24,40,20});choice.state.options={{"a","A","",true}};
    auto bitmap=make("bitmap",Kind::bitmap,{50,0,10,10});bitmap.state.bitmap={"image",0,solid_bitmap(10,20,30)};
    view.widgets={editor,choice,bitmap};adapter.present(view);
    check(adapter.measure_text({"Caption",{12,true,Tone::normal},20,1.5,TextWrap::word})==Size{20,18},
          "Abstract text measurement failed");
    check(adapter.focus(editor.spec.key)&&adapter.focused()==editor.spec.key,"Abstract focus failed");
    adapter.text_selection(editor.spec.key,{4,1});
    check(adapter.text_selection(editor.spec.key)==TextSelection{4,1},"Abstract selection failed");
    adapter.scroll(editor.spec.key,{100,100});
    check(adapter.scroll_offset(editor.spec.key)==Point{40,20},"Abstract scroll failed");
    check(adapter.focus_next()&&adapter.focused()==choice.spec.key,"Abstract traversal failed");
    check(adapter.open_popup(choice.spec.key),"Abstract popup opening failed");adapter.close_popup(choice.spec.key);
    check(memory.choose_popup(choice.spec.key,0)==Delivery::ignored,"Abstract popup dismissal failed");
    check(adapter.resolved_availability(bitmap.spec.key).bounds==bitmap.state.bounds,"Abstract geometry failed");
    memory.repaint(bitmap.spec.key);adapter.invalidate(bitmap.spec.key,{1,1,2,2});
    check(memory.repaint(bitmap.spec.key)&&events==0,"Abstract commands were not silent");
    adapter.close();adapter.close();check(adapter.closed()&&!adapter.focused(),"Abstract closure failed");
    rejects([&]{adapter.measure_text({});});
}
void measured_requests_and_guards() {
    MemoryAdapter missing;rejects([&]{missing.measure_text({});});
    unsigned calls=0;bool fail=false,mutate=false,recurse=false;
    MemoryAdapter memory({},4096,[&](const TextMeasureRequest& request)->Size {
        ++calls;
        if(mutate)memory.close();
        if(recurse)return memory.measure_text(request);
        return fail?Size{-1,0}:Size{request.available_width,12};
    });
    Adapter& adapter=memory;
    for(auto invalid:{TextMeasureRequest{"\xc0\x80",{}},TextMeasureRequest{"a",{0}},
                      TextMeasureRequest{"a",{},-1},TextMeasureRequest{"a",{},0,0},
                      TextMeasureRequest{"a",{},0,1,static_cast<TextWrap>(99)}}) {
        rejects([&]{adapter.measure_text(invalid);});
    }
    check(calls==0,"Invalid request reached metrics provider");
    fail=true;rejects([&]{adapter.measure_text({});});fail=false;
    mutate=true;rejects([&]{adapter.measure_text({});});mutate=false;
    recurse=true;rejects([&]{adapter.measure_text({});});recurse=false;
    check(!adapter.closed()&&adapter.measure_text({})==Size{0,12},"Measurement failure corrupted adapter or guard");
}
void group_interior_and_actions() {
    std::vector<Event> events;MemoryAdapter memory([&](const Event& e){events.push_back(e);});Adapter& adapter=memory;
    Snapshot view;
    auto group=make("group",Kind::group,{10,10,80,80});group.state.content_size={160,160};
    group.state.content_clip=Rect{10,10,60,60};
    auto bitmap=make("bitmap",Kind::bitmap,{15,15,40,40});bitmap.spec.parent="group";bitmap.spec.pointer_input=true;
    bitmap.state.actions={{"reset","Reset image","",true},{"other","Other action","",false}};
    view.widgets={group,bitmap};adapter.present(view);
    check(adapter.resolved_availability(bitmap.spec.key).clip==Rect{20,20,35,35},"Group interior was not clipped");
    check(memory.send(WidgetEvent{bitmap.spec.key,PointerInput{PointerKind::click,{16,16}}})==Delivery::ignored,
          "Padding accepted pointer input");
    check(memory.send(WidgetEvent{bitmap.spec.key,InvokeAction{"reset"}})==Delivery::delivered,
          "Named keyboard/accessibility action was not delivered");
    check(memory.send(WidgetEvent{bitmap.spec.key,InvokeAction{"other"}})==Delivery::ignored&&
          memory.send(WidgetEvent{bitmap.spec.key,InvokeAction{"absent"}})==Delivery::ignored,"Invalid action was delivered");
    adapter.scroll(group.spec.key,{10,10});
    check(adapter.resolved_availability(bitmap.spec.key).bounds==Rect{5,5,40,40}&&
          adapter.resolved_availability(bitmap.spec.key).clip==Rect{20,20,25,25},"Interior clip moved with its scrolled children");
    check(adapter.focus(bitmap.spec.key),"Interactive bitmap was not focusable");
    view.widgets[1].state.actions.clear();view.widgets[1].state.visible=false;adapter.present(view);
    check(!adapter.focused()&&memory.send(WidgetEvent{bitmap.spec.key,InvokeAction{"reset"}})==Delivery::ignored,
          "Removed or hidden action remained eligible");
    auto invalid=view;invalid.widgets[0].state.content_clip->width=-1;rejects([&]{adapter.present(invalid);});
    invalid=view;invalid.widgets[1].state.content_clip=Rect{};rejects([&]{adapter.present(invalid);});
    invalid=view;invalid.widgets[1].state.actions={{"a","A","",true},{"a","B","",true}};rejects([&]{adapter.present(invalid);});
    invalid=view;invalid.widgets[0].spec.kind=static_cast<Kind>(99);rejects([&]{adapter.present(invalid);});
    invalid=view;invalid.widgets[0].state.font.tone=static_cast<Tone>(99);rejects([&]{adapter.present(invalid);});
    invalid=view;invalid.widgets[0].spec.text_policy.submit=static_cast<SubmitKey>(99);rejects([&]{adapter.present(invalid);});
    invalid=view;invalid.widgets[0].state.wrap=static_cast<TextWrap>(99);rejects([&]{adapter.present(invalid);});
    view.widgets[1].state.visible=true;adapter.present(view);
    check(memory.send(WidgetEvent{bitmap.spec.key,PointerInput{static_cast<PointerKind>(99),{25,25}}})==Delivery::ignored,
          "Unknown pointer kind was accepted");
    auto list=make("list",Kind::list,{100,100,80,80});list.state.records={{"a","A",{},true,true}};
    view.widgets.push_back(list);adapter.present(view);
    check(memory.list_key(list.spec.key,static_cast<ListKey>(99))==Delivery::ignored,"Unknown list key moved selection");
}
void shared_example() {
    Example* current=nullptr;unsigned measured=0;
    MemoryAdapter memory([&](const Event& event){current->handle(event);},64*1024*1024,
        [&](const TextMeasureRequest& request){++measured;return Size{request.available_width,24};});
    Adapter& adapter=memory;Example application(adapter);current=&application;
    check(measured>0,"Shared example bypassed generic measurement");
    application.select_editor();check(adapter.focused()==WidgetKey{"editor",1},"Shared example bypassed public focus");
    memory.replace({"editor",1},"Changed");
    check(find_widget(application.view(),{"editor",1})->state.text=="Changed","Generic edit did not reach shared application");
    memory.send(WidgetEvent{{"toggle",1},SetChecked{true}});memory.send(WidgetEvent{{"button",1},Activate{}});
    check(find_widget(application.view(),{"list",1})->state.records.size()==1,"Shared button action failed");
    memory.send(WidgetEvent{{"bitmap",1},InvokeAction{"refresh"}});
    check(find_widget(application.view(),{"bitmap",1})->state.bitmap.revision==1,"Shared bitmap action failed");
    memory.enter({"editor",1});const auto request=application.next_service();check(bool(request),"Shared service was not queued");
    check(application.complete_service({request->id,ServiceStatus::success,"Reply",{}}),"Shared service reply failed");
    memory.send(PageEvent{"other"});check(!adapter.resolved_availability({"editor",1}).visible,"Shared page update failed");
    memory.send(ResizeEvent{{320,240},1.5});check(application.view().display_scale==1.5,"Shared resize failed");
    memory.send(CloseEvent{});check(adapter.closed(),"Shared lifecycle failed");
}
void interior_scroll_reaches_content() {
    MemoryAdapter memory;Adapter& adapter=memory;Snapshot view;
    auto group=make("group",Kind::group,{10,10,80,80});
    group.state.content_clip=Rect{10,10,60,60};group.state.content_size={160,160};
    // Content starts at the child viewport origin (20,20); its last item ends
    // at (180,180). At maximum scroll, it must end at visible edge (80,80).
    auto child=make("last",Kind::button,{160,160,20,20});child.spec.parent="group";
    view.widgets={group,child};adapter.present(view);adapter.scroll(group.spec.key,{1000,1000});
    check(adapter.scroll_offset(group.spec.key)==Point{100,100},"Interior viewport did not determine scroll maximum");
    check(adapter.resolved_availability(child.spec.key).clip==Rect{60,60,20,20},"Last content remained unreachable");
    view.widgets[0].state.content_clip=Rect{0,0,80,80};adapter.present(view);
    check(adapter.scroll_offset(group.spec.key)==Point{80,80},"Expanded interior did not clamp retained scroll");
    view.widgets[0].state.content_clip=Rect{0,0,0,0};adapter.present(view);
    check(!adapter.resolved_availability(child.spec.key).visible,"Empty interior left a visible child");
}
void bitmap_action_popup() {
    std::vector<Event> events;MemoryAdapter memory([&](const Event& event){events.push_back(event);});Adapter& adapter=memory;
    Snapshot view;auto bitmap=make("bitmap",Kind::bitmap,{0,0,10,10});
    bitmap.state.actions={{"first","Same label","",true},{"second","Same label","",true}};
    view.widgets={bitmap};adapter.present(view);
    check(adapter.open_popup(bitmap.spec.key),"Abstract boundary could not open bitmap actions");
    std::swap(view.widgets[0].state.actions[0],view.widgets[0].state.actions[1]);adapter.present(view);
    check(memory.choose_popup(bitmap.spec.key,0)==Delivery::delivered,"Bitmap action popup did not dispatch");
    check(std::get<InvokeAction>(std::get<WidgetEvent>(events.back()).input).id=="first",
          "Bitmap popup reorder changed action identity");
    check(memory.choose_popup(bitmap.spec.key,0)==Delivery::ignored,"Completed bitmap popup retained its displayed items");
    check(adapter.open_popup(bitmap.spec.key),"Reordered bitmap popup did not reopen");
    view.widgets[0].state.actions[0].enabled=false;adapter.present(view);
    check(memory.choose_popup(bitmap.spec.key,0)==Delivery::ignored,"Disabled current bitmap action dispatched");
    check(adapter.open_popup(bitmap.spec.key),"Disabled bitmap action popup did not reopen");
    view.widgets[0].state.actions[0].enabled=true;adapter.present(view);
    check(memory.choose_popup(bitmap.spec.key,0)==Delivery::ignored,"Disabled displayed bitmap action dispatched");
    adapter.open_popup(bitmap.spec.key);view.widgets[0].state.actions.erase(view.widgets[0].state.actions.begin());adapter.present(view);
    check(memory.choose_popup(bitmap.spec.key,0)==Delivery::ignored,"Removed bitmap action dispatched");
    adapter.open_popup(bitmap.spec.key);adapter.close_popup(bitmap.spec.key);
    check(memory.choose_popup(bitmap.spec.key,0)==Delivery::ignored,"Abstract boundary did not dismiss bitmap actions");
    adapter.open_popup(bitmap.spec.key);view.widgets[0].state.visible=false;adapter.present(view);
    view.widgets[0].state.visible=true;adapter.present(view);
    check(memory.choose_popup(bitmap.spec.key,0)==Delivery::ignored&&events.size()==1,
          "Unavailable bitmap retained an action popup or silent commands emitted input");
    view.widgets[0].state.actions.clear();adapter.present(view);
    check(!adapter.open_popup(bitmap.spec.key),"Empty bitmap action popup opened");
}
}
int main() {
    try {public_commands();measured_requests_and_guards();group_interior_and_actions();interior_scroll_reaches_content();
        bitmap_action_popup();shared_example();
        std::cout<<"Public adapter checks passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
