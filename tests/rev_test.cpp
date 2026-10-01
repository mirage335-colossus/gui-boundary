#include "adapter.hpp"
#include "probe.hpp"
#include "application.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using gui::rev::Adapter;
using gui::rev::Probe;
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void settle(Adapter& adapter) {adapter.sync();adapter.pump();adapter.sync();check(adapter.error().empty(),adapter.error().c_str());}
template<class Predicate> void await_native(Adapter& adapter,Predicate ready,const char* message) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    do {settle(adapter);if(ready())return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    while(std::chrono::steady_clock::now()<deadline);
    check(false,message);
}
void write_capture(const gui::BitmapImage& image,const std::string& path) {
    check(image.width()>0&&image.height()>0&&image.format()==gui::PixelFormat::rgb24,"Rev capture must contain an RGB window image");
    if(path.empty())return;
    std::ofstream file(path,std::ios::binary);
    file<<"P6\n"<<image.width()<<' '<<image.height()<<"\n255\n";
    file.write(reinterpret_cast<const char*>(image.pixels().data()),static_cast<std::streamsize>(image.pixels().size()));
    check(bool(file),"Could not write Rev screenshot");
}
gui::Color pixel(const gui::BitmapImage& image,unsigned x,unsigned y) {
    check(x<image.width()&&y<image.height(),"Capture pixel outside the native window");
    const auto at=(std::size_t(y)*image.width()+x)*3;
    return {image.pixels()[at],image.pixels()[at+1],image.pixels()[at+2]};
}

