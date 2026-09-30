#include "../backends/framebuffer/host.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
std::array<std::uint8_t,3> pixel(const gui::Frame& frame,unsigned x,unsigned y) {
    check(frame.pixels&&x<frame.width&&y<frame.height,"Invalid test pixel");
    const auto at=std::size_t(y)*frame.stride_bytes+std::size_t(x)*3;
    return {(*frame.pixels)[at],(*frame.pixels)[at+1],(*frame.pixels)[at+2]};
}
std::array<std::uint8_t,3> rgb(gui::Color color) {return {color.red,color.green,color.blue};}
void output_and_input() {
    framebuffer_example::Session session;auto& adapter=session.adapter;
    const auto initial=adapter.frame();
    check(initial.width==640&&initial.height==480&&initial.stride_bytes==1920,"Frame dimensions differ from shared client geometry");
    check(initial.damage==gui::PixelRect{0,0,640,480},"Initial frame did not advertise full damage");
    check(pixel(initial,0,0)==rgb(adapter.snapshot().palette.background),"Rasterizer ignored shared background palette");
    const auto& bitmap=framebuffer_example::first(adapter.snapshot(),gui::Kind::bitmap);
    const auto point=framebuffer_example::center(bitmap.state.bounds);
    check(pixel(initial,unsigned(point.x),unsigned(point.y))==std::array<std::uint8_t,3>{64,128,192},"Bitmap source pixels did not reach the full UI frame");
    const auto stable=adapter.frame(initial.revision);
    check(stable.pixels==initial.pixels&&stable.revision==initial.revision&&stable.damage==gui::PixelRect{},"Unchanged frame fabricated damage or new ownership");
    const auto editor=framebuffer_example::first(adapter.snapshot(),gui::Kind::text).spec.key;
    adapter.focus(editor);const auto focus=adapter.frame(initial.revision);
    check(focus.revision>initial.revision&&focus.base_revision==initial.revision&&focus.damage.width<focus.width&&focus.damage.height<focus.height,"Focus change did not produce bounded damage");
    auto reconstructed=*initial.pixels;
    for(unsigned y=focus.damage.y;y<focus.damage.y+focus.damage.height;++y) {
        const auto at=std::size_t(y)*focus.stride_bytes+std::size_t(focus.damage.x)*3;
        std::copy_n(focus.pixels->begin()+static_cast<std::ptrdiff_t>(at),std::size_t(focus.damage.width)*3,reconstructed.begin()+static_cast<std::ptrdiff_t>(at));
    }
    check(reconstructed==*focus.pixels,"Advertised damage omitted changed pixels");
    adapter.key(gui::Key::end);const auto caret=adapter.frame(focus.revision);
    check(caret.revision>focus.revision,"Editor caret was not rendered");
    const auto skipped=adapter.frame(initial.revision);
    check(skipped.base_revision==0&&skipped.damage==gui::PixelRect{0,0,skipped.width,skipped.height},"Skipped frame revisions permitted unsafe incremental upload");
    const auto original_pixels=*initial.pixels;framebuffer_example::smoke(session);
    check(*initial.pixels==original_pixels,"Retained frame pixels changed after input and resize");
    const auto resized=adapter.frame(caret.revision);
    check(resized.width==800&&resized.height==600&&resized.damage==gui::PixelRect{0,0,800,600},"Resize did not force full upload");
}
void lifetime_and_theme() {
    gui::Frame retained;std::vector<std::uint8_t> before;
    {
        gui::FramebufferAdapter adapter;gui::Snapshot view;view.client_size={40,30};view.palette.background={11,22,33};adapter.present(view);
        retained=adapter.frame();before=*retained.pixels;
        view.palette.background={44,55,66};adapter.present(view);const auto changed=adapter.frame(retained.revision);
        check(pixel(changed,0,0)==std::array<std::uint8_t,3>{44,55,66},"Shared theme update did not affect raster output");
        adapter.close();check(*retained.pixels==before,"Close invalidated retained frame");
    }
    check(retained.pixels&&*retained.pixels==before,"Adapter destruction invalidated retained frame ownership");
}
void clipping_scale_and_formats() {
    gui::FramebufferAdapter adapter;gui::Snapshot view;view.client_size={60,50};view.display_scale=2;view.bitmap_format=gui::PixelFormat::mono1;
    gui::Widget parent;parent.spec.key.id="container";parent.spec.kind=gui::Kind::group;parent.state.bounds={5,5,30,30};parent.state.content_clip=gui::Rect{8,8,10,10};view.widgets.push_back(parent);
    gui::Widget image;image.spec.key.id="pixels";image.spec.parent="container";image.spec.kind=gui::Kind::bitmap;image.state.bounds={5,5,30,30};image.state.bitmap={"white",1,gui::solid_bitmap(255,255,255)};view.widgets.push_back(image);
    adapter.present(view);const auto frame=adapter.frame();
    check(frame.width==120&&frame.height==100,"Display scale did not control complete image dimensions");
    check(pixel(frame,28,28)==std::array<std::uint8_t,3>{255,255,255},"Mono1 bitmap did not convert to host RGB pixels");
    check(pixel(frame,12,12)==rgb(view.palette.surface),"Bitmap rendering escaped ancestor content clip");
    const auto retained=frame.pixels;
    view.widgets[1].state.bitmap={"failure",2,gui::BitmapSource([](const gui::BitmapRequest&,const gui::BitmapSink&){throw std::runtime_error("paint failed");})};adapter.present(view);
    bool threw=false;try {(void)adapter.frame(frame.revision);}catch(const std::runtime_error&) {threw=true;}
    check(threw&&frame.pixels==retained,"Producer failure invalidated last complete frame");
    view.widgets[1].state.bitmap={"black",3,gui::solid_bitmap(0,0,0)};adapter.present(view);const auto recovered=adapter.frame(frame.revision);
    check(pixel(recovered,28,28)==std::array<std::uint8_t,3>{0,0,0},"A failed producer prevented later successful paint");
}
void modal_and_prompt_output() {
    framebuffer_example::Session session;auto& adapter=session.adapter;
    const auto background=adapter.frame();
    // The composition-root fixture exercises the shared application declaration;
    // renderers and hosts themselves dispatch only generic identities and kinds.
    adapter.policy().send(gui::WidgetEvent{{"details",1},gui::Activate{}});
    check(adapter.snapshot().modal_root.has_value(),"Shared modal did not open");
    const auto modal=adapter.frame(background.revision);check(modal.revision>background.revision,"Modal subtree was not painted");
    adapter.key(gui::Key::escape);check(!adapter.snapshot().modal_root,"Escape did not dispatch declared dismissal shortcut");
    const auto normal=adapter.frame(modal.revision);
    const auto editor=framebuffer_example::first(adapter.snapshot(),gui::Kind::text).spec.key;
    adapter.focus(editor);adapter.key(gui::Key::enter);session.tick();const auto prompt=adapter.frame(normal.revision);
    check(adapter.prompt()&&prompt.revision>normal.revision,"Prompt overlay was not painted");
    adapter.text("Visible reply");adapter.key(gui::Key::enter);check(!adapter.prompt(),"Prompt did not complete");
    check(adapter.frame(prompt.revision).revision>prompt.revision,"Prompt completion did not redraw content");
}
void custom_font_and_paint_guards() {
    gui::FramebufferAdapter* active=nullptr;bool saw_unicode=false,mutation_rejected=false,recursion_rejected=false;
    gui::FramebufferTextRenderer font;
    font.measure=[](const gui::TextMeasureRequest& request) {return gui::Size{double(gui::framebuffer_detail::count(request.text))*9,11};};
    font.paint=[&](const gui::FramebufferTextRequest& request,const gui::FramebufferTextSink& sink) {
        saw_unicode=request.text=="é";
        try {active->service({23,gui::ServiceKind::prompt,"Unexpected","",20},[](gui::ServiceResult){});}
        catch(const std::logic_error&) {mutation_rejected=true;}
        try {(void)active->frame();}catch(const std::logic_error&) {recursion_rejected=true;}
        sink({request.bounds.x,request.bounds.y,3,5},request.color);
    };
    gui::FramebufferAdapter adapter({},4096,std::move(font));active=&adapter;
    gui::Snapshot view;view.client_size={30,30};gui::Widget label;label.spec.key.id="glyph";label.spec.kind=gui::Kind::label;
    label.state.bounds={10,10,12,12};label.state.text="é";view.widgets.push_back(label);adapter.present(view);
    check(adapter.measure_text({"é",{},30,1,gui::TextWrap::none})==gui::Size{9,11},"Injected text metrics were ignored");
    const auto frame=adapter.frame();
    check(saw_unicode&&pixel(frame,11,11)==rgb(view.palette.text),"Injected font did not receive UTF-8 and paint pixels");
    check(mutation_rejected&&recursion_rejected&&!adapter.prompt(),"Custom glyph painter bypassed mutation/reentrancy guards");
    gui::FramebufferAdapter source_adapter;gui::Snapshot source_view;source_view.client_size={30,30};
    gui::Widget bitmap;bitmap.spec.key.id="source";bitmap.spec.kind=gui::Kind::bitmap;bitmap.state.bounds={0,0,30,30};
    bool source_guard=false;
    bitmap.state.bitmap={"guard",1,gui::BitmapSource([&](const gui::BitmapRequest& request,const gui::BitmapSink& sink) {
        try {source_adapter.service({24,gui::ServiceKind::prompt,"Unexpected","",20},[](gui::ServiceResult){});}
        catch(const std::logic_error&) {source_guard=true;}
        gui::solid_bitmap(1,2,3).paint(request,sink);
    })};source_view.widgets.push_back(bitmap);source_adapter.present(source_view);
    check(pixel(source_adapter.frame(),1,1)==std::array<std::uint8_t,3>{1,2,3}&&source_guard&&!source_adapter.prompt(),"Bitmap producer bypassed shared interaction paint guard");
}
void wrapped_editor_output() {
    gui::FramebufferAdapter adapter;gui::Snapshot view;view.client_size={120,100};
    gui::Widget editor;editor.spec.key.id="wrapped-text";editor.spec.kind=gui::Kind::text;editor.spec.text_policy.multiline=true;
    editor.state.bounds={5,5,58,64};editor.state.text="ab cd ef";editor.state.wrap=gui::TextWrap::word;view.widgets.push_back(editor);adapter.present(view);
    adapter.focus(editor.spec.key);adapter.text_selection(editor.spec.key,{8,8});
    const auto lines=adapter.editor_lines(editor);check(lines.size()>=3,"Narrow editor did not use shared word-wrap lines");
    const auto frame=adapter.frame();const auto content=adapter.text_bounds(editor);const auto ink=rgb(view.palette.text);
    for(std::size_t row=0;row<lines.size();++row) {
        std::size_t pixels=0;const auto y=unsigned(content.y+double(row)*adapter.line_height(editor.state.font));
        for(unsigned dy=0;dy<unsigned(adapter.line_height(editor.state.font));++dy)
            for(unsigned x=unsigned(content.x);x<unsigned(content.x+content.width);++x)if(pixel(frame,x,y+dy)==ink)++pixels;
        check(pixels>0,"Software editor omitted a wrapped visual line");
    }
    check(adapter.editor_caret_position(editor,8).y>0,"Wrapped editor caret remained on first row");
}
void budget() {
    gui::FramebufferAdapter adapter({},1024);gui::Snapshot view;view.client_size={30,30};bool threw=false;
    try {adapter.present(view);}catch(const std::length_error&) {threw=true;}
    check(threw&&adapter.snapshot().revision==0,"Frame budget was not enforced before presentation");
    view.client_size={10,10};adapter.present(view);check(adapter.frame().pixels->size()==300,"Budget failure prevented smaller recovery");
}
}
int main() {
    try {output_and_input();lifetime_and_theme();clipping_scale_and_formats();modal_and_prompt_output();custom_font_and_paint_guards();wrapped_editor_output();budget();std::cout<<"Framebuffer rendering, input, damage and ownership passed\n";}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
