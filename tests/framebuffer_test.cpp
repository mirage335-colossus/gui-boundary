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
bool contains(gui::DeviceRect box,unsigned x,unsigned y) {
    return int(x)>=box.x&&int(y)>=box.y&&int(x)<box.x+int(box.width)&&int(y)<box.y+int(box.height);
}
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
void prompt_chrome_and_input() {
    gui::FramebufferAdapter adapter;gui::Snapshot view;view.client_size={360,220};adapter.present(view);
    std::vector<gui::ServiceResult> results;
    const auto open=[&](std::uint64_t id) {
        check(adapter.service({id,gui::ServiceKind::prompt,"Choose a display name","Existing",32},
            [&](gui::ServiceResult result){results.push_back(std::move(result));}),"Prompt service was rejected");
    };
    open(31);const auto frame=adapter.frame();
    const auto field=gui::device_rect(adapter.prompt_field_bounds(),1);std::size_t highlighted=0;
    for(unsigned y=unsigned(field.y);y<unsigned(field.y)+field.height;++y)
        for(unsigned x=unsigned(field.x);x<unsigned(field.x)+field.width;++x)
            if(pixel(frame,x,y)==rgb(view.palette.selection))++highlighted;
    check(highlighted>0,"Prompt omitted the initial selected-text highlight");
    for(const auto area:{adapter.prompt_cancel_bounds(),adapter.prompt_accept_bounds()}) {
        const auto box=gui::device_rect(area,1);
        check(pixel(frame,unsigned(box.x)+2,unsigned(box.y)+2)==rgb(view.palette.surface),
            "Prompt action did not render its declared surface");
        check(pixel(frame,unsigned(box.x)+box.width/2,unsigned(box.y))==rgb(view.palette.border),
            "Prompt action did not render a visible border");
    }
    adapter.pointer({gui::PointerKind::click,framebuffer_example::center(adapter.prompt_cancel_bounds())});
    check(!adapter.prompt()&&results.size()==1&&results.back().id==31&&results.back().status==gui::ServiceStatus::cancelled,
        "Rendered Cancel button did not use shared prompt dismissal");
    open(32);adapter.text("Replacement");
    adapter.pointer({gui::PointerKind::click,framebuffer_example::center(adapter.prompt_accept_bounds())});
    check(!adapter.prompt()&&results.size()==2&&results.back().id==32&&results.back().status==gui::ServiceStatus::success&&results.back().value=="Replacement",
        "Rendered OK button did not accept the shared prompt text exactly once");
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
void default_font_output() {
    const gui::Color background{219,191,153},foreground{17,73,131};
    const auto render=[&](std::string text,bool bold=false,double scale=1) {
        gui::FramebufferAdapter adapter;gui::Snapshot view;view.client_size={160,40};view.display_scale=scale;
        view.palette.background=background;view.palette.text=foreground;
        gui::Widget label;label.spec.key.id="sample";label.spec.kind=gui::Kind::label;
        label.state.bounds={4,4,152,32};label.state.text=std::move(text);label.state.font={14,bold};view.widgets.push_back(label);
        adapter.present(view);return adapter.frame();
    };
    for(const double scale:{1.0,1.25,1.5,2.0}) {
        const auto frame=render("Aa éÑß",false,scale);std::size_t painted=0,blended=0;
        for(unsigned y=0;y<frame.height;++y)for(unsigned x=0;x<frame.width;++x) {
            const auto value=pixel(frame,x,y);if(value==rgb(background))continue;
            ++painted;if(value!=rgb(foreground))++blended;
            for(std::size_t channel=0;channel<value.size();++channel)
                check(value[channel]>=rgb(foreground)[channel]&&value[channel]<=rgb(background)[channel],
                    "Default font ignored custom text/background colors while blending");
        }
        check(painted>0&&blended>0,"Default font lost antialiased coverage at a supported display scale");
    }
    const auto empty=render(""),regular=render("Aa"),bold=render("Aa",true);
    const auto plain=render("e"),latin=render("é"),replacement=render("�"),unsupported=render("🙂");
    check(*regular.pixels!=*empty.pixels&&*bold.pixels!=*regular.pixels,"Default font did not render distinct regular and bold faces");
    check(*latin.pixels!=*empty.pixels&&*latin.pixels!=*plain.pixels&&*latin.pixels!=*replacement.pixels,
        "Supported Latin-1 glyphs lost their accented appearance");
    check(*replacement.pixels!=*empty.pixels&&*replacement.pixels!=*render("?").pixels&&*unsupported.pixels==*replacement.pixels,
        "Unsupported text did not render a visible replacement glyph");
    gui::FramebufferAdapter adapter;
    const auto metrics=[&](const std::string& value) {return adapter.measure_text({value,{},160,1,gui::TextWrap::none});};
    check(metrics("é").width==metrics("e").width&&metrics("🙂").width==metrics("�").width,
        "Default font measured UTF-8 bytes instead of displayed glyphs");
    check(metrics("é\nÑ").height==2*metrics("é").height,"Default font measurements lost explicit line breaks");
}
void label_word_wrap() {
    for(const double scale:{1.0,1.25,1.5,2.0}) {
        gui::FramebufferAdapter adapter;
        const auto single=adapter.measure_text({"aa",{},80,scale,gui::TextWrap::none});
        const auto expected=adapter.measure_text({"aa\nbb",{},single.width,scale,gui::TextWrap::none});
        const auto render=[&](const std::string& text,gui::TextWrap wrap) {
            gui::Snapshot view;view.client_size={80,96};view.display_scale=scale;
            gui::Widget label;label.spec.key.id="wrapped-label";label.spec.kind=gui::Kind::label;
            label.state.bounds={4,4,single.width,3*single.height};label.state.text=text;label.state.wrap=wrap;
            view.widgets.push_back(label);adapter.present(view);return adapter.frame();
        };
        const auto explicit_lines=render("aa\nbb",gui::TextWrap::none);
        for(const std::string text:{"aa bb","aa\tbb"}) {
            const auto measured=adapter.measure_text({text,{},single.width,scale,gui::TextWrap::word});
            check(measured==expected&&measured.height==2*single.height,
                "Wrapping at an exact word width added a separator-only line");
            check(*render(text,gui::TextWrap::word).pixels==*explicit_lines.pixels,
                "Soft word wrapping painted differently from the equivalent explicit lines");
        }
        const auto glyph=adapter.measure_text({"a",{},80,scale,gui::TextWrap::none});
        check(adapter.measure_text({"ab",{},0,scale,gui::TextWrap::word})==gui::Size{glyph.width,2*glyph.height},
            "A zero-width label failed to make progress through an overlong word");
    }
}
void antialiased_text_clipping() {
    for(const double scale:{1.25,1.5,2.0}) {
        gui::FramebufferAdapter adapter;gui::Snapshot view;view.client_size={80,60};view.display_scale=scale;
        gui::Widget parent;parent.spec.key.id="clip-parent";parent.spec.kind=gui::Kind::group;
        parent.state.bounds={2,2,72,50};parent.state.content_clip=gui::Rect{15.25,13.5,23.5,16.25};view.widgets.push_back(parent);
        adapter.present(view);const auto before=adapter.frame();
        gui::Widget label;label.spec.key.id="clipped-text";label.spec.parent=parent.spec.key.id;label.spec.kind=gui::Kind::label;
        label.state.bounds={6,6,64,36};label.state.text="MMMMMMMM\nMMMMMMMM";label.state.font.size=18;view.widgets.push_back(label);
        adapter.present(view);const auto after=adapter.frame();
        const auto content=*parent.state.content_clip;
        const auto clip=gui::device_rect({parent.state.bounds.x+content.x,parent.state.bounds.y+content.y,content.width,content.height},scale);
        std::size_t changed=0;
        for(unsigned y=0;y<after.height;++y)for(unsigned x=0;x<after.width;++x)if(pixel(before,x,y)!=pixel(after,x,y)) {
            ++changed;check(contains(clip,x,y),"Antialiased glyph pixels escaped a fractional ancestor clip");
        }
        check(changed>0,"Ancestor clipping discarded all visible glyph pixels");
    }
}
void wrapped_editor_output() {
    for(const double scale:{1.0,1.25,1.5,2.0}) {
        gui::FramebufferAdapter adapter;gui::Snapshot view;view.client_size={120,110};view.display_scale=scale;
        gui::Widget editor;editor.spec.key.id="wrapped-text";editor.spec.kind=gui::Kind::text;editor.spec.text_policy.multiline=true;
        editor.state.bounds={5,5,42,84};editor.state.text="ab cd ef";editor.state.wrap=gui::TextWrap::word;view.widgets.push_back(editor);adapter.present(view);
        adapter.focus(editor.spec.key);adapter.text_selection(editor.spec.key,{8,8});
        const auto lines=adapter.editor_lines(editor);check(lines.size()>=3,"Narrow editor did not use shared word-wrap lines");
        const auto frame=adapter.frame();const auto content=adapter.text_bounds(editor);const auto height=adapter.line_height(editor.state.font);
        for(std::size_t row=0;row<lines.size();++row) {
            const auto area=gui::device_rect({content.x,content.y+double(row)*height,content.width,height},scale);std::size_t ink=0;
            for(unsigned y=unsigned(area.y);y<unsigned(area.y)+area.height;++y)
                for(unsigned x=unsigned(area.x);x<unsigned(area.x)+area.width;++x) {
                    const auto value=pixel(frame,x,y);
                    if(value!=rgb(view.palette.surface)&&value!=rgb(view.palette.accent))++ink;
                }
            check(ink>0,"Software editor omitted a wrapped visual line");
            const auto line=lines[row];
            const auto measured=adapter.measure_text({editor.state.text.substr(line.begin,line.end-line.begin),editor.state.font,content.width,scale,gui::TextWrap::none});
            const auto position=adapter.editor_caret_position(editor,line.end);
            check(position==gui::Point{measured.width,double(row)*height},"Wrapped caret geometry disagreed with font measurements");
            adapter.text_selection(editor.spec.key,{line.begin,line.end});const auto selected=adapter.frame();
            const auto caret=gui::device_rect({content.x+position.x,content.y+position.y,1/scale,height},scale);
            check(pixel(selected,unsigned(caret.x),unsigned(caret.y)+caret.height/2)==rgb(view.palette.accent),
                "Rendered caret did not follow measured wrapped text");
            const auto selection=gui::device_rect({content.x,content.y+double(row)*height,measured.width,height},scale);
            std::size_t selected_pixels=0;bool reaches_end=false;
            for(unsigned y=0;y<selected.height;++y)for(unsigned x=0;x<selected.width;++x)if(pixel(selected,x,y)==rgb(view.palette.selection)) {
                ++selected_pixels;check(contains(selection,x,y),"Editor selection escaped its measured wrapped line");
                if(int(x)>=selection.x+int(selection.width)-2)reaches_end=true;
            }
            check(selected_pixels>0&&reaches_end,"Editor selection did not cover the measured text width");
            const auto first=adapter.measure_text({editor.state.text.substr(line.begin,1),editor.state.font,content.width,scale,gui::TextWrap::none});
            adapter.pointer({gui::PointerKind::click,{content.x+first.width,content.y+(double(row)+0.5)*height}});
            check(adapter.text_selection(editor.spec.key).caret==line.begin+1,"Pointer placement disagreed with measured wrapped glyph positions");
        }
        check(adapter.editor_caret_position(editor,8).y>0,"Wrapped editor caret remained on first row");
    }
}
void budget() {
    gui::FramebufferAdapter adapter({},1024);gui::Snapshot view;view.client_size={30,30};bool threw=false;
    try {adapter.present(view);}catch(const std::length_error&) {threw=true;}
    check(threw&&adapter.snapshot().revision==0,"Frame budget was not enforced before presentation");
    view.client_size={10,10};adapter.present(view);check(adapter.frame().pixels->size()==300,"Budget failure prevented smaller recovery");
}
}
int main() {
    try {output_and_input();lifetime_and_theme();clipping_scale_and_formats();modal_and_prompt_output();prompt_chrome_and_input();custom_font_and_paint_guards();default_font_output();label_word_wrap();antialiased_text_clipping();wrapped_editor_output();budget();std::cout<<"Framebuffer rendering, input, damage and ownership passed\n";}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