// These opaque keys deliberately share no identities with the demonstration.
// Events publish authoritative values through the same public boundary an
// independent application uses. Probe inputs enter native callback paths.
struct Fixture {
    gui::Snapshot view;
    std::vector<gui::Event> events;
    std::function<void(const gui::Event&)> after_input;
    Adapter adapter;
    Fixture():adapter([this](const gui::Event& event) {
        events.push_back(event);
        if(const auto* input=std::get_if<gui::WidgetEvent>(&event)) {
            auto& current=widget(input->target.id);
            if(const auto* text=std::get_if<gui::EditText>(&input->input))current.state.text=text->value;
            if(const auto* toggle=std::get_if<gui::SetChecked>(&input->input))current.state.checked=toggle->value;
            if(const auto* option=std::get_if<gui::ChooseOption>(&input->input)) {
                if(current.spec.kind==gui::Kind::choice)current.state.selected=option->id;
                if(current.spec.kind==gui::Kind::text)
                    for(const auto& item:current.state.options)if(item.id==option->id)current.state.text=item.value;
            }
            if(const auto* row=std::get_if<gui::SelectRecord>(&input->input))current.state.selected=row->id;
            if(const auto* row=std::get_if<gui::ActivateRecord>(&input->input))current.state.selected=row->id;
        } else if(const auto* page=std::get_if<gui::PageEvent>(&event))view.active_page=page->id;
        else if(const auto* resize=std::get_if<gui::ResizeEvent>(&event)) {
            view.client_size=resize->client_size;view.display_scale=resize->display_scale;
        }
        publish();
        if(after_input)after_input(event);
    }) {
        view.title="Independent Rev declarations";view.client_size={640,480};
        view.pages={{"page-47","Primary"},{"page-93","Secondary"}};view.active_page="page-47";
        view.page_bar={16,440,608,24};
        auto& group=add("group-70",gui::Kind::group,{12,12,190,170});
        group.state.content_clip=gui::Rect{8,8,140,135};group.state.content_size={190,360};
        add("action-53",gui::Kind::button,{24,24,128,28},"group-70").state.label="A&B @ action";
        add("label-21",gui::Kind::label,{24,60,128,28},"group-70").state.text="Literal label";
        auto& bitmap=add("pixels-38",gui::Kind::bitmap,{120,110,100,60},"group-70");
        bitmap.state.bitmap={"source-90",1,gui::solid_bitmap(17,99,203)};
        bitmap.state.actions={{"paint-96","Repaint","",true}};bitmap.spec.pointer_input=true;
        auto& editor=add("edit-61",gui::Kind::text,{230,16,370,84});
        editor.spec.text_policy={true,false,4096,gui::SubmitKey::control_enter};
        for(int i=0;i<12;++i){if(i)editor.state.text+='\n';editor.state.text+=std::string(90,'W');}
        auto& single=add("entry-12",gui::Kind::text,{230,112,270,28});
        single.spec.text_policy={false,false,24,gui::SubmitKey::enter};single.state.placeholder="Type here";
        single.state.options={{"suggest-24","A suggestion","AéΩ",true}};
        auto& choice=add("choice-33",gui::Kind::choice,{230,154,270,28});
        choice.state.options={{"option-44","A&B / @ literal","",true},{"option-81","Other","",true},{"option-07","Disabled","",false}};
        choice.state.selected="option-44";
        auto& menu=add("menu-49",gui::Kind::menu,{230,190,270,28});menu.state.label="Commands";
        menu.state.options={{"command-76","Do something","",true}};
        add("toggle-85",gui::Kind::toggle,{230,226,270,28}).state.label="A switch";
        auto& list=add("rows-52",gui::Kind::list,{230,262,270,92});list.spec.row_height=28;
        list.state.records={{"record-19","First",{{"First",{4,0,230,28},{}}},true,true},
                            {"record-73","Second",{{"Second",{4,0,230,28},{14,false,gui::Tone::error}}},true,true},
                            {"record-06","Disabled",{{"Disabled",{4,0,230,28},{}}},false,true}};
        auto& pointer=add("pointer-68",gui::Kind::label,{230,366,270,24});pointer.state.text="Pointer-enabled label";pointer.spec.pointer_input=true;
        auto& modal=add("modal-04",gui::Kind::group,{80,70,440,300});modal.state.visible=false;
        add("dismiss-29",gui::Kind::button,{340,280,140,30},"modal-04").state.label="Dismiss";
        view.key_bindings.push_back({gui::ShortcutKey::escape,{"dismiss-29",1}});
        publish();adapter.show();settle(adapter);
    }
    gui::Widget& add(std::string id,gui::Kind kind,gui::Rect bounds,std::string parent={}) {
        gui::Widget value;value.spec.key={std::move(id),1};value.spec.kind=kind;
        value.spec.parent=std::move(parent);value.spec.page="page-47";value.state.bounds=bounds;
        view.widgets.push_back(std::move(value));return view.widgets.back();
    }
    gui::Widget& widget(std::string_view id) {
        for(auto& value:view.widgets)if(value.spec.key.id==id)return value;
        throw std::runtime_error("Fixture widget not found");
    }
    void publish() {++view.revision;adapter.present(view);}
    std::size_t inputs() const {
        return std::count_if(events.begin(),events.end(),[](const auto& event){return std::holds_alternative<gui::WidgetEvent>(event)||std::holds_alternative<gui::PageEvent>(event);});
    }
    const gui::WidgetEvent& last_input() const {
        for(auto it=events.rbegin();it!=events.rend();++it)if(const auto* value=std::get_if<gui::WidgetEvent>(&*it))return *value;
        throw std::runtime_error("No widget input received");
    }
};

