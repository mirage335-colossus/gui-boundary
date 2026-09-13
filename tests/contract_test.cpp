#include "gui/memory_adapter.hpp"
#include <iostream>
#include <limits>
#include <thread>

namespace {
using namespace gui;
void check(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
template<class F> void rejects(F action) {
    bool threw=false;try {action();}catch(const std::exception&){threw=true;}
    check(threw,"Expected rejection");
}
Widget widget(std::string id,Kind kind,Rect bounds) {
    Widget w;w.spec.key.id=std::move(id);w.spec.kind=kind;w.state.bounds=bounds;
    w.state.label=w.spec.key.id;return w;
}
Snapshot fixture() {
    Snapshot s;s.title="Widget example";s.pages={{"one","One"},{"two","Two"}};s.active_page="one";
    s.widgets.push_back(widget("choice",Kind::choice,{0,0,100,30}));
    s.widgets.back().state.options={{"a","Same","",true},{"b","Same","",true}};
    s.widgets.push_back(widget("editor",Kind::text,{0,40,100,30}));
    s.widgets.back().spec.text_policy={false,false,20,SubmitKey::enter};
    s.widgets.back().state.text="ab";
    s.widgets.push_back(widget("list",Kind::list,{0,80,100,40}));
    s.widgets.back().spec.row_height=20;s.widgets.back().spec.follow_tail=true;
    s.widgets.back().state.records={{"a","A",{},true,true},{"b","B",{},false,false},{"c","C",{},true,true}};
    s.widgets.back().state.selected="a";
    s.widgets.push_back(widget("image",Kind::bitmap,{120,0,8,8}));
    s.widgets.back().state.bitmap={"solid",0,solid_bitmap(255,0,0)};s.widgets.back().spec.pointer_input=true;
    s.widgets.push_back(widget("toggle",Kind::toggle,{120,40,100,30}));
    s.widgets.push_back(widget("button",Kind::button,{120,80,100,30}));s.widgets.back().spec.page="one";
    s.widgets.push_back(widget("menu",Kind::menu,{120,120,100,30}));
    s.widgets.back().state.options={{"a","Item","",true}};
    return s;
}
void text_contract() {
    check(valid_utf8("\xc3\xa9"),"Valid UTF-8 rejected");
    for(auto text:{std::string("\0",1),std::string("\xc0\x80"),std::string("\xed\xa0\x80"),
                   std::string("\xf4\x90\x80\x80"),std::string("\xe2\x82")})
        check(!valid_utf8(text),"Invalid UTF-8 accepted");
    TextPolicy p;p.max_bytes=4;
    auto edit=replace_text("a\xc3\xa9",{3,1},"xy",p);
    check(edit.changed&&edit.text=="axy"&&edit.selection.caret==3,"Reversed replacement failed");
    edit=replace_text("a\xc3\xa9",{2,2},"!",p);
    check(edit.changed&&edit.text=="a!\xc3\xa9","Caret split a UTF-8 codepoint");
    edit=replace_text("abc",{1,2},"long",p);
    check(!edit.changed&&edit.text=="abc"&&edit.selection==TextSelection{1,2},"Rejected edit changed state");
    check(!replace_text("abc",{0,3},"abc",p).changed,"Equal replacement was not silent");
    check(!text_error("a\nb",p).empty(),"Single-line text accepted newline");
    p.multiline=true;check(text_error("a\nb",p).empty(),"Multiline text rejected newline");
    p.read_only=true;check(!replace_text("a",{},"b",p).error.empty(),"Read-only editor changed");
}
void geometry_contract() {
    const auto a=device_rect({0.2,0,0.8,1},1.25),b=device_rect({1,0,1,1},1.25);
    check(a.x+static_cast<int>(a.width)==b.x,"Snapped neighboring edges disagree");
    check(!pixel_at({0,0,10,10},20,20,{10,0}),"Right edge accepted");
    check(pixel_at({0,0,10,10},20,20,{9.9,9.9})==Point{19,19},"Pixel mapping failed");
    rejects([]{device_rect({0,0,1,1},std::numeric_limits<double>::infinity());});
    const std::vector<Allocation> allocations{{20,0},{0,1},{0,3}};
    const auto boxes=arrange({0,0,100,10},Axis::horizontal,allocations,4);
    check(boxes[0].width==20&&boxes[1].width==18&&boxes[2].width==54,"Layout weights incorrect");
}
void events_and_identity() {
    std::vector<Event> events;MemoryAdapter adapter([&](const Event& e){events.push_back(e);});
    auto s=fixture();adapter.present(s);check(events.empty(),"Presentation emitted input");
    check(adapter.open_popup({"choice",1}),"Popup failed to open");
    std::swap(s.widgets[0].state.options[0],s.widgets[0].state.options[1]);adapter.present(s);
    check(adapter.choose_popup({"choice",1},0)==Delivery::delivered,"Popup did not dispatch");
    check(std::get<ChooseOption>(std::get<WidgetEvent>(events.back()).input).id=="a","Popup index changed identity");
    adapter.open_popup({"choice",1});s.widgets[0].state.options.erase(s.widgets[0].state.options.begin());adapter.present(s);
    check(adapter.choose_popup({"choice",1},0)==Delivery::ignored,"Removed popup item dispatched");
    check(adapter.send(WidgetEvent{{"editor",1},EditText{"new","old"}})==Delivery::ignored,"Stale edit accepted");
    check(adapter.send(WidgetEvent{{"editor",1},EditText{"new","ab"}})==Delivery::delivered,"Valid edit rejected");
    check(adapter.send(WidgetEvent{{"editor",1},SetChecked{true}})==Delivery::ignored,"Wrong event kind accepted");
    check(adapter.enter({"editor",1})&&!adapter.enter({"editor",1},false,true),"Submit key policy failed");
    s.widgets[1].state.enabled=false;adapter.present(s);
    check(adapter.enter({"editor",1}),"Declared submit key was not consumed");
    check(adapter.send(WidgetEvent{{"editor",1},EditText{"new","ab"}})==Delivery::ignored,"Disabled edit accepted");
    check(adapter.focus(WidgetKey{"button",1}),"Focus failed");
    s.active_page="two";adapter.present(s);
    check(!adapter.focused()&&adapter.send(WidgetEvent{{"button",1},Activate{}})==Delivery::ignored,"Inactive page retained input");
    auto bad=s;bad.widgets[0].state.options.push_back(bad.widgets[0].state.options.front());
    rejects([&]{adapter.present(bad);});check(adapter.snapshot().widgets[0].state.options.size()==1,"Invalid snapshot partially applied");
    bad=s;bad.widgets[0].spec.kind=Kind::menu;rejects([&]{adapter.present(bad);});
    s.widgets.erase(s.widgets.begin());adapter.present(s);
    s.widgets.push_back(widget("choice",Kind::choice,{0,0,100,30}));rejects([&]{adapter.present(s);});
    s.widgets.back().spec.key.generation=2;adapter.present(s);
    check(adapter.send(WidgetEvent{{"choice",1},ChooseOption{"a"}})==Delivery::ignored,"Deleted generation accepted input");
    bool wrong_thread=false;std::thread worker([&]{try {adapter.focus_next();}catch(const std::logic_error&){wrong_thread=true;}});worker.join();
    check(wrong_thread,"Adapter accepted worker thread access");
    adapter.close();adapter.close();
    check(adapter.send(CloseEvent{})==Delivery::ignored,"Closed adapter dispatched input");
    rejects([&]{adapter.present(s);});
}
void retention_and_bitmap() {
    std::vector<Event> events;MemoryAdapter adapter([&](const Event& e){events.push_back(e);});
    auto s=fixture();adapter.present(s);
    adapter.text_selection({"editor",1},{2,2});s.widgets[1].state.text="\xc3\xa9";adapter.present(s);
    check(adapter.text_selection({"editor",1}).caret==2,"Editor caret was lost");
    s.widgets[1].state.text="a";adapter.present(s);
    check(adapter.text_selection({"editor",1}).caret==1,"Editor caret did not clamp");
    check(adapter.scroll_offset({"list",1}).y==20,"Initial tail position failed");
    adapter.scroll({"list",1},{0,0});s.widgets[2].state.records.push_back({"d","D",{},true,true});adapter.present(s);
    check(adapter.scroll_offset({"list",1}).y==0,"Append moved history reader");
    adapter.scroll({"list",1},{0,40});s.widgets[2].state.records.push_back({"e","E",{},true,true});adapter.present(s);
    check(adapter.scroll_offset({"list",1}).y==60,"Append did not follow tail");
    adapter.list_key({"list",1},ListKey::space);
    check(adapter.scroll_offset({"list",1}).y==0,"Space did not reveal the current row");
    adapter.scroll({"list",1},{0,60});adapter.list_key({"list",1},ListKey::enter);
    check(adapter.scroll_offset({"list",1}).y==0&&
          std::holds_alternative<ActivateRecord>(std::get<WidgetEvent>(events.back()).input),"Enter did not reveal and activate the current row");
    adapter.list_key({"list",1},ListKey::down);
    check(std::get<SelectRecord>(std::get<WidgetEvent>(events.back()).input).id=="c","Keyboard selection did not skip disabled row");
    check(adapter.repaint({"image",1})&&adapter.image({"image",1}).pixels()[0]==255,"Bitmap was not presented");
    check(!adapter.repaint({"image",1}),"Unchanged image repainted");
    s.display_scale=2;adapter.present(s);check(adapter.repaint({"image",1})&&adapter.image({"image",1}).width()==16,"Display scale did not resize grid");
    s.widgets[3].state.bitmap.revision=1;s.widgets[3].state.bitmap.source=solid_bitmap(0,255,0);adapter.present(s);adapter.repaint({"image",1});
    check(adapter.image({"image",1}).pixels()[1]==255,"New bitmap revision was ignored");
    adapter.invalidate({"image",1},{0,0,1,1});check(adapter.repaint({"image",1}),"Explicit damage did not repaint");
    MemoryAdapter tiny({},10);rejects([&]{tiny.present(s);});
}
void synchronous_text_acknowledgement() {
    MemoryAdapter adapter([&](const Event& event) {
        const auto& edit=std::get<EditText>(std::get<WidgetEvent>(event).input);
        auto next=adapter.snapshot();next.widgets[1].state.text=edit.value;adapter.present(std::move(next));
    });
    adapter.present(fixture());adapter.text_selection({"editor",1},{0,2});
    // The supplied key is borrowed from storage replaced inside the callback.
    check(adapter.replace(adapter.snapshot().widgets[1].spec.key,"new")==Delivery::delivered,
          "Synchronous edit was not delivered");
    check(adapter.text_selection({"editor",1}).caret==3&&adapter.snapshot().widgets[1].state.text=="new",
          "Accepted synchronous text lost its selection");
}
void group_scroll_contract() {
    int events=0;MemoryAdapter adapter([&](const Event&){++events;});
    Snapshot view;
    auto outer=widget("outer",Kind::group,{10,20,100,100});outer.state.content_size={300,300};
    auto inner=widget("inner",Kind::group,{30,140,80,80});inner.spec.parent="outer";inner.state.content_size={300,300};
    auto area=widget("area",Kind::bitmap,{40,170,30,30});area.spec.parent="inner";area.spec.pointer_input=true;
    auto choice=widget("nested-choice",Kind::choice,{40,170,30,30});choice.spec.parent="inner";
    choice.state.options={{"a","A","",true}};
    view.widgets={outer,inner,area,choice};adapter.present(view);
    check(!adapter.resolved_availability(area.spec.key).visible,"Unscrolled clipped child was visible");
    adapter.scroll(outer.spec.key,{5,100});adapter.scroll(inner.spec.key,{5,20});
    const auto resolved=adapter.resolved_availability(area.spec.key);
    check(resolved.bounds==Rect{30,50,30,30}&&resolved.visible&&resolved.enabled,"Nested scroll translation failed");
    check(adapter.resolved_availability(outer.spec.key).bounds==outer.state.bounds,"Scrolling moved the group frame");
    check(adapter.resolved_availability(inner.spec.key).bounds==Rect{25,40,80,80},"Parent scroll did not move nested group frame");
    check(pixel_at(resolved.bounds,60,60,{45,65})==Point{30,30},"Scrolled bitmap point mapping failed");
    check(adapter.focus(area.spec.key)&&adapter.open_popup(choice.spec.key),"Revealed child did not accept focus or popup");
    check(adapter.send(WidgetEvent{area.spec.key,PointerInput{PointerKind::click,{31,51}}})==Delivery::delivered,
        "Translated pointer input was rejected");
    check(adapter.send(WidgetEvent{area.spec.key,PointerInput{PointerKind::click,{41,171}}})==Delivery::ignored,
        "Unscrolled pointer coordinates remained active");
    adapter.present(view);check(adapter.focused()==area.spec.key,"Presentation discarded scrolled focus");
    adapter.scroll(outer.spec.key,{0,0});
    check(!adapter.focused()&&!adapter.resolved_availability(area.spec.key).visible,"Scrolling out retained focus or visibility");
    check(adapter.choose_popup(choice.spec.key,0)==Delivery::ignored&&events==1,"Scrolling out retained popup or emitted input");

    Snapshot fractional;
    auto group=widget("group",Kind::group,{0,0,10,10});group.state.content_size={20,20};
    auto image=widget("small",Kind::bitmap,{0.6,0.6,1.25,1.25});image.spec.parent="group";image.spec.pointer_input=true;
    image.state.bitmap={"fill",1,solid_bitmap(50,60,70)};
    fractional.widgets={group,image};MemoryAdapter pixels({},12);pixels.present(fractional);
    check(pixels.image(image.spec.key).width()==1&&pixels.image(image.spec.key).height()==1,"Initial snapped bitmap grid was wrong");
    pixels.scroll(group.spec.key,{0.2,0.2});
    check(pixels.image(image.spec.key).width()==2&&pixels.image(image.spec.key).height()==2,"Fractional scroll did not resize the bitmap grid");
    pixels.present(fractional);
    check(pixels.image(image.spec.key).width()==2&&pixels.repaint(image.spec.key),"Presentation lost the resolved bitmap grid");
    auto extra=image;extra.spec.key.id="extra";fractional.widgets.push_back(extra);
    rejects([&]{pixels.present(fractional);});
    check(pixels.snapshot().widgets.size()==2&&pixels.image(image.spec.key).width()==2,
        "Resolved aggregate budget failure partially applied a snapshot");
    fractional.widgets.pop_back();MemoryAdapter limited({},3);limited.present(fractional);limited.repaint(image.spec.key);
    check(limited.focus(image.spec.key),"Small bitmap could not receive focus");
    rejects([&]{limited.scroll(group.spec.key,{0.2,0.2});});
    check(limited.scroll_offset(group.spec.key)==Point{}&&limited.image(image.spec.key).width()==1&&
          limited.image(image.spec.key).pixels()[0]==50&&limited.focused()==image.spec.key,
        "Failed fractional allocation changed scroll, pixels or focus");

    Snapshot distant;
    auto first=widget("first",Kind::group,{0,0,1,1});first.state.content_size={coordinate_limit,coordinate_limit};
    auto second=widget("second",Kind::group,{0,0,1,1});second.spec.parent="first";second.state.content_size=first.state.content_size;
    auto last=widget("last",Kind::bitmap,{0,0,1,1});last.spec.parent="second";
    distant.widgets={first,second,last};MemoryAdapter bounds;bounds.present(distant);
    bounds.scroll(first.spec.key,{coordinate_limit-1,0});
    rejects([&]{bounds.scroll(second.spec.key,{coordinate_limit-1,0});});
    check(bounds.scroll_offset(second.spec.key)==Point{}&&bounds.image(last.spec.key).width()==1,
        "Excessive accumulated coordinates partially committed a scroll");
}
void bitmap_callback_lifetime() {
    unsigned events=0;MemoryAdapter adapter([&](const Event&){++events;});
    auto view=fixture();
    auto group=widget("viewport",Kind::group,{0,0,300,300});group.state.content_size={600,600};
    view.widgets[3].spec.parent="viewport";
    view.widgets.insert(view.widgets.begin(),group);
    adapter.present(view);adapter.repaint({"image",1});
    const auto before=adapter.image({"image",1}).pixels();
    const std::vector<std::function<void()>> changes{
        [&]{adapter.present(view);}, [&]{adapter.close();},
        [&]{adapter.scroll(group.spec.key,{1,1});}, [&]{adapter.focus(std::nullopt);},
        [&]{adapter.focus_next();}, [&]{adapter.text_selection({"editor",1},{0,1});},
        [&]{adapter.replace({"editor",1},"x");}, [&]{adapter.enter({"editor",1});},
        [&]{adapter.open_popup({"choice",1});}, [&]{adapter.choose_popup({"choice",1},0);},
        [&]{adapter.close_popup({"choice",1});}, [&]{adapter.list_key({"list",1},ListKey::down);},
        [&]{adapter.send(CloseEvent{});}, [&]{adapter.invalidate({"image",1},{0,0,1,1});},
        [&]{adapter.repaint({"image",1});}
    };
    std::uint64_t revision=0;
    for(const auto& change:changes) {
        view.widgets[4].state.bitmap={"guarded",++revision,
            BitmapSource([&](const BitmapRequest& request,const BitmapSink& sink) {
                // Read-only access remains safe while the surface uses staging.
                check(adapter.image({"image",1}).pixels()==before&&!adapter.closed(),
                    "Read-only access during paint observed uncommitted state");
                check(adapter.resolved_availability({"image",1}).visible,
                    "Read-only geometry during paint was unavailable");
                solid_bitmap(10,20,30).paint(request,sink);
                change();
            })};
        adapter.present(view);
        for(unsigned attempt=0;attempt<2;++attempt) {
            bool guarded=false;
            try {adapter.repaint({"image",1});}catch(const std::logic_error&){guarded=true;}
            check(guarded,"Producer mutated its owning adapter during paint");
            check(adapter.image({"image",1}).pixels()==before&&!adapter.closed()&&
                  adapter.scroll_offset(group.spec.key)==Point{}&&events==0,
                  "Rejected producer mutation changed pixels, lifecycle, scroll or events");
        }
    }
    view.widgets[4].state.bitmap={"recovered",1,solid_bitmap(10,20,30)};
    adapter.present(view);
    check(adapter.repaint({"image",1})&&adapter.image({"image",1}).pixels()[0]==10,
          "Paint guard was not cleared after producer failure");
    check(adapter.send(CloseEvent{})==Delivery::delivered&&events==1,
          "Paint guard blocked later event delivery");
    adapter.close();check(adapter.closed(),"Paint guard blocked later close");
}
void shared_event_normalization() {
    auto view=fixture();view.widgets[2].spec.activate_on_select=true;
    auto normalize=[&](Event event){return normalize_event(view,event);};
    Event selection=WidgetEvent{{"list",1},SelectRecord{"a"}};
    check(normalize_event(view,selection)&&
          std::get<ActivateRecord>(std::get<WidgetEvent>(selection).input).id=="a",
          "Shared policy did not combine declared list selection and activation");
    check(normalize_event(view,selection),"Already normalized activation failed a repeated authoritative check");
    check(!normalize(WidgetEvent{{"list",1},SelectRecord{"b"}})&&
          !normalize(WidgetEvent{{"list",1},ActivateRecord{"missing"}}),"Ineligible shared row input was accepted");
    view.widgets[2].state.records[1].enabled=true;
    selection=WidgetEvent{{"list",1},SelectRecord{"b"}};
    check(normalize_event(view,selection)&&std::holds_alternative<SelectRecord>(std::get<WidgetEvent>(selection).input),
          "Nonactivatable shared row was converted to activation");
    view.widgets[1].spec.text_policy.read_only=true;
    view.widgets[1].state.options={{"empty","Clear","",true}};
    check(!normalize(WidgetEvent{{"editor",1},EditText{"new","ab"}})&&
          !normalize(WidgetEvent{{"editor",1},ChooseOption{"empty"}})&&
          !normalize(WidgetEvent{{"editor",1},SubmitText{}}),"Read-only shared editor accepted mutation");
    view.widgets[1].spec.text_policy.read_only=false;
    check(normalize(WidgetEvent{{"editor",1},ChooseOption{"empty"}}),"Valid empty suggestion was rejected");
    check(!normalize(WidgetEvent{{"editor",1},EditText{"new","stale"}})&&
          !normalize(WidgetEvent{{"editor",1},EditText{"\xc0\x80","ab"}})&&
          !normalize(WidgetEvent{{"button",2},Activate{}}),"Shared policy accepted invalid text or a stale generation");
    check(normalize(PageEvent{"two"})&&!normalize(PageEvent{"one"})&&!normalize(PageEvent{"missing"}),
          "Shared page eligibility failed");
    check(normalize(ResizeEvent{{0,0},16})&&!normalize(ResizeEvent{{-1,1},1})&&
          !normalize(ResizeEvent{{1,1},17})&&normalize(CloseEvent{}),"Shared lifecycle input validation failed");
    auto group=widget("group",Kind::group,{100,0,30,30});group.state.content_size={30,100};
    view.widgets[3].spec.parent="group";view.widgets[3].state.bounds={110,60,8,8};
    view.widgets.insert(view.widgets.begin(),group);validate_snapshot(view);
    Event pointer=WidgetEvent{{"image",1},PointerInput{PointerKind::click,{111,11}}};
    const ScrollLookup scrolled=[](const WidgetKey&){return Point{0,50};};
    check(!normalize_event(view,pointer)&&normalize_event(view,pointer,scrolled),
          "Shared input normalization did not apply retained group scrolling");
    view.widgets[0].state.enabled=false;
    check(!normalize_event(view,pointer,scrolled),"Authoritative ancestor disablement was bypassed");
}
}
int main() {
    try {text_contract();geometry_contract();events_and_identity();retention_and_bitmap();
        synchronous_text_acknowledgement();group_scroll_contract();bitmap_callback_lifetime();shared_event_normalization();
        std::cout<<"Widget contract checks passed\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
