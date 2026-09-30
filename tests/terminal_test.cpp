#include "gui/terminal.hpp"
#include "../examples/application.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
std::string joined(const std::vector<std::string>& lines) {
    std::string text;for(const auto& line:lines)text+=line+'\n';return text;
}

void shared_application_input() {
    Example* app=nullptr;
    gui::TerminalAdapter adapter([&](const gui::Event& event){if(app)app->handle(event);});
    Example example(adapter);app=&example;
    adapter.terminal_size(80,31);
    check(adapter.focus(gui::WidgetKey{"editor",1}),"Could not focus shared editor");
    adapter.input("\x01"); // Ctrl-A is translated to shared selection policy.
    adapter.input("Hi ");
    adapter.input("\xf0\x9f");
    check(gui::find_widget(example.view(),{"editor",1})->state.text=="Hi ","Partial UTF-8 escaped decoder");
    adapter.input("\x99\x82");
    check(gui::find_widget(example.view(),{"editor",1})->state.text=="Hi \xf0\x9f\x99\x82","UTF-8 or space insertion failed");
    adapter.input("\x7f");
    check(gui::find_widget(example.view(),{"editor",1})->state.text=="Hi ","Backspace split a UTF-8 codepoint");
    adapter.input("\x1b[200~paste\x11\x1b[201~");
    check(!adapter.closed(),"Pasted Ctrl-Q became a command");
    check(gui::find_widget(example.view(),{"editor",1})->state.text=="Hi paste\x11","Paste was not one literal replacement");
    const auto before=gui::find_widget(example.view(),{"editor",1})->state.text;
    adapter.input("\x1b[200~"+std::string(70000,'x')+"\x1b[201~");
    check(gui::find_widget(example.view(),{"editor",1})->state.text==before,"Oversized paste applied partially");
    adapter.input("\x01" "Terminal row");
    adapter.input("\x1b[1;2D" "X");
    check(gui::find_widget(example.view(),{"editor",1})->state.text=="Terminal roX","Shift-arrow modifier lost shared selection semantics");
    adapter.input("\x01" "Terminal row");
    check(adapter.focus(gui::WidgetKey{"toggle",1}),"Could not focus toggle");adapter.input(" ");
    check(gui::find_widget(example.view(),{"toggle",1})->state.checked,"Toggle did not use shared policy");
    check(adapter.focus(gui::WidgetKey{"button",1}),"Enabled button was unfocusable");adapter.input("\r");
    const auto* list=gui::find_widget(example.view(),{"list",1});
    check(list->state.records.size()==1&&list->state.records.front().accessible_text=="Terminal row","Button did not add editor text");
    const auto row_id=list->state.records.front().id;
    adapter.focus(gui::WidgetKey{"list",1});adapter.input("\x1b[B");
    check(gui::find_widget(example.view(),{"list",1})->state.selected==row_id,"List navigation failed");
    adapter.focus(gui::WidgetKey{"choice",1});adapter.input("\r\x1b[B\r");
    check(gui::find_widget(example.view(),{"choice",1})->state.selected=="second","Popup choice did not reach application");
    adapter.focus(gui::WidgetKey{"editor",1});adapter.input("\r");
    const auto request=example.next_service();check(bool(request),"Editor submission did not request prompt");
    check(adapter.service(*request,[&](gui::ServiceResult result){example.complete_service(std::move(result));}),"Prompt rejected");
    const auto revision=example.view().revision;adapter.input("Prompt reply");
    check(example.view().revision==revision,"Prompt text leaked into background editor");
    adapter.input("\r");check(!adapter.prompt(),"Prompt did not finish");
    check(gui::find_widget(example.view(),{"caption",1})->state.text=="Prompt reply","Prompt result was lost");
    adapter.input("\x0e");check(example.view().active_page=="other","Next-page key failed");
    adapter.input("\x10");check(example.view().active_page=="main","Previous-page key failed");
    adapter.input("\x11");check(adapter.closed(),"Quit did not close shared application");
}

