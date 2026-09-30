#include "adapter.hpp"
#include "application.hpp"
#include <iostream>
#include <fstream>
#include <array>

void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
std::vector<unsigned char> capture(gui::fltk::Adapter& adapter,const std::string& output={}) {
    adapter.sync();Fl::check();Fl::wait(.05);Fl::flush();adapter.window().make_current();
    const auto width=adapter.window().w(),height=adapter.window().h();
    const std::unique_ptr<unsigned char[]> bytes(fl_read_image(nullptr,0,0,width,height));
    check(bool(bytes),"Native window capture failed");
    std::vector<unsigned char> result(bytes.get(),bytes.get()+std::size_t(width)*std::size_t(height)*3);
    if(!output.empty()) {
        std::ofstream file(output,std::ios::binary);file<<"P6\n"<<width<<' '<<height<<"\n255\n";
        file.write(reinterpret_cast<const char*>(result.data()),static_cast<std::streamsize>(result.size()));check(bool(file),"Native screenshot write failed");
    }
    return result;
}
void application_smoke(const std::string& output) {
        Example* app=nullptr;
        gui::fltk::Adapter adapter([&](const gui::Event& event){app->handle(event);});
        Example example(adapter);app=&example;adapter.show();Fl::check();
        auto* toggle=dynamic_cast<Fl_Check_Button*>(adapter.native_widget({"toggle",1}));
        check(toggle,"Missing native toggle");toggle->value(1);toggle->do_callback();adapter.sync();
        auto* button=dynamic_cast<Fl_Button*>(adapter.native_widget({"button",1}));
        check(button&&button->active(),"Shared state did not enable native button");
        auto* editor=dynamic_cast<Fl_Text_Editor*>(adapter.native_widget({"editor",1}));
        check(editor,"Missing native editor");editor->buffer()->text("Native input");editor->do_callback();button->do_callback();adapter.sync();
        check(gui::find_widget(example.view(),{"list",1})->state.records.size()==1,"Native action did not update shared application");
        check(gui::find_widget(example.view(),{"editor",1})->state.text=="Native input","Native edit did not reach model");
        auto* choice=dynamic_cast<Fl_Choice*>(adapter.native_widget({"choice",1}));
        check(choice,"Missing native choice");choice->value(1);choice->do_callback();adapter.sync();
        check(gui::find_widget(example.view(),{"choice",1})->state.selected=="second","Native choice lost identity");
        example.select_editor();check(adapter.focused()==gui::WidgetKey{"editor",1},"Native focus not retained");
        const auto screenshot=capture(adapter,output);check(!screenshot.empty(),"Native window did not render");
        adapter.policy().enter({"editor",1});const auto request=example.next_service();check(bool(request),"Prompt not requested");
        adapter.service(*request,[&](gui::ServiceResult result){example.complete_service(std::move(result));});
        check(adapter.service_active(),"Modeless native service did not open");
        adapter.close();adapter.sync();Fl::check();check(!adapter.window().shown(),"Native close left window open");
        check(adapter.error().empty(),"Native callback or drawing failure");
 }