void declarations_and_pixels() {
    Fixture fixture;auto& adapter=fixture.adapter;
    for(const auto& widget:fixture.view.widgets) {
        check(Probe::exists(adapter,widget.spec.key),"A declared kind has no native representation");
        check(Probe::bounds(adapter,widget.spec.key)==widget.state.bounds,"Rev changed shared widget geometry");
        const auto expected=adapter.resolved_availability(widget.spec.key);
        check(Probe::clip(adapter,widget.spec.key)==expected.clip,"Rev clipping differs from the shared resolved clip");
        check(Probe::visible(adapter,widget.spec.key)==expected.visible,"Rev ignored declared visibility/page scope");
    }
    const auto capture=adapter.capture();
    const auto physical=[&](gui::Point logical) {
        return gui::Point{std::floor(logical.x*capture.width()/fixture.view.client_size.width),
                          std::floor(logical.y*capture.height()/fixture.view.client_size.height)};
    };
    const auto background=physical({630,420});
    check(pixel(capture,unsigned(background.x),unsigned(background.y))==fixture.view.palette.background,"Native root background ignored the shared palette");
    check(pixel(capture,135,125)==gui::Color{17,99,203},"Rev did not upload/draw the declared bitmap texture");
    check(pixel(capture,175,125)!=gui::Color{17,99,203},"Rev bitmap escaped its group content clip");
    fixture.widget("label-21").state.font.bold=true;fixture.publish();settle(adapter);
    const auto bold=adapter.capture();const auto first=physical({24,60}),last=physical({152,88});
    std::size_t changed_glyph_pixels=0;
    for(unsigned y=unsigned(first.y);y<unsigned(last.y);++y)
        for(unsigned x=unsigned(first.x);x<unsigned(last.x);++x)
            if(pixel(capture,x,y)!=pixel(bold,x,y))++changed_glyph_pixels;
    check(changed_glyph_pixels>10,"Native label ignored the shared bold font declaration");
    fixture.widget("pixels-38").state.bitmap={"source-90",2,gui::solid_bitmap(111,57,23)};
    fixture.publish();settle(adapter);
    check(pixel(adapter.capture(),135,125)==gui::Color{111,57,23},"Changed source revision did not replace the native texture");
    const auto before=fixture.inputs();
    Probe::pointer(adapter,{135,125});settle(adapter);
    check(fixture.inputs()==before+1&&fixture.last_input().target.id=="pixels-38","Native pointer missed the bitmap target");
    check(std::holds_alternative<gui::PointerInput>(fixture.last_input().input),"Bitmap pointer lost its public event kind");
    Probe::wheel(adapter,{135,125},{0,-1});settle(adapter);
    const auto* wheel=std::get_if<gui::PointerInput>(&fixture.last_input().input);
    check(wheel&&wheel->kind==gui::PointerKind::wheel&&wheel->wheel_y==-1,"Native raw wheel event lost its logical detent units");
    Probe::pointer(adapter,{245,374});settle(adapter);
    check(fixture.last_input().target.id=="pointer-68","Pointer opt-in was restricted to bitmap controls");
    adapter.focus(gui::WidgetKey{"pixels-38",1});Probe::key(adapter,"enter");Probe::key(adapter,"enter");settle(adapter);
    const auto* action=std::get_if<gui::InvokeAction>(&fixture.last_input().input);
    check(action&&action->id=="paint-96","Native bitmap actions were not reachable with the activation key");
    Probe::wheel(adapter,{40,35},{0,-1});settle(adapter);
    check(adapter.scroll_offset({"group-70",1}).y==32,"Native wheel did not scroll the control's ancestor by one detent");
    adapter.scroll({"group-70",1},{0,40});settle(adapter);
    check(Probe::bounds(adapter,{"action-53",1}).y==-16,"Group scroll did not translate native descendants");
    check(Probe::clip(adapter,{"action-53",1})==adapter.resolved_availability({"action-53",1}).clip,"Scrolled native control ignored its resolved clip");
}

