#pragma once
#include "interaction.hpp"
#include "runtime.hpp"
#include <array>
#include <limits>
#include <memory>
#include <string_view>

namespace gui {
// A complete RGB24 image with immutable shared ownership. Consumers may retain
// it across redraws, resize, close and adapter destruction. damage is applicable
// only to base_revision; frame(last_seen) returns full damage after skipped frames.
struct Frame {
    unsigned width=0,height=0;
    std::size_t stride_bytes=0;
    std::uint64_t revision=0,base_revision=0;
    PixelRect damage;
    std::shared_ptr<const std::vector<std::uint8_t>> pixels;
    PixelBlock block() const {
        return {width,height,stride_bytes,PixelFormat::rgb24,pixels?std::span<const std::uint8_t>(*pixels):std::span<const std::uint8_t>{}};
    }
};
// Optional host font extension. Both callbacks must be supplied together and
// describe the same renderer. Paint receives original UTF-8, including shaping
// characters, and emits logical colored rectangles through a synchronous sink.
// The adapter clips, copies and owns resulting pixels. Callbacks must not retain
// borrowed requests/sinks or mutate the adapter. A host may capture owned font
// data and use any rasterizer without changing the application declarations.
struct FramebufferTextRequest {
    std::string_view text;
    Rect bounds;
    Font font;
    TextWrap wrap=TextWrap::none;
    double display_scale=1;
    Color color;
};
using FramebufferTextSink=std::function<void(Rect,Color)>;
struct FramebufferTextRenderer {
    TextMeasure measure;
    std::function<void(const FramebufferTextRequest&,const FramebufferTextSink&)> paint;
};

namespace framebuffer_detail {
using Color=std::array<std::uint8_t,3>;
// Small original 5x7 demonstration alphabet. Unsupported Unicode codepoints
// use ?. This is deliberately not a shaping or
// accessibility engine. Input and application text remain intact UTF-8.
inline std::array<unsigned,7> glyph(char c) {
    switch(c) {
    case 'a':return {0,0,14,1,15,17,15};case 'b':return {16,16,30,17,17,17,30};
    case 'c':return {0,0,14,16,16,17,14};case 'd':return {1,1,15,17,17,17,15};
    case 'e':return {0,0,14,17,31,16,14};case 'f':return {6,9,8,28,8,8,8};
    case 'g':return {0,15,17,17,15,1,14};case 'h':return {16,16,30,17,17,17,17};
    case 'i':return {4,0,12,4,4,4,14};case 'j':return {2,0,6,2,2,18,12};
    case 'k':return {16,16,18,20,24,20,18};case 'l':return {12,4,4,4,4,4,14};
    case 'm':return {0,0,26,21,21,21,21};case 'n':return {0,0,30,17,17,17,17};
    case 'o':return {0,0,14,17,17,17,14};case 'p':return {0,0,30,17,30,16,16};
    case 'q':return {0,0,15,17,15,1,1};case 'r':return {0,0,22,25,16,16,16};
    case 's':return {0,0,15,16,14,1,30};case 't':return {8,8,28,8,8,9,6};
    case 'u':return {0,0,17,17,17,19,13};case 'v':return {0,0,17,17,17,10,4};
    case 'w':return {0,0,17,17,21,21,10};case 'x':return {0,0,17,10,4,10,17};
    case 'y':return {0,0,17,17,15,1,14};case 'z':return {0,0,31,2,4,8,31};
    case 'A':return {14,17,17,31,17,17,17};case 'B':return {30,17,17,30,17,17,30};
    case 'C':return {14,17,16,16,16,17,14};case 'D':return {30,17,17,17,17,17,30};
    case 'E':return {31,16,16,30,16,16,31};case 'F':return {31,16,16,30,16,16,16};
    case 'G':return {14,17,16,23,17,17,15};case 'H':return {17,17,17,31,17,17,17};
    case 'I':return {14,4,4,4,4,4,14};case 'J':return {7,2,2,2,2,18,12};
    case 'K':return {17,18,20,24,20,18,17};case 'L':return {16,16,16,16,16,16,31};
    case 'M':return {17,27,21,21,17,17,17};case 'N':return {17,25,25,21,19,19,17};
    case 'O':return {14,17,17,17,17,17,14};case 'P':return {30,17,17,30,16,16,16};
    case 'Q':return {14,17,17,17,21,18,13};case 'R':return {30,17,17,30,20,18,17};
    case 'S':return {15,16,16,14,1,1,30};case 'T':return {31,4,4,4,4,4,4};
    case 'U':return {17,17,17,17,17,17,14};case 'V':return {17,17,17,17,17,10,4};
    case 'W':return {17,17,17,21,21,27,17};case 'X':return {17,17,10,4,10,17,17};
    case 'Y':return {17,17,10,4,4,4,4};case 'Z':return {31,1,2,4,8,16,31};
    case '0':return {14,17,19,21,25,17,14};case '1':return {4,12,4,4,4,4,14};
    case '2':return {14,17,1,2,4,8,31};case '3':return {30,1,1,14,1,1,30};
    case '4':return {2,6,10,18,31,2,2};case '5':return {31,16,16,30,1,1,30};
    case '6':return {14,16,16,30,17,17,14};case '7':return {31,1,2,4,8,8,8};
    case '8':return {14,17,17,14,17,17,14};case '9':return {14,17,17,15,1,1,14};
    case ' ':return {};case '.':return {0,0,0,0,0,6,6};case ',':return {0,0,0,0,6,6,4};
    case ':':return {0,6,6,0,6,6,0};case ';':return {0,6,6,0,6,6,4};
    case '-':return {0,0,0,31,0,0,0};case '_':return {0,0,0,0,0,0,31};
    case '+':return {0,4,4,31,4,4,0};case '=':return {0,0,31,0,31,0,0};
    case '/':return {1,2,2,4,8,8,16};case '\\':return {16,8,8,4,2,2,1};
    case '[':return {14,8,8,8,8,8,14};case ']':return {14,2,2,2,2,2,14};
    case '(':return {2,4,8,8,8,4,2};case ')':return {8,4,2,2,2,4,8};
    case '<':return {1,2,4,8,4,2,1};case '>':return {16,8,4,2,4,8,16};
    case '!':return {4,4,4,4,4,0,4};case '?':return {14,17,1,2,4,0,4};
    case '"':return {10,10,10,0,0,0,0};case '\'':return {4,4,4,0,0,0,0};
    case '*':return {0,21,14,31,14,21,0};case '#':return {10,31,10,10,31,10,0};
    case '%':return {25,25,2,4,8,19,19};case '&':return {12,18,20,8,21,18,13};
    case '@':return {14,17,23,21,23,16,14};case '|':return {4,4,4,4,4,4,4};
    case '^':return {4,10,17,0,0,0,0};case '~':return {0,0,8,21,2,0,0};
    case '$':return {4,15,20,14,5,30,4};case '`':return {8,4,0,0,0,0,0};
    case '{':return {2,4,4,8,4,4,2};case '}':return {8,4,4,2,4,4,8};
    default:return {14,17,1,2,4,0,4};
    }
}
inline std::size_t next(std::string_view value,std::size_t at) {
    if(at>=value.size())return value.size();
    ++at;while(at<value.size()&&(static_cast<unsigned char>(value[at])&0xc0)==0x80)++at;
    return at;
}
inline std::size_t previous(std::string_view value,std::size_t at) {
    return at==0?0:text_boundary(value,at-1);
}
inline std::size_t count(std::string_view value) {
    std::size_t n=0;for(std::size_t i=0;i<value.size();i=next(value,i))++n;return n;
}
inline std::string display(std::string_view value) {
    std::string out;
    for(std::size_t i=0;i<value.size();i=next(value,i)) {
        const auto c=static_cast<unsigned char>(value[i]);
        if(c=='\t')out.append(4,' ');
        else if(c=='\r')continue;
        else out.push_back(c<128?char(c):'?');
    }
    return out;
}
inline std::vector<std::string> lines(std::string_view value,std::size_t columns,TextWrap wrap) {
    std::vector<std::string> out;
    const auto text=display(value);std::size_t begin=0;
    for(;;) {
        const auto end=text.find('\n',begin);
        auto line=text.substr(begin,end==std::string::npos?std::string::npos:end-begin);
        while(wrap==TextWrap::word&&line.size()>columns&&columns>0) {
            auto split=line.rfind(' ',columns);
            if(split==std::string::npos||split==0)split=columns;
            out.push_back(line.substr(0,split));
            if(split<line.size()&&line[split]==' ')++split;
            line.erase(0,split);
        }
        out.push_back(std::move(line));
        if(end==std::string::npos)break;
        begin=end+1;
    }
    return out;
}
inline double unit(const Font& font,double scale) {return std::max(1.0,std::round(font.size*scale/8.0))/scale;}
inline Size measure(const TextMeasureRequest& request) {
    validate_measure_request(request);
    const double u=unit(request.font,request.display_scale);
    const auto columns=static_cast<std::size_t>(std::max(1.0,std::floor(request.available_width/(6*u))));
    const auto rows=lines(request.text,columns,request.wrap);
    std::size_t width=0;for(const auto& row:rows)width=std::max(width,row.size());
    return {double(width)*6*u,double(rows.size())*8*u};
}
}

// Software pixels independent of an OS window or browser. Input and transient
// control state are inherited from the same engine used by the terminal profile.
class FramebufferAdapter final : public InteractiveAdapter {
public:
    explicit FramebufferAdapter(EventSink sink={},std::size_t frame_budget=64*1024*1024,FramebufferTextRenderer text_renderer={})
        :InteractiveAdapter(std::move(sink),text_renderer.measure?text_renderer.measure:framebuffer_detail::measure),
            frame_budget_(frame_budget),text_renderer_(std::move(text_renderer)) {
        if(bool(text_renderer_.measure)!=bool(text_renderer_.paint))throw std::invalid_argument("Text provider requires measurement and painting together");
    }
    void present(Snapshot view) override {
        const auto grid=device_rect({0,0,view.client_size.width,view.client_size.height},view.display_scale);
        if(bitmap_detail::multiply(pixel_row_bytes(grid.width,PixelFormat::rgb24),grid.height)>frame_budget_)
            throw std::length_error("Framebuffer byte budget exceeded");
        InteractiveAdapter::present(std::move(view));
    }
    Frame frame(std::uint64_t last_seen_revision=0) {
        if(closed())return previous_;
        if(rendering_)throw std::logic_error("Recursive framebuffer rendering");
        policy().require_interaction();
        struct Rendering {bool& flag;explicit Rendering(bool& value):flag(value){flag=true;}~Rendering(){flag=false;}} rendering(rendering_);
        const auto& view=snapshot();
        scale_=view.display_scale;
        const auto rgb=[](gui::Color color){return framebuffer_detail::Color{color.red,color.green,color.blue};};
        background=rgb(view.palette.background);panel=rgb(view.palette.surface);control_color=panel;
        ink=rgb(view.palette.text);muted=rgb(view.palette.muted);accent=rgb(view.palette.accent);
        border_color=rgb(view.palette.border);selection_color=rgb(view.palette.selection);
        disabled_color=rgb(view.palette.disabled);error_color=rgb(view.palette.error);
        const auto grid=device_rect({0,0,view.client_size.width,
            view.client_size.height},scale_);
        const auto stride=pixel_row_bytes(grid.width,PixelFormat::rgb24);
        const auto bytes=bitmap_detail::multiply(stride,grid.height);
        if(bytes>frame_budget_)throw std::length_error("Framebuffer byte budget exceeded");
        width_=grid.width;height_=grid.height;stride_=stride;
        canvas_.assign(bytes,0);
        clip_={0,0,double(width_)/scale_,double(height_)/scale_};
        fill(clip_,background);
        for(const auto& key:paint_order(view))paint(*find_widget(view,key));
        paint_pages();paint_popup();paint_prompt();
        PixelRect damage{0,0,width_,height_};
        const bool compatible=previous_.pixels&&previous_.width==width_&&previous_.height==height_;
        bool changed=!compatible;
        if(compatible) {
            unsigned left=width_,top=height_,right=0,bottom=0;
            for(unsigned y=0;y<height_;++y)for(unsigned x=0;x<width_;++x) {
                const auto at=std::size_t(y)*stride_+std::size_t(x)*3;
                if(!std::equal(canvas_.begin()+static_cast<std::ptrdiff_t>(at),
                    canvas_.begin()+static_cast<std::ptrdiff_t>(at+3),previous_.pixels->begin()+static_cast<std::ptrdiff_t>(at))) {
                    changed=true;left=std::min(left,x);top=std::min(top,y);right=std::max(right,x+1);bottom=std::max(bottom,y+1);
                }
            }
            if(changed)damage={left,top,right-left,bottom-top};
        }
        if(changed) {
            if(previous_.revision==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("Frame revision exhausted");
            const auto old=previous_.revision;
            previous_={width_,height_,stride_,old+1,compatible?old:0,damage,
                std::make_shared<const std::vector<std::uint8_t>>(std::move(canvas_))};
        }
        auto result=previous_;
        if(last_seen_revision==result.revision) {result.damage={};result.base_revision=result.revision;}
        else if(last_seen_revision!=result.base_revision) {result.damage={0,0,result.width,result.height};result.base_revision=0;}
        return result;
    }
private:
    framebuffer_detail::Color background{},panel{},control_color{},ink{},muted{},accent{},border_color{},selection_color{},disabled_color{},error_color{};
    bool rendering_=false;
    std::size_t frame_budget_;
    FramebufferTextRenderer text_renderer_;
    Frame previous_;
    std::vector<std::uint8_t> canvas_;
    unsigned width_=0,height_=0;
    std::size_t stride_=0;
    double scale_=1;
    Rect clip_;
    void fill(Rect box,framebuffer_detail::Color color) {
        box=intersect(box,clip_);if(!has_area(box))return;
        const auto r=device_rect(box,scale_);
        const int right=std::min(int(width_),r.x+int(r.width)),bottom=std::min(int(height_),r.y+int(r.height));
        for(int y=std::max(0,r.y);y<bottom;++y)for(int x=std::max(0,r.x);x<right;++x) {
            const auto offset=std::size_t(y)*stride_+std::size_t(x)*3;
            std::copy(color.begin(),color.end(),canvas_.begin()+static_cast<std::ptrdiff_t>(offset));
        }
    }
    void outline(Rect box,framebuffer_detail::Color color,double thickness=1) {
        fill({box.x,box.y,box.width,std::min(thickness,box.height)},color);
        fill({box.x,box.y, std::min(thickness,box.width),box.height},color);
        fill({box.x,std::max(box.y,box.y+box.height-thickness),box.width,std::min(thickness,box.height)},color);
        fill({std::max(box.x,box.x+box.width-thickness),box.y,std::min(thickness,box.width),box.height},color);
    }
    void draw_text(std::string_view value,Rect box,const Font& font={},TextWrap wrap=TextWrap::none,
                   std::optional<framebuffer_detail::Color> color={}) {
        const auto saved=clip_;clip_=intersect(clip_,box);
        const double u=framebuffer_detail::unit(font,scale_);
        const auto columns=static_cast<std::size_t>(std::max(1.0,std::floor(box.width/(6*u))));
        const auto rows=framebuffer_detail::lines(value,columns,wrap);
        const auto foreground=color.value_or(font.tone==Tone::muted?muted:font.tone==Tone::accent?accent:
            font.tone==Tone::error?error_color:ink);
        if(text_renderer_.paint) {
            policy().paint_ui([&] {
                text_renderer_.paint({value,box,font,wrap,scale_,{foreground[0],foreground[1],foreground[2]}},
                    [&](Rect pixels,gui::Color color) {
                        if(!valid_rect(pixels))throw std::invalid_argument("Invalid glyph raster rectangle");
                        fill(pixels,{color.red,color.green,color.blue});
                    });
            });
            clip_=saved;return;
        }
        double y=box.y;
        for(const auto& row:rows) {
            if(y>=box.y+box.height)break;
            double x=box.x;
            for(const char c:row) {
                if(x>=box.x+box.width)break;
                const auto g=framebuffer_detail::glyph(c);
                for(unsigned gy=0;gy<7;++gy)for(unsigned gx=0;gx<5;++gx)
                    if(g[gy]&(1u<<(4-gx)))fill({x+gx*u,y+gy*u,u+(font.bold?u/2:0),u},foreground);
                x+=6*u;
            }
            y+=8*u;
        }
        clip_=saved;
    }
    void centered_text(std::string_view text,Rect box,const Font& font,framebuffer_detail::Color color) {
        const auto measured=measure_text({std::string(text),font,box.width,scale_,TextWrap::none});
        const double inset=std::max(0.0,(box.width-measured.width)/2);
        box.x+=inset;box.width=std::max(0.0,box.width-inset);
        draw_text(text,box,font,TextWrap::none,color);
    }
    void paint(const Widget& widget) {
        const auto a=resolved_availability(widget.spec.key);if(!a.visible)return;
        const auto saved=clip_;clip_=a.clip;
        const auto& state=widget.state;const auto box=a.bounds;
        const auto color=a.enabled?ink:muted;
        const auto text_box=Rect{box.x+6,box.y+std::max(2.0,(box.height-line_height(state.font))/2),
            std::max(0.0,box.width-12),std::max(0.0,box.height-4)};
        switch(widget.spec.kind) {
        case Kind::group:fill(box,panel);outline(box,border_color);break;
        case Kind::label:draw_text(visible_text(widget),box,state.font,state.wrap,a.enabled?std::nullopt:std::optional{muted});break;
        case Kind::button:fill(box,a.enabled?control_color:disabled_color);outline(box,border_color);centered_text(state.label,text_box,state.font,color);break;
        case Kind::toggle: {
            fill(box,a.enabled?control_color:disabled_color);const Rect mark{box.x+5,box.y+5,18,18};outline(mark,a.enabled?accent:muted);
            if(state.checked)draw_text("X",{mark.x+3,mark.y+1,16,16},{12},TextWrap::none,color);
            auto label=text_box;label.x+=24;label.width=std::max(0.0,label.width-24);draw_text(state.label,label,state.font,TextWrap::none,color);break;
        }
        case Kind::choice:case Kind::menu: {
            fill(box,a.enabled?control_color:disabled_color);outline(box,border_color);const auto label=visible_text(widget);
            auto content=text_box;content.width=std::max(0.0,content.width-20);
            draw_text(label,content,state.font,TextWrap::none,color);draw_text("v",{box.x+box.width-20,text_box.y,16,20},{12},TextWrap::none,color);break;
        }
        case Kind::text: {
            fill(box,background);outline(box,border_color);
            const auto offset=scroll_offset(widget.spec.key);auto content=text_bounds(widget);content.x-=offset.x;content.y-=offset.y;content.width+=offset.x;content.height+=offset.y;
            clip_=intersect(clip_,{box.x+3,box.y+2,std::max(0.0,box.width-6),std::max(0.0,box.height-4)});
            const auto selection=text_selection(widget.spec.key);
            const double row_height=line_height(state.font);
            const auto text_width=[&](std::size_t start,std::size_t length) {
                return measure_text({state.text.substr(start,length),state.font,coordinate_limit,scale_,TextWrap::none}).width;
            };
            const auto lines=editor_lines(widget);
            for(std::size_t row=0;row<lines.size();++row) {
                const auto line=lines[row];const double y=content.y+double(row)*row_height;
                if(y+row_height<=clip_.y||y>=clip_.y+clip_.height)continue;
                if(focused()==widget.spec.key&&selection.anchor!=selection.caret) {
                    const auto first=std::min(selection.anchor,selection.caret),last=std::max(selection.anchor,selection.caret);
                    const auto left=std::clamp(first,line.begin,line.end),right=std::clamp(last,line.begin,line.end);
                    if(right>left)fill({content.x+text_width(line.begin,left-line.begin),y,text_width(left,right-left),row_height},selection_color);
                }
                if(!state.text.empty())draw_text(std::string_view(state.text).substr(line.begin,line.end-line.begin),
                    {content.x,y,content.width,row_height},state.font,TextWrap::none,color);
            }
            if(state.text.empty())draw_text(state.placeholder,content,state.font,state.wrap,muted);
            if(focused()==widget.spec.key&&!widget.spec.text_policy.read_only) {
                const auto position=editor_caret_position(widget,selection.caret);
                fill({content.x+position.x,content.y+position.y,1/scale_,row_height},accent);
            }
            break;
        }
        case Kind::list: {
            fill(box,background);outline(box,border_color);const auto offset=scroll_offset(widget.spec.key);
            if(state.records.empty())draw_text(state.placeholder,{box.x+4,box.y+4,std::max(0.0,box.width-8),std::max(0.0,box.height-8)},state.font,TextWrap::none,muted);
            for(std::size_t i=0;i<state.records.size();++i) {
                const auto& row=state.records[i];const double y=box.y+double(i)*widget.spec.row_height-offset.y;
                if(y+widget.spec.row_height<=a.clip.y||y>=a.clip.y+a.clip.height)continue;
                if(state.selected==row.id)fill({box.x,y,box.width,widget.spec.row_height},selection_color);
                if(row.cells.empty())draw_text(row.accessible_text,{box.x+4,y+4,std::max(0.0,box.width-8),widget.spec.row_height},state.font,TextWrap::none,row.enabled?color:muted);
                for(const auto& cell:row.cells)draw_text(cell.text,{box.x+cell.bounds.x-offset.x,y+cell.bounds.y,cell.bounds.width,cell.bounds.height},cell.font,cell.wrap,row.enabled?std::nullopt:std::optional{muted});
            }
            break;
        }
        case Kind::bitmap: {
            policy().repaint(widget.spec.key);const auto block=policy().image(widget.spec.key).block();
            const auto destination=device_rect(box,scale_),clip=device_rect(a.clip,scale_);
            const int left=std::max(0,clip.x),top=std::max(0,clip.y);
            const int right=std::min(int(width_),clip.x+int(clip.width)),bottom=std::min(int(height_),clip.y+int(clip.height));
            for(int y=top;y<bottom;++y)for(int x=left;x<right;++x) {
                const auto px=unsigned(x-destination.x),py=unsigned(y-destination.y);
                if(px>=block.width||py>=block.height)continue;
                const auto rgb=bitmap_detail::read(block.bytes.data()+std::size_t(py)*block.stride_bytes,px,block.format);
                std::copy(rgb.begin(),rgb.end(),canvas_.begin()+static_cast<std::ptrdiff_t>(std::size_t(y)*stride_+std::size_t(x)*3));
            }
            break;
        }
        }
        clip_=a.clip;if(focused()==widget.spec.key)outline(box,accent,2);
        clip_=saved;
    }
    void paint_pages() {
        for(const auto& [page,area]:page_bounds()) {
            fill(area,snapshot().active_page==page.id?control_color:background);
            outline(area,snapshot().active_page==page.id?accent:border_color);
            centered_text(page.label,{area.x+6,area.y+std::max(0.0,(area.height-line_height({12}))/2),std::max(0.0,area.width-12),area.height},{12},page.enabled?ink:muted);
        }
    }
    void paint_popup() {
        if(!popup())return;
        const auto box=popup_bounds();fill(box,control_color);outline(box,accent);
        for(std::size_t i=0;i<popup()->options.size();++i) {
            const Rect row{box.x,box.y+double(i)*popup_row_height(),box.width,popup_row_height()};
            if(i==popup()->index)fill(row,selection_color);
            draw_text(popup()->options[i].label,{row.x+6,row.y+4,std::max(0.0,row.width-12),std::max(0.0,row.height-4)}, {12},TextWrap::none,popup()->options[i].enabled?ink:muted);
        }
    }
    void paint_prompt() {
        if(!prompt())return;
        const auto box=prompt_bounds();fill(box,panel);outline(box,accent,2);
        draw_text(prompt()->title,{box.x+12,box.y+12,std::max(0.0,box.width-24),24},{14,true});
        const auto field=prompt_field_bounds();fill(field,background);outline(field,accent);
        draw_text(prompt()->value,{field.x+4,field.y+6,std::max(0.0,field.width-8),20},{14});
        const double caret=field.x+4+measure_text({prompt()->value.substr(0,prompt_selection().caret),{14},coordinate_limit,scale_,TextWrap::none}).width;
        if(caret<field.x+field.width-2)fill({caret,field.y+5,1,17},accent);
        draw_text("Cancel",{box.x+16,box.y+box.height-30,90,24},{14});
        draw_text("OK",{box.x+box.width-90,box.y+box.height-30,78,24},{14});
    }
};
}