struct Fixture {
    gui::Snapshot view;
    std::vector<gui::Event> events;
    gui::fltk::Adapter adapter;
    Fixture():adapter([this](const gui::Event& event) {
        events.push_back(event);
        if(const auto* input=std::get_if<gui::WidgetEvent>(&event)) {
            auto& current=widget(input->target.id);
            if(const auto* text=std::get_if<gui::EditText>(&input->input))current.state.text=text->value;
            if(const auto* check=std::get_if<gui::SetChecked>(&input->input))current.state.checked=check->value;
        }
        ++view.revision;adapter.present(view);adapter.sync();
    }) {
        view.client_size={520,380};view.title="Arbitrary native declarations";
        auto& group=add("g-70",gui::Kind::group,{10,10,190,160});group.state.content_size={190,360};
        add("action-z",gui::Kind::button,{20,20,140,28},"g-70").state.label="A&B @ button";
        add("label-k",gui::Kind::label,{20,70,160,32},"g-70").state.text="Visible child";
        auto& text=add("edit-z",gui::Kind::text,{230,10,260,90});text.spec.text_policy={true,false,1024,gui::SubmitKey::none};
        for(int row=0;row<8;++row) {if(row)text.state.text+="\n";text.state.text+=std::string(80,'W');}
        auto& empty=add("empty-z",gui::Kind::text,{230,120,260,28});empty.state.placeholder="A&B @ placeholder";empty.spec.text_policy.max_bytes=12;
        auto& list=add("list-z",gui::Kind::list,{230,164,260,68});list.spec.row_height=30;
        gui::Record row;row.id="row-z";row.accessible_text="Fallback row";list.state.records.push_back(row);
        row.id="literal-row";row.cells={{"A&B @ cell",{4,0,230,28},{14,false,gui::Tone::error}}};list.state.records.push_back(row);
        auto& choice=add("choice-z",gui::Kind::choice,{230,248,260,28});
        choice.state.options={{"first-z","A&B / @ literal","",true}};choice.state.selected="first-z";choice.state.font.tone=gui::Tone::accent;
        auto& pointer=add("pointer-z",gui::Kind::label,{230,292,260,24});pointer.state.text="Raw pointer label";pointer.spec.pointer_input=true;
        view.key_bindings.push_back({gui::ShortcutKey::escape,{"action-z",1}});
        adapter.present(view);
        adapter.scroll({"edit-z",1},{90,20});
        check(adapter.scroll_offset({"edit-z",1}).x==90,"Native editor scroll could not be queued before synchronization");
        adapter.show();Fl::check();Fl::wait(.05);
    }
    gui::Widget& add(std::string id,gui::Kind kind,gui::Rect bounds,std::string parent={}) {
        gui::Widget value;value.spec.key.id=std::move(id);value.spec.kind=kind;value.spec.parent=std::move(parent);value.state.bounds=bounds;
        view.widgets.push_back(std::move(value));return view.widgets.back();
    }
    gui::Widget& widget(std::string_view id) {
        for(auto& value:view.widgets)if(value.spec.key.id==id)return value;
        throw std::runtime_error("Missing fixture widget");
    }
};
struct EventState {
    int x=Fl::e_x,y=Fl::e_y,dx=Fl::e_dx,dy=Fl::e_dy,key=Fl::e_keysym,state=Fl::e_state,clicks=Fl::e_clicks;
    ~EventState() {Fl::e_x=x;Fl::e_y=y;Fl::e_dx=dx;Fl::e_dy=dy;Fl::e_keysym=key;Fl::e_state=state;Fl::e_clicks=clicks;}
};
void arbitrary_declarations() {
    Fixture fixture;auto& adapter=fixture.adapter;EventState restore;
    const auto screenshot=capture(adapter);
    const auto count_color=[&](gui::Rect box,gui::Color color) {
        std::size_t count=0;const auto width=adapter.window().w();
        for(int y=int(box.y);y<int(box.y+box.height);++y)for(int x=int(box.x);x<int(box.x+box.width);++x) {
            const auto at=(std::size_t(y)*std::size_t(width)+std::size_t(x))*3;
            if(screenshot[at]==color.red&&screenshot[at+1]==color.green&&screenshot[at+2]==color.blue)++count;
        }
        return count;
    };
    check(count_color({234,123,250,20},fixture.view.palette.muted)>5,"Native empty editor omitted its placeholder");
    check(count_color({234,194,250,28},fixture.view.palette.error)>5,"Native list cell ignored its declared text tone");
    auto* choice=dynamic_cast<Fl_Choice*>(adapter.native_widget({"choice-z",1}));
    check(choice&&std::string(choice->menu()[0].label())=="A&B / @ literal","Native option parsed literal menu text");
    Fl::e_x=40;Fl::e_y=30;Fl::e_dx=0;Fl::e_dy=2;Fl::e_state=0;
    check(adapter.window().handle(FL_MOUSEWHEEL)==1,"Native wheel did not reach a button's scrollable ancestor");
    adapter.sync();check(adapter.scroll_offset({"g-70",1}).y==64,"Ancestor wheel scroll did not update shared retained offset");
    check(adapter.native_widget({"action-z",1})->y()==-44,"Native descendants did not follow shared group translation");
    adapter.scroll({"g-70",1},{0,0});adapter.sync();
    adapter.scroll({"edit-z",1},{120,32});adapter.sync();Fl::check();
    const auto offset=adapter.scroll_offset({"edit-z",1});if(!(offset.x>0&&offset.y>0))throw std::runtime_error("Native editor did not scroll on both axes: "+std::to_string(offset.x)+", "+std::to_string(offset.y));
    fixture.widget("label-k").state.text="New label";adapter.present(fixture.view);adapter.sync();Fl::check();
    check(adapter.scroll_offset({"edit-z",1}).x>0&&adapter.scroll_offset({"edit-z",1}).y>0,"Unrelated publication reset native editor scrolling");
    adapter.text_selection({"edit-z",1},{0,4});Fl::check();
    auto* editor=dynamic_cast<Fl_Text_Editor*>(adapter.native_widget({"edit-z",1}));int first=0,last=0;
    check(editor&&editor->buffer()->selection_position(&first,&last)&&first==0&&last==4,"Shared selection was not applied to native editor");
    auto* empty=dynamic_cast<Fl_Text_Editor*>(adapter.native_widget({"empty-z",1}));check(empty,"Missing arbitrary native editor");
    const auto before=fixture.events.size();empty->buffer()->text("not\none line");empty->do_callback();adapter.sync();
    check(fixture.events.size()==before&&fixture.widget("empty-z").state.text.empty()&&empty->buffer()->length()==0,"Rejected native paste left divergent text");
    empty->buffer()->text("é");empty->do_callback();adapter.sync();
    check(fixture.widget("empty-z").state.text=="é","Native UTF-8 edit did not reach arbitrary model binding");
    Fl::e_keysym=FL_Escape;Fl::e_state=0;adapter.window().handle(FL_KEYDOWN);
    check(std::get<gui::WidgetEvent>(fixture.events.back()).target.id=="action-z","Declared native Escape shortcut did not activate its target");
    Fl::e_x=245;Fl::e_y=300;Fl::e_keysym=FL_Button+1;Fl::e_clicks=0;
    adapter.window().handle(FL_PUSH);
    check(std::holds_alternative<gui::PointerInput>(std::get<gui::WidgetEvent>(fixture.events.back()).input),"Generic native pointer opt-in was lost");
    adapter.open_popup({"choice-z",1});fixture.view.widgets.erase(std::remove_if(fixture.view.widgets.begin(),fixture.view.widgets.end(),[](const gui::Widget& value){return value.spec.key.id=="choice-z";}),fixture.view.widgets.end());
    adapter.present(fixture.view);adapter.sync();check(adapter.error().empty(),"Retiring a popup's widget caused native lifecycle failure");
    adapter.scroll({"edit-z",1},{80,20});fixture.widget("edit-z").spec.key.generation=2;fixture.widget("edit-z").state.text="Replacement";
    adapter.present(fixture.view);check(adapter.scroll_offset({"edit-z",2})==gui::Point{},"Replacement editor inherited pending scroll from an obsolete generation");
    adapter.sync();Fl::check();check(adapter.scroll_offset({"edit-z",2})==gui::Point{},"Replacement native editor retained obsolete viewport state");
    adapter.close();adapter.sync();
    bool rejected=false;try {(void)adapter.scroll_offset({"edit-z",2});}catch(const std::logic_error&) {rejected=true;}
    check(rejected,"Closed native scroll query bypassed contract guard");
}
int main(int argc,char** argv) {
    try {
        if(argc>2)throw std::invalid_argument("Usage: fltk_test [screenshot.ppm]");
        application_smoke(argc==2?argv[1]:"");arbitrary_declarations();
        std::cout<<"Native widgets, generic declarations, scrolling, input, rendering, prompt and teardown passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