void retained_editing_and_options() {
    Fixture fixture;auto& adapter=fixture.adapter;
    adapter.focus(gui::WidgetKey{"entry-12",1});
    Probe::edit_text(adapter,{"entry-12",1},"AéΩ",{1,3});settle(adapter);
    check(fixture.widget("entry-12").state.text=="AéΩ","Native Unicode edit did not reach the independent application");
    adapter.text_selection({"entry-12",1},{1,3});settle(adapter);
    check(Probe::selection(adapter,{"entry-12",1})==gui::TextSelection{1,3},"Native Unicode selection did not preserve UTF-8 boundaries");
    const auto before=fixture.inputs();fixture.widget("label-21").state.text="Changed elsewhere";fixture.publish();settle(adapter);
    check(fixture.inputs()==before,"Programmatic publication echoed application input");
    check(adapter.focused()==gui::WidgetKey{"entry-12",1},"Unrelated publication lost editor focus");
    check(Probe::selection(adapter,{"entry-12",1})==gui::TextSelection{1,3},"Unrelated publication lost native selection");
    Probe::edit_text(adapter,{"entry-12",1},"bad\nline");settle(adapter);
    check(fixture.inputs()==before&&Probe::text(adapter,{"entry-12",1})=="AéΩ","Rejected native edit left model and control divergent");
    Probe::edit_text(adapter,{"entry-12",1},std::string(25,'x'));settle(adapter);
    check(fixture.inputs()==before&&Probe::text(adapter,{"entry-12",1})=="AéΩ","Native edit bypassed the declared byte limit");
    adapter.scroll({"edit-61",1},{90,30});settle(adapter);
    const auto scrolled=adapter.scroll_offset({"edit-61",1});
    check(scrolled.x>0&&scrolled.y>0,"Native editor did not retain both scroll axes");
    fixture.widget("label-21").state.text="Changed again";fixture.publish();settle(adapter);
    check(adapter.scroll_offset({"edit-61",1})==scrolled,"Publication reset native editor scrolling");
    Probe::set_checked(adapter,{"toggle-85",1},true);settle(adapter);
    check(fixture.widget("toggle-85").state.checked,"Native toggle callback lost its value");
    Probe::select_option(adapter,{"choice-33",1},"option-81");settle(adapter);
    check(fixture.widget("choice-33").state.selected=="option-81","Native choice lost stable option identity");
    adapter.focus(gui::WidgetKey{"choice-33",1});Probe::key(adapter,"enter");Probe::key(adapter,"enter");settle(adapter);
    check(fixture.widget("choice-33").state.selected=="option-81","Opening a native choice reset its current option highlight");
    const auto selected=fixture.inputs();Probe::select_option(adapter,{"choice-33",1},"option-07");settle(adapter);
    check(fixture.inputs()==selected,"Native choice accepted a disabled option");
    adapter.open_popup({"choice-33",1});fixture.widget("choice-33").state.options[0].enabled=false;
    fixture.publish();settle(adapter);Probe::select_option(adapter,{"choice-33",1},"option-44");settle(adapter);
    check(fixture.inputs()==selected,"Open native popup accepted an option disabled by newer state");
    Probe::select_option(adapter,{"menu-49",1},"command-76");settle(adapter);
    check(std::get<gui::ChooseOption>(fixture.last_input().input).id=="command-76","Native command menu did not emit its opaque option ID");
    fixture.after_input=[&](const gui::Event& event) {
        const auto* input=std::get_if<gui::WidgetEvent>(&event);
        if(input&&input->target.id=="choice-33"&&std::holds_alternative<gui::ChooseOption>(input->input))
            adapter.open_popup({"menu-49",1});
    };
    Probe::select_option(adapter,{"choice-33",1},"option-81");settle(adapter);
    Probe::key(adapter,"enter");settle(adapter);
    check(fixture.last_input().target.id=="menu-49"&&std::holds_alternative<gui::ChooseOption>(fixture.last_input().input),"Completing an old popup closed the next popup requested by its handler");
    fixture.after_input={};
    Probe::select_record(adapter,{"rows-52",1},"record-73");settle(adapter);
    check(fixture.widget("rows-52").state.selected=="record-73","Native list did not select by record identity");
    std::reverse(fixture.widget("rows-52").state.records.begin(),fixture.widget("rows-52").state.records.end());fixture.publish();settle(adapter);
    Probe::select_record(adapter,{"rows-52",1},"record-73",true);settle(adapter);
    check(std::get<gui::ActivateRecord>(fixture.last_input().input).id=="record-73","Reordered native list activated the wrong record");
    const auto activated=fixture.inputs();Probe::select_record(adapter,{"rows-52",1},"record-06",true);settle(adapter);
    check(fixture.inputs()==activated,"Native list accepted a disabled record");
}