void spatial_rendering() {
    gui::TerminalAdapter adapter;
    gui::Snapshot view;view.title="Spatial profile";view.client_size={640,480};
    view.pages={{"alpha","Alpha"},{"beta","Beta"}};view.active_page="alpha";view.page_bar={0,448,640,32};
    const auto add=[&](std::string id,gui::Kind kind,gui::Rect bounds)->gui::Widget& {
        gui::Widget widget;widget.spec.key={std::move(id),1};widget.spec.kind=kind;widget.spec.page="alpha";
        widget.state.bounds=bounds;view.widgets.push_back(std::move(widget));return view.widgets.back();
    };
    add("left-copy",gui::Kind::label,{16,48,200,16}).state.text="Left";
    auto& records=add("arbitrary-records",gui::Kind::list,{16,256,368,112});records.spec.row_height=32;
    gui::Record record;record.id="record-z";record.accessible_text="Whole row";
    record.cells={{"Column A",{0,0,160,32},{}},{"Column B",{176,0,160,32},{}}};records.state.records.push_back(record);
    auto& bitmap=add("right-image",gui::Kind::bitmap,{440,48,160,160});
    unsigned samples=0;
    bitmap.state.bitmap={"opaque",1,gui::BitmapSource([&](const gui::BitmapRequest& request,const gui::BitmapSink& sink) {
        check(request.width==20&&request.height==10,"Terminal did not request its cell grid");
        check(request.sample_aspect_ratio==0.5&&request.fit_content,"Terminal lost physical sampling metadata");
        ++samples;gui::solid_bitmap(64,128,192).paint(request,sink);
    })};
    add("escaped-label",gui::Kind::label,{440,216,160,32}).state.text="\x1b[2J";
    adapter.present(view);
    const auto lines=adapter.render();
    check(lines.size()==31&&lines.front().size()==80,"Unexpected terminal cell grid");
    check(lines[3].substr(2,4)=="Left","Left label moved from shared rectangle");
    check(lines[3].substr(55,20)==std::string(20,'='),"Bitmap lost right-hand column or aspect");
    check(lines[16].substr(2,8)=="Column A"&&lines[16].substr(24,8)=="Column B","Structured list cells collapsed or moved");
    check(lines[28].find("[Alpha]")==0&&lines[28].find("Beta")==41,"Page strip ignored shared rectangles");
    check(joined(lines).find('\x1b')==std::string::npos&&joined(lines).find("\\x1b[2J")!=std::string::npos,
          "Presentation text injected a terminal escape");
    check(samples==1,"Unexpected bitmap sample count");
    const auto ansi=joined(adapter.ansi_rows());
    check(ansi.find("\x1b[38;2;64;128;192;48;2;64;128;192m")!=std::string::npos,"Bitmap RGB palette was not mapped to terminal color");
    check(ansi.find("\x1b[2J")==std::string::npos,"Untrusted caption became terminal control output");

    auto& image=view.widgets[2];
    gui::BitmapImage retained(4,2);
    const std::vector<std::uint8_t> white(4*2*3,255);
    retained.blit(0,0,{4,2,12,gui::PixelFormat::rgb24,white});
    image.state.bitmap.source=gui::image_bitmap(std::move(retained));++image.state.bitmap.revision;adapter.present(view);
    const auto fitted=adapter.render();
    check(fitted[3].substr(55,20)==std::string(20,' ')&&fitted[6].substr(55,20)==std::string(20,'@'),
          "Retained bitmap did not fit cell aspect with letterboxing");
    image.state.bitmap.source=gui::BitmapSource([](const gui::BitmapRequest&,const gui::BitmapSink&) {
        throw std::runtime_error("An undersampled discrete source must not be painted");
    },gui::BitmapSampling::discrete,{0,0,21,11});
    ++image.state.bitmap.revision;adapter.present(view);
    check(joined(adapter.render()).find("Bitmap needs more")!=std::string::npos,"Discrete minimum resolution silently discarded");
}

