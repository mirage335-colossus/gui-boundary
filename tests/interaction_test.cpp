#include "gui/interaction.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
struct Fixture {
    gui::Snapshot view;
    std::vector<gui::Event> events;
    gui::InteractiveAdapter adapter;
    Fixture():adapter([this](const gui::Event& event){receive(event);},[](const gui::TextMeasureRequest& request) {
        std::size_t width=0,current=0,rows=1;
        for(std::size_t at=0;at<request.text.size();at=gui::interaction_detail::next(request.text,at)) {
            if(request.text[at]=='\n') {width=std::max(width,current);current=0;++rows;}else ++current;
        }
        return gui::Size{double(std::max(width,current))*8,double(rows)*16};
    }) {
        view.client_size={360,260};view.pages={{"front","Front"},{"back","Back"}};view.active_page="front";view.page_bar={0,236,360,24};
        auto& editor=add("input-42",gui::Kind::text,{8,8,112,24});
        editor.state.text="Aé🙂Z";editor.spec.text_policy={true,false,1024,gui::SubmitKey::enter};
        auto& choice=add("pick-81",gui::Kind::choice,{8,40,112,24});
        choice.state.options={{"old","Old","",true},{"off","Off","",false},{"new","New","",true}};choice.state.selected="old";
        add("tick-17",gui::Kind::toggle,{140,8,112,24});
        add("press-20",gui::Kind::button,{140,40,112,24});
        auto& rows=add("rows-92",gui::Kind::list,{8,80,200,48});rows.spec.row_height=24;
        rows.state.records={{"r0","First",{},true,true},{"r1","Disabled",{},false,false},{"r2","Third",{},true,true}};
        auto& raw=add("raw-3",gui::Kind::label,{220,80,100,32});raw.spec.pointer_input=true;raw.state.text="Raw input";
        adapter.present(view);
    }
    gui::Widget& add(std::string id,gui::Kind kind,gui::Rect bounds,std::string parent={}) {
        gui::Widget widget;widget.spec.key.id=std::move(id);widget.spec.kind=kind;widget.spec.page="front";widget.spec.parent=std::move(parent);widget.state.bounds=bounds;
        view.widgets.push_back(std::move(widget));return view.widgets.back();
    }
    gui::Widget& widget(std::string_view id) {
        for(auto& widget:view.widgets)if(widget.spec.key.id==id)return widget;
        throw std::runtime_error("Missing fixture widget");
    }
    void publish() {++view.revision;adapter.present(view);}
    void receive(const gui::Event& event) {
        events.push_back(event);
        if(const auto* input=std::get_if<gui::WidgetEvent>(&event)) {
            auto& state=widget(input->target.id).state;
            if(const auto* edit=std::get_if<gui::EditText>(&input->input))state.text=edit->value;
            else if(const auto* tick=std::get_if<gui::SetChecked>(&input->input))state.checked=tick->value;
            else if(const auto* choose=std::get_if<gui::ChooseOption>(&input->input))state.selected=choose->id;
            else if(const auto* row=std::get_if<gui::SelectRecord>(&input->input))state.selected=row->id;
        } else if(const auto* page=std::get_if<gui::PageEvent>(&event))view.active_page=page->id;
        else if(const auto* size=std::get_if<gui::ResizeEvent>(&event)) {view.client_size=size->client_size;view.display_scale=size->display_scale;}
        else if(std::holds_alternative<gui::CloseEvent>(event)) {adapter.close();return;}
        publish();
    }
};
void unicode_and_scroll() {
    Fixture fixture;auto& adapter=fixture.adapter;const gui::WidgetKey editor{"input-42",1};
    check(adapter.focus(editor),"Editor could not receive focus");adapter.key(gui::Key::end);
    adapter.key(gui::Key::backspace);adapter.key(gui::Key::backspace);
    check(fixture.widget(editor.id).state.text=="Aé","Backspace split a Unicode codepoint");
    adapter.key(gui::Key::left);adapter.key(gui::Key::delete_key);
    check(fixture.widget(editor.id).state.text=="A","Delete split a Unicode codepoint");
    adapter.key(gui::Key::select_all);adapter.text(std::string(80,'w'));
    check(adapter.scroll_offset(editor).x>500,"Long editor text did not reveal its caret");
    adapter.key(gui::Key::home);check(adapter.scroll_offset(editor).x==0,"Home did not reveal the start");
    adapter.scroll(editor,{99999,-20});check(adapter.scroll_offset(editor).x<700&&adapter.scroll_offset(editor).y==0,"Editor scroll was not clamped");
    adapter.key(gui::Key::select_all);adapter.text("ab\nçd\nxyz");adapter.key(gui::Key::home);adapter.key(gui::Key::up);
    check(adapter.text_selection(editor).caret==3,"Multiline navigation lost the requested column");
    adapter.key(gui::Key::right);check(adapter.text_selection(editor).caret==5,"Multiline navigation split UTF-8");
    auto& replacement=fixture.widget(editor.id);replacement.spec.key.generation=2;replacement.state.text="fresh";fixture.publish();
    check(!adapter.focused(),"Replaced generation retained focus");
    check(adapter.scroll_offset({editor.id,2})==gui::Point{},"Replaced editor inherited obsolete scrolling");
}
void popup_and_list() {
    Fixture fixture;auto& adapter=fixture.adapter;const gui::WidgetKey choice{"pick-81",1};
    adapter.focus(choice);adapter.key(gui::Key::down);check(bool(adapter.popup()),"Choice popup did not open");
    adapter.key(gui::Key::down);adapter.key(gui::Key::enter);
    check(fixture.widget(choice.id).state.selected=="new","Popup did not skip disabled option");
    fixture.widget(choice.id).state.selected="old";fixture.publish();adapter.open_popup(choice);
    fixture.widget(choice.id).state.options.erase(fixture.widget(choice.id).state.options.begin());fixture.widget(choice.id).state.selected.reset();fixture.publish();
    const auto before=fixture.events.size();adapter.key(gui::Key::enter);
    check(fixture.events.size()==before,"Stale popup index was rebound to a different option");
    adapter.focus(gui::WidgetKey{"rows-92",1});adapter.key(gui::Key::down);adapter.key(gui::Key::down);
    check(fixture.widget("rows-92").state.selected=="r2","List navigation did not skip disabled row");
    check(adapter.scroll_offset({"rows-92",1}).y==24,"List navigation did not reveal selected row");
    adapter.key(gui::Key::enter);check(std::holds_alternative<gui::ActivateRecord>(std::get<gui::WidgetEvent>(fixture.events.back()).input),"List activation was not semantic");
}
void wrapped_editor_lines() {
    Fixture fixture;auto& adapter=fixture.adapter;const gui::WidgetKey key{"input-42",1};
    auto& editor=fixture.widget(key.id);editor.state.bounds={8,8,52,72};editor.state.wrap=gui::TextWrap::word;
    editor.state.text="ab é🙂 cd\nx";fixture.publish();adapter.focus(key);
    using Line=gui::InteractiveAdapter::VisualLine;
    check(adapter.editor_lines(editor)==std::vector<Line>{{0,9},{10,12},{13,14}},"Word wrapping lost UTF-8 byte boundaries or hard newline");
    adapter.text_selection(key,{14,14});adapter.key(gui::Key::up);
    check(adapter.text_selection(key).caret==11,"Up did not navigate a visual line");
    adapter.key(gui::Key::up);check(adapter.text_selection(key).caret==1,"Visual navigation lost measured horizontal position");
    adapter.key(gui::Key::end);check(adapter.text_selection(key).caret==9,"End ignored the soft visual line");
    adapter.key(gui::Key::home);check(adapter.text_selection(key).caret==0,"Home ignored the soft visual line");
    const auto box=adapter.text_bounds(editor);
    adapter.pointer({gui::PointerKind::click,{box.x+8,box.y+16+3}});
    check(adapter.text_selection(key).caret==11,"Pointer hit testing used hard lines instead of visual lines");
    adapter.key(gui::Key::home,false,true);
    check(adapter.text_selection(key)==gui::TextSelection{11,10},"Shift-Home did not retain selection anchor on visual line");

    editor.state.bounds={8,8,28,40};editor.state.text="é🙂Z";fixture.publish();adapter.text_selection(key,{0,0});
    check(adapter.editor_lines(editor)==std::vector<Line>{{0,6},{6,7}},"Long word wrapping split a UTF-8 codepoint");
    adapter.key(gui::Key::end);
    check(adapter.text_selection(key).caret==6&&adapter.editor_caret_position(editor,6)==gui::Point{16,0},
          "End lost trailing affinity at a shared soft-wrap byte boundary");
    adapter.key(gui::Key::right);
    check(adapter.text_selection(key).caret==7&&adapter.editor_caret_position(editor,7)==gui::Point{8,16},"Right did not cross soft line boundary");
    editor.state.wrap=gui::TextWrap::none;fixture.publish();
    check(adapter.editor_lines(editor)==std::vector<Line>{{0,7}}&&adapter.scroll_offset(key).x>0,"Disabling wrap retained old lines or scroll extent");
    editor.state.wrap=gui::TextWrap::word;editor.state.bounds.height=20;fixture.publish();
    check(adapter.editor_lines(editor).size()==2&&adapter.scroll_offset(key).x==0&&adapter.scroll_offset(key).y>0,
          "Enabling wrap did not reveal the caret vertically");
    editor.state.bounds.width=76;fixture.publish();
    check(adapter.editor_lines(editor).size()==1&&adapter.scroll_offset(key).y<=2,"Resize did not rebuild wrapped visual lines");
    editor.state.text="one two three four";editor.state.bounds.width=76;fixture.publish();
    const auto lines=adapter.editor_lines(editor);
    check(lines.front()==Line{0,7},"Word wrap chose the first separator instead of the last fitting separator");
}
void navigation_does_not_activate() {
    Fixture fixture;auto& adapter=fixture.adapter;
    adapter.focus(gui::WidgetKey{"press-20",1});adapter.key(gui::Key::down);
    adapter.focus(gui::WidgetKey{"tick-17",1});adapter.key(gui::Key::down);
    check(fixture.events.empty(),"Down activated an unrelated button or toggle");
    auto& group=fixture.add("scrolling",gui::Kind::group,{0,120,120,40});group.state.content_size={120,300};
    auto& child=fixture.add("inside",gui::Kind::choice,{4,124,100,28},"scrolling");
    child.state.options={{"disabled","Disabled","",false},{"enabled","Enabled","",true}};fixture.publish();
    check(adapter.open_popup({"inside",1})&&adapter.popup()->index==1,"Popup initially selected a disabled item");
    adapter.scroll({"scrolling",1},{0,100});
    check(!adapter.popup(),"Ancestor scrolling retained an invisible popup");
}
void pointer_and_pages() {
    Fixture fixture;auto& adapter=fixture.adapter;
    adapter.pointer({gui::PointerKind::click,{150,16}});check(fixture.widget("tick-17").state.checked,"Pointer missed generic toggle");
    const auto before=fixture.events.size();adapter.pointer({gui::PointerKind::click,{225,90}});
    check(fixture.events.size()==before+1&&std::holds_alternative<gui::PointerInput>(std::get<gui::WidgetEvent>(fixture.events.back()).input),"Non-bitmap pointer opt-in was ignored");
    adapter.pointer({gui::PointerKind::click,{270,245}});check(fixture.view.active_page=="back","Declared page bar was not interactive");
    adapter.key(gui::Key::previous_page);check(fixture.view.active_page=="front","Previous page command failed");
    adapter.resize({700,500},2);check(fixture.view.client_size==gui::Size{700,500}&&fixture.view.display_scale==2,"Resize changed shared logical dimensions");
}
void modality_and_services() {
    Fixture fixture;auto& adapter=fixture.adapter;
    fixture.add("layer",gui::Kind::group,{32,32,280,170});fixture.add("dismiss-99",gui::Kind::button,{48,140,120,24},"layer");
    fixture.view.modal_root=gui::WidgetKey{"layer",1};fixture.view.key_bindings.push_back({gui::ShortcutKey::escape,{"dismiss-99",1}});fixture.publish();
    const auto before=fixture.events.size();adapter.pointer({gui::PointerKind::click,{150,16}});adapter.key(gui::Key::next_page);
    check(fixture.events.size()==before,"Background input escaped modal scope");
    check(adapter.focus_next()&&adapter.focused()==gui::WidgetKey{"dismiss-99",1},"Focus traversal escaped modal subtree");
    std::vector<gui::ServiceResult> results;
    check(adapter.service({17,gui::ServiceKind::prompt,"Name","old",8},[&](gui::ServiceResult result){results.push_back(std::move(result));}),"Prompt was rejected");
    adapter.text("123456789");check(adapter.prompt()->value=="old","Prompt accepted excessive bytes");
    adapter.text("value");adapter.key(gui::Key::escape);
    check(results.size()==1&&results[0].status==gui::ServiceStatus::cancelled&&fixture.events.size()==before,"Prompt Escape leaked to application shortcut");
    adapter.key(gui::Key::escape);check(fixture.events.size()==before+1&&std::get<gui::WidgetEvent>(fixture.events.back()).target.id=="dismiss-99","Eligible shortcut did not become activation");
    adapter.service({18,gui::ServiceKind::prompt,"Name","",8},[&](gui::ServiceResult result){results.push_back(std::move(result));});
    adapter.text("é");adapter.key(gui::Key::enter);
    check(results.size()==2&&results.back().value=="é"&&!adapter.prompt(),"Prompt result was not completed exactly once");
    adapter.service({19,gui::ServiceKind::open_file,"Open","",8},[&](gui::ServiceResult result){results.push_back(std::move(result));});
    check(results.back().status==gui::ServiceStatus::error,"Unsupported host service fabricated success");
    adapter.service({20,gui::ServiceKind::prompt,"Pending","",8},[&](gui::ServiceResult result){results.push_back(std::move(result));});
    const auto count=results.size();adapter.key(gui::Key::quit);
    check(adapter.closed()&&results.size()==count&&!adapter.prompt(),"Close failed to revoke pending prompt");
}
}
int main() {
    try {unicode_and_scroll();wrapped_editor_lines();popup_and_list();navigation_does_not_activate();pointer_and_pages();modality_and_services();std::cout<<"Shared software interaction passed\n";}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