void modal_pages_and_lifetimes() {
    Fixture fixture;auto& adapter=fixture.adapter;
    auto old_callback=Probe::callback_for(adapter,{"action-53",1});
    old_callback();settle(adapter);check(fixture.last_input().target==gui::WidgetKey{"action-53",1},"Captured native callback failed before replacement");
    fixture.widget("action-53").spec.key.generation=2;fixture.publish();settle(adapter);
    const auto replaced=fixture.inputs();old_callback();settle(adapter);
    check(fixture.inputs()==replaced,"Retired native callback reached a replacement generation");
    Probe::activate(adapter,{"action-53",2});settle(adapter);
    check(fixture.inputs()==replaced+1&&fixture.last_input().target.generation==2,"Replacement control lost its callback identity");
    fixture.add("added-42",gui::Kind::button,{520,300,100,28}).state.label="Added later";
    fixture.publish();settle(adapter);Probe::activate(adapter,{"added-42",1});settle(adapter);
    check(fixture.last_input().target==gui::WidgetKey{"added-42",1},"A newly declared feature needed a native adapter change");
    adapter.focus(gui::WidgetKey{"entry-12",1});Probe::page(adapter,"page-93");settle(adapter);
    check(fixture.view.active_page=="page-93"&&!Probe::visible(adapter,{"entry-12",1}),"Native page change did not hide its widgets");
    const auto hidden=fixture.inputs();Probe::activate(adapter,{"action-53",2});settle(adapter);
    check(fixture.inputs()==hidden,"Hidden native page accepted input");
    Probe::page(adapter,"page-47");settle(adapter);
    fixture.widget("modal-04").state.visible=true;fixture.view.modal_root=gui::WidgetKey{"modal-04",1};fixture.publish();settle(adapter);
    check(!Probe::enabled(adapter,{"action-53",2})&&Probe::enabled(adapter,{"dismiss-29",1}),"Native modal scope did not disable background input");
    const auto modal=fixture.inputs();Probe::activate(adapter,{"action-53",2});Probe::page(adapter,"page-93");settle(adapter);
    check(fixture.inputs()==modal&&fixture.view.active_page=="page-47","Native background/page callback escaped modal isolation");
    Probe::key(adapter,"escape");settle(adapter);
    check(fixture.inputs()==modal+1&&fixture.last_input().target.id=="dismiss-29","Native key did not resolve the shared dismissal shortcut");
    fixture.view.modal_root.reset();fixture.widget("modal-04").state.visible=false;fixture.publish();settle(adapter);
    adapter.open_popup({"choice-33",1});
    fixture.view.widgets.erase(std::remove_if(fixture.view.widgets.begin(),fixture.view.widgets.end(),[](const auto& widget){return widget.spec.key.id=="choice-33";}),fixture.view.widgets.end());
    fixture.publish();settle(adapter);check(!Probe::exists(adapter,{"choice-33",1}),"Removed popup owner retained a native control");
    auto close_callback=Probe::callback_for(adapter,{"action-53",2});const auto closing=fixture.inputs();
    adapter.close();settle(adapter);close_callback();check(fixture.inputs()==closing,"Closed adapter accepted a retained native callback");
}