void viewport_and_limits() {
    Example* app=nullptr;
    gui::TerminalAdapter adapter([&](const gui::Event& event){if(app)app->handle(event);});
    Example example(adapter);app=&example;
    adapter.focus(gui::WidgetKey{"editor",1});adapter.text_selection({"editor",1},{2,4});
    adapter.terminal_size(20,8);
    check(adapter.focused()==gui::WidgetKey{"editor",1}&&adapter.text_selection({"editor",1})==gui::TextSelection{2,4},
          "Terminal resize discarded retained editor identity");
    auto rows=adapter.render();check(rows.size()==8&&rows[0].size()==20,"Small terminal emitted an oversized frame");
    adapter.focus(gui::WidgetKey{"bitmap",1});rows=adapter.render();
    check(adapter.viewport_origin().x>0,"Narrow viewport did not reveal right-hand bitmap");
    adapter.terminal_size(1,2);rows=adapter.render();
    check(rows.size()==2&&rows[0].size()==1,"Tiny terminal dimensions were exceeded");
    check(gui::terminal_literal("\xf0\x9f\x99\x82\x1b")=="\\u{1f642}\\x1b","Literal text is not cell-width stable");
    const auto metrics=adapter.measure_text({"one two three",{},32,1,gui::TextWrap::word});
    check(metrics.height>=48,"Word wrapping measurement did not match cells");
}

void producer_cannot_reenter_terminal() {
    gui::TerminalAdapter adapter;
    gui::Snapshot view;view.client_size={640,480};
    gui::Widget widget;widget.spec.key={"raster/reentry",1};widget.spec.kind=gui::Kind::bitmap;widget.state.bounds={16,16,80,80};
    unsigned rejected=0;
    widget.state.bitmap={"producer",1,gui::BitmapSource([&](const gui::BitmapRequest& request,const gui::BitmapSink& sink) {
        const auto reject=[&](const auto& action) {
            try {action();}catch(const std::logic_error&) {++rejected;return;}
            throw std::runtime_error("Terminal mutation escaped the guarded producer boundary");
        };
        reject([&]{adapter.terminal_size(160,60);});
        reject([&]{adapter.input("\x1b");});
        reject([&]{adapter.flush_escape();});
        reject([&]{adapter.render();});
        gui::solid_bitmap(255,255,255).paint(request,sink);
    })};
    view.widgets.push_back(std::move(widget));adapter.present(view);
    const auto cells=adapter.render();
    check(rejected==4&&adapter.columns()==80&&adapter.rows()==31&&cells.front().size()==80,
          "Rejected producer reentry changed terminal buffers or dimensions");
}
void wrapped_text_and_empty_labels() {
    gui::TerminalAdapter adapter;gui::Snapshot view;
    gui::Widget editor;editor.spec.key={"wrapped/text",1};editor.spec.kind=gui::Kind::text;
    editor.spec.text_policy.multiline=true;editor.state.bounds={16,48,84,80};editor.state.text="alpha beta gamma";
    editor.state.wrap=gui::TextWrap::word;view.widgets.push_back(editor);
    gui::Widget label;label.spec.key={"empty/caption",1};label.spec.kind=gui::Kind::label;
    label.state.bounds={16,160,240,32};label.state.label="Must stay invisible";view.widgets.push_back(label);
    adapter.present(view);const auto lines=adapter.render();
    check(lines[3].find("alpha")!=std::string::npos&&lines[4].find("beta")!=std::string::npos&&lines[5].find("gamma")!=std::string::npos,
          "Terminal editor ignored shared visual word-wrap lines");
    check(joined(lines).find("Must stay invisible")==std::string::npos,"Empty label text fell back to unrelated label metadata");
    adapter.focus(editor.spec.key);adapter.text_selection(editor.spec.key,{6,10});
    const auto styled=joined(adapter.ansi_rows());
    check(styled.find("48;2;210;227;250m")!=std::string::npos,"Wrapped text selection did not receive declared selection color");
    view.widgets[0].state.wrap=gui::TextWrap::none;adapter.present(view);
    check(adapter.editor_lines(view.widgets[0]).size()==1,"Terminal retained wrapped rows after same-generation state change");
}
}

int main() {
    try {shared_application_input();spatial_rendering();viewport_and_limits();producer_cannot_reenter_terminal();wrapped_text_and_empty_labels();std::cout<<"Terminal checks passed\n";}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