void native_clipboard() {
    Fixture fixture;auto& adapter=fixture.adapter;const gui::WidgetKey key{"entry-12",1};
    adapter.focus(key);Probe::edit_text(adapter,key,"AéΩ");settle(adapter);
    adapter.text_selection(key,{1,3});settle(adapter);
    const auto copied=fixture.inputs();Probe::key(adapter,"c",true);settle(adapter);
    check(fixture.inputs()==copied&&Probe::text(adapter,key)=="AéΩ","Copy changed the native editor");
    adapter.text_selection(key,{0,5});settle(adapter);Probe::key(adapter,"v",true);
    await_native(adapter,[&]{return fixture.widget(key.id).state.text=="é";},"Native copy/paste did not preserve the selected Unicode scalar");
    Probe::edit_text(adapter,key,"AéΩ");adapter.text_selection(key,{1,3});settle(adapter);
    Probe::key(adapter,"x",true);settle(adapter);
    check(fixture.widget(key.id).state.text=="AΩ","Native cut did not remove exactly the selected UTF-8 bytes");
    adapter.text_selection(key,{0,0});Probe::key(adapter,"v",true);
    await_native(adapter,[&]{return fixture.widget(key.id).state.text=="éAΩ";},"Native cut did not publish its selection to the clipboard");
    std::optional<gui::ServiceResult> written;
    check(adapter.service({800,gui::ServiceKind::clipboard_write,"Copy text","External Ω",0},[&](auto result){written=std::move(result);}),"Native clipboard write request was not accepted");
    settle(adapter);check(written&&written->status==gui::ServiceStatus::success,"Native clipboard write service failed");
    adapter.text_selection(key,{0,fixture.widget(key.id).state.text.size()});Probe::key(adapter,"v",true);
    await_native(adapter,[&]{return fixture.widget(key.id).state.text=="External Ω";},"Clipboard service and editor paste use different native clipboard state");

    // Hold only transport completion. Input still enters the native shortcut
    // path and uses the production completion translator and captured tokens.
    Probe::hold_clipboard_read(adapter,true);
    adapter.text_selection(key,{0,9});settle(adapter);Probe::key(adapter,"v",true);
    const auto failed=fixture.inputs();Probe::complete_clipboard_read(adapter,std::nullopt,"No text format");settle(adapter);
    check(fixture.inputs()==failed&&Probe::text(adapter,key)=="External Ω","Failed clipboard read deleted the selection");
    check(Probe::selection(adapter,key)==gui::TextSelection{0,9},"Failed clipboard read changed native selection");
    Probe::key(adapter,"v",true);Probe::complete_clipboard_read(adapter,std::string{});settle(adapter);
    check(fixture.widget(key.id).state.text=="Ω","Valid empty clipboard text was confused with read failure");

    Probe::edit_text(adapter,key,"AéΩ");adapter.text_selection(key,{0,5});settle(adapter);
    Probe::key(adapter,"v",true);adapter.focus(gui::WidgetKey{"edit-61",1});settle(adapter);
    const auto refocused=fixture.inputs();Probe::complete_clipboard_read(adapter,std::string("late"));settle(adapter);
    check(fixture.inputs()==refocused&&Probe::text(adapter,key)=="AéΩ","Late paste ignored a focus change");
    adapter.focus(key);adapter.text_selection(key,{0,5});settle(adapter);Probe::key(adapter,"v",true);
    fixture.widget(key.id).state.text="New value";fixture.publish();settle(adapter);
    const auto rebased=fixture.inputs();Probe::complete_clipboard_read(adapter,std::string("late"));settle(adapter);
    check(fixture.inputs()==rebased&&Probe::text(adapter,key)=="New value","Late paste overwrote newer authoritative text");
    adapter.text_selection(key,{0,3});settle(adapter);Probe::key(adapter,"v",true);
    adapter.text_selection(key,{4,9});settle(adapter);
    const auto reselection=fixture.inputs();Probe::complete_clipboard_read(adapter,std::string("late"));settle(adapter);
    check(fixture.inputs()==reselection&&Probe::text(adapter,key)=="New value","Late paste ignored a newer selection");
    Probe::key(adapter,"v",true);fixture.widget(key.id).spec.key.generation=2;fixture.widget(key.id).state.text="Replacement";
    fixture.publish();adapter.focus(gui::WidgetKey{key.id,2});settle(adapter);
    const auto retired=fixture.inputs();Probe::complete_clipboard_read(adapter,std::string("late"));settle(adapter);
    check(fixture.inputs()==retired&&Probe::text(adapter,{key.id,2})=="Replacement","Late paste entered a replacement widget generation");

    fixture.widget(key.id).spec.key.generation=3;fixture.widget(key.id).spec.text_policy.read_only=true;fixture.publish();
    adapter.focus(gui::WidgetKey{key.id,3});adapter.text_selection({key.id,3},{0,11});settle(adapter);
    const auto read_only=fixture.inputs();Probe::key(adapter,"x",true);Probe::key(adapter,"v",true);
    Probe::complete_clipboard_read(adapter,std::string("forbidden"));settle(adapter);
    check(fixture.inputs()==read_only&&Probe::text(adapter,{key.id,3})=="Replacement","Read-only native editor accepted clipboard mutation");
    Probe::hold_clipboard_read(adapter,false);
}

void prompt_from_native_event_batch() {
    gui::Snapshot view;view.title="Batched native service intake";
    gui::Widget editor;editor.spec.key={"entry-44",1};editor.spec.kind=gui::Kind::text;
    editor.spec.text_policy={false,false,64,gui::SubmitKey::enter};editor.state.bounds={24,24,300,28};editor.state.text="Original";
    view.widgets.push_back(editor);
    Adapter* port=nullptr;std::size_t submitted=0;std::vector<gui::ServiceResult> results;
    Adapter adapter([&](const gui::Event& event) {
        if(const auto* input=std::get_if<gui::WidgetEvent>(&event)) {
            if(std::holds_alternative<gui::SubmitText>(input->input)) {
                ++submitted;
                check(port->service({900,gui::ServiceKind::prompt,"Opened from callback","",32},[&](auto result){results.push_back(std::move(result));}),"Service opened from native callback was rejected");
            } else throw std::runtime_error("Pending native prompt leaked a background input");
        }
    });
    port=&adapter;adapter.present(view);adapter.show();adapter.focus(editor.spec.key);settle(adapter);
    Probe::batch(adapter,[&] {
        Probe::key(adapter,"enter");
        // These keys arrive before deferred native controls can be created.
        // They must not dereference pending prompt controls or reach the editor.
        Probe::key(adapter,"tab");Probe::key(adapter,"enter");
        Probe::edit_text(adapter,editor.spec.key,"Leaked");
    });
    settle(adapter);
    check(submitted==1&&results.empty()&&adapter.service_active(),"Batched native input escaped pending prompt isolation");
    check(Probe::text(adapter,editor.spec.key)=="Original","Batched native input changed a blocked background editor");
    Probe::complete_prompt(adapter,"Ready");settle(adapter);
    check(results.size()==1&&results[0].id==900&&results[0].value=="Ready","Deferred native prompt did not complete once after event batching");
}

void services() {
    Fixture fixture;auto& adapter=fixture.adapter;std::vector<gui::ServiceResult> results;
    adapter.focus(gui::WidgetKey{"entry-12",1});
    adapter.service({701,gui::ServiceKind::prompt,"Arbitrary prompt","Default",32},[&](auto result){results.push_back(std::move(result));});
    settle(adapter);check(adapter.service_active(),"Native prompt did not open");
    const auto pending=fixture.inputs();Probe::activate(adapter,{"action-53",1});settle(adapter);
    check(fixture.inputs()==pending,"Native service prompt allowed background input");
    Probe::complete_prompt(adapter,"Réponse");settle(adapter);
    check(results.size()==1&&results[0].id==701&&results[0].status==gui::ServiceStatus::success&&results[0].value=="Réponse","Native prompt lost request identity or Unicode value");
    check(!adapter.service_active()&&adapter.focused()==gui::WidgetKey{"entry-12",1},"Native prompt did not restore application focus");
    adapter.service({702,gui::ServiceKind::prompt,"Cancel prompt","",32},[&](auto result){results.push_back(std::move(result));});
    settle(adapter);Probe::complete_prompt(adapter,"",true);settle(adapter);
    check(results.size()==2&&results[1].status==gui::ServiceStatus::cancelled,"Native prompt cancellation did not complete once");
    adapter.service({703,gui::ServiceKind::prompt,"Keyboard cancel","",32},[&](auto result){results.push_back(std::move(result));});
    settle(adapter);Probe::key(adapter,"tab");Probe::key(adapter,"tab");settle(adapter);
    fixture.widget("label-21").state.text="Background publication during prompt";fixture.publish();settle(adapter);
    Probe::key(adapter,"enter");settle(adapter);
    check(results.size()==3&&results[2].status==gui::ServiceStatus::cancelled,"Publication lost prompt keyboard focus on its cancel button");
    adapter.service({704,gui::ServiceKind::open_file,"Unsupported service","",32},[&](auto result){results.push_back(std::move(result));});
    settle(adapter);check(results.size()==4&&results[3].status==gui::ServiceStatus::error&&!results[3].error.empty(),"Unavailable host service did not report an explicit error");
    adapter.service({705,gui::ServiceKind::prompt,"Close prompt","",32},[&](auto result){results.push_back(std::move(result));});
    settle(adapter);adapter.close();settle(adapter);
    check(results.size()==5&&results[4].status==gui::ServiceStatus::cancelled,"Closing left a native service unresolved");
}

void shared_application(const std::string& output) {
    Example* app=nullptr;Adapter adapter([&](const gui::Event& event){if(app)app->handle(event);});
    Example example(adapter);app=&example;adapter.show();settle(adapter);
    write_capture(adapter.capture(),output);
    Probe::set_checked(adapter,{"toggle",1},true);settle(adapter);
    check(Probe::enabled(adapter,{"button",1}),"Shared enable rule did not update the Rev button");
    Probe::edit_text(adapter,{"editor",1},"Native é input");settle(adapter);
    Probe::activate(adapter,{"button",1});settle(adapter);
    check(gui::find_widget(example.view(),{"list",1})->state.records.size()==1,"Real Rev callbacks did not add a shared application row");
    check(gui::find_widget(example.view(),{"list",1})->state.records[0].accessible_text=="Native é input","Rev row did not use the shared editor value");
    Probe::select_option(adapter,{"choice",1},"second");settle(adapter);
    check(gui::find_widget(example.view(),{"choice",1})->state.selected=="second","Rev choice did not update shared application state");
    example.select_editor();settle(adapter);Probe::activate(adapter,{"details",1});settle(adapter);
    check(example.view().modal_root==gui::WidgetKey{"dialog",1},"Rev did not open the shared composed modal");
    Probe::key(adapter,"escape");settle(adapter);check(!example.view().modal_root,"Rev did not dismiss the shared modal");
    example.select_editor();settle(adapter);Probe::key(adapter,"enter");settle(adapter);
    const auto request=example.next_service();check(bool(request),"Rev editor did not request the shared prompt service");
    adapter.service(*request,[&](auto result){check(example.complete_service(std::move(result)),"Shared service rejected native completion");});
    settle(adapter);Probe::complete_prompt(adapter,"Shared result");settle(adapter);
    check(gui::find_widget(example.view(),{"caption",1})->state.text=="Shared result","Rev prompt result did not update the shared caption");
    Probe::page(adapter,"other");settle(adapter);Probe::page(adapter,"main");settle(adapter);
    check(gui::find_widget(example.view(),{"list",1})->state.records.size()==1,"Rev page changes lost application rows");
    adapter.close();settle(adapter);
}
}

int main(int argc,char** argv) {
    try {
        if(argc>2)throw std::invalid_argument("Usage: rev_test [screenshot.ppm]");
        shared_application(argc==2?argv[1]:"");declarations_and_pixels();retained_editing_and_options();modal_pages_and_lifetimes();native_clipboard();prompt_from_native_event_batch();services();
        std::cout<<"Rev native callbacks, pixels, arbitrary declarations, retention, lifetime, pages, modals, clipboard, services and shared application passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
