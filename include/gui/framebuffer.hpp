#pragma once
#include "interaction.hpp"
#include "detail/framebuffer_font_data.hpp"
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
// Measurement and paint use the same retained font data, including the same
// hinted device size. Hosts need neither a font file nor a font rasterizer.
struct FontMetrics {
    const framebuffer_font::Face& face;
    double unit;
    double height() const {return face.line_height*unit;}
    double advance(char32_t codepoint) const {
        return codepoint==U'\t'?4*framebuffer_font::glyph(face,U' ').advance*unit:
            framebuffer_font::glyph(face,codepoint).advance*unit;
    }
};
inline FontMetrics metrics(const Font& font,double scale) {
    const auto pixels=std::max(1u,static_cast<unsigned>(font.size*scale));
    const auto& face=framebuffer_font::face(pixels,font.bold);
    return {face,double(pixels)/face.pixels/scale};
}
inline std::size_t next(std::string_view value,std::size_t at) {
    if(at>=value.size())return value.size();
    ++at;while(at<value.size()&&(static_cast<unsigned char>(value[at])&0xc0)==0x80)++at;
    return at;
}
inline std::size_t count(std::string_view value) {
    std::size_t n=0;for(std::size_t i=0;i<value.size();i=next(value,i))++n;return n;
}
inline std::u32string display(std::string_view value) {
    std::u32string out;
    for(std::size_t i=0;i<value.size();) {
        const auto first=static_cast<unsigned char>(value[i++]);
        char32_t codepoint=first;
        if(first>=128) {
            const unsigned continuation=first<0xe0?1:first<0xf0?2:3;
            codepoint=first&((1u<<(6-continuation))-1);
            for(unsigned n=0;n<continuation;++n)codepoint=(codepoint<<6)|(static_cast<unsigned char>(value[i++])&63);
        }
        if(codepoint!='\r')out.push_back(codepoint);
    }
    return out;
}
struct TextLine {std::u32string text;double width;};
inline std::vector<TextLine> lines(std::string_view value,const FontMetrics& font,double width,TextWrap wrap) {
    std::vector<TextLine> out;
    const auto text=display(value);
    for(std::size_t begin=0;;) {
        const auto newline=text.find(U'\n',begin);
        const auto end=newline==std::u32string::npos?text.size():newline;
        if(begin==end)out.push_back({{},0});
        while(begin<end) {
            auto at=begin,last_space=std::u32string::npos;double advance=0,space_width=0;
            while(at<end) {
                const auto next_width=advance+font.advance(text[at]);
                if((text[at]==U' '||text[at]==U'\t')&&at>begin){last_space=at;space_width=advance;}
                if(wrap==TextWrap::word&&at>begin&&next_width>width)break;
                advance=next_width;++at;
            }
            if(at<end&&last_space!=std::u32string::npos) {
                out.push_back({text.substr(begin,last_space-begin),space_width});begin=last_space+1;
            } else {out.push_back({text.substr(begin,at-begin),advance});begin=at;}
        }
        if(newline==std::u32string::npos)break;
        begin=newline+1;
    }
    return out;
}
inline Size measure(const TextMeasureRequest& request) {
    validate_measure_request(request);
    const auto font=metrics(request.font,request.display_scale);
    const auto rows=lines(request.text,font,request.available_width,request.wrap);
    double width=0;for(const auto& row:rows)width=std::max(width,row.width);
    return {width,double(rows.size())*font.height()};
}
}

// Software pixels independent of an OS window or browser. Input and transient
// control state are inherited from the same engine used by the terminal profile.
class FramebufferAdapter final : public InteractiveAdapter {
public:
    explicit FramebufferAdapter(EventSink sink={},std::size_t frame_budget=64*1024*1024,FramebufferTextRenderer text_renderer={})
        :InteractiveAdapter(std::move(sink),text_renderer.measure?text_renderer.measure:framebuffer_detail::measure,4),
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
    void blend(int x,int y,framebuffer_detail::Color color,unsigned coverage) {
        const auto at=std::size_t(y)*stride_+std::size_t(x)*3;
        for(unsigned channel=0;channel<3;++channel)
            canvas_[at+channel]=static_cast<std::uint8_t>((color[channel]*coverage+canvas_[at+channel]*(255-coverage)+127)/255);
    }
    void draw_glyph(const framebuffer_font::Glyph& glyph,double x,double y,
                    const framebuffer_detail::FontMetrics& font,framebuffer_detail::Color color) {
        if(!glyph.width||!glyph.height)return;
        const double left=(x+glyph.left*font.unit)*scale_;
        const double top=(y+(double(font.face.ascent)-glyph.top)*font.unit)*scale_;
        const double ratio=font.unit*scale_;
        const auto clip=device_rect(clip_,scale_);
        // Interpolate coverage, including transparent texels around the glyph,
        // so fractional positioning and sizes keep smooth edges. At a retained
        // integer size and origin these are the original hinted font pixels.
        const int x0=std::max({0,clip.x,int(std::floor(left-ratio/2))});
        const int y0=std::max({0,clip.y,int(std::floor(top-ratio/2))});
        const int x1=std::min({int(width_),clip.x+int(clip.width),int(std::ceil(left+(glyph.width+.5)*ratio))});
        const int y1=std::min({int(height_),clip.y+int(clip.height),int(std::ceil(top+(glyph.height+.5)*ratio))});
        if(x0>=x1||y0>=y1)return;
        const auto pixels=framebuffer_font::raster(glyph);
        const auto sample=[&](int sx,int sy)->double {
            return sx>=0&&sy>=0&&sx<glyph.width&&sy<glyph.height?pixels[std::size_t(sy)*glyph.width+unsigned(sx)]:0;
        };
        for(int py=y0;py<y1;++py)for(int px=x0;px<x1;++px) {
            const double sx=(px+.5-left)/ratio-.5,sy=(py+.5-top)/ratio-.5;
            const int ix=int(std::floor(sx)),iy=int(std::floor(sy));
            const double fx=sx-ix,fy=sy-iy;
            const auto coverage=static_cast<unsigned>(std::round(
                (sample(ix,iy)*(1-fx)+sample(ix+1,iy)*fx)*(1-fy)+
                (sample(ix,iy+1)*(1-fx)+sample(ix+1,iy+1)*fx)*fy));
            if(coverage)blend(px,py,color,coverage);
        }
    }
    void stroke(Point a,Point b,Point c,framebuffer_detail::Color color,double thickness=1.4) {
        const double radius=thickness/2;
        const auto bounds=intersect(clip_,{std::min({a.x,b.x,c.x})-radius,std::min({a.y,b.y,c.y})-radius,
            std::max({a.x,b.x,c.x})-std::min({a.x,b.x,c.x})+thickness,
            std::max({a.y,b.y,c.y})-std::min({a.y,b.y,c.y})+thickness});
        if(!has_area(bounds))return;
        const auto rect=device_rect(bounds,scale_);
        const auto distance=[](Point p,Point from,Point to) {
            const double dx=to.x-from.x,dy=to.y-from.y,length=dx*dx+dy*dy;
            const double t=length?std::clamp(((p.x-from.x)*dx+(p.y-from.y)*dy)/length,0.0,1.0):0;
            return std::hypot(p.x-from.x-t*dx,p.y-from.y-t*dy);
        };
        for(int y=std::max(0,rect.y);y<std::min(int(height_),rect.y+int(rect.height));++y)
            for(int x=std::max(0,rect.x);x<std::min(int(width_),rect.x+int(rect.width));++x) {
                unsigned coverage=0;
                for(unsigned sy=0;sy<4;++sy)for(unsigned sx=0;sx<4;++sx) {
                    const Point p{(x+(sx+.5)/4)/scale_,(y+(sy+.5)/4)/scale_};
                    if(contains(clip_,p)&&std::min(distance(p,a,b),distance(p,b,c))<=radius)++coverage;
                }
                if(coverage)blend(x,y,color,(coverage*255+8)/16);
            }
    }
    void draw_text(std::string_view value,Rect box,const Font& font={},TextWrap wrap=TextWrap::none,
                   std::optional<framebuffer_detail::Color> color={}) {
        const auto saved=clip_;clip_=intersect(clip_,box);
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
        const auto metrics=framebuffer_detail::metrics(font,scale_);
        const auto rows=framebuffer_detail::lines(value,metrics,box.width,wrap);
        double y=box.y;
        for(const auto& row:rows) {
            if(y>=box.y+box.height)break;
            if(y+metrics.height()>clip_.y) {
                double x=box.x;
                for(const auto c:row.text) {
                    if(x>=box.x+box.width)break;
                    if(c!=U'\t')draw_glyph(framebuffer_font::glyph(metrics.face,c),x,y,metrics,foreground);
                    x+=metrics.advance(c);
                }
            }
            y+=metrics.height();
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
        const auto color=a.enabled?state.font.tone==Tone::muted?muted:state.font.tone==Tone::accent?accent:
            state.font.tone==Tone::error?error_color:ink:muted;
        const auto text_box=Rect{box.x+4,box.y+std::max(2.0,(box.height-line_height(state.font))/2),
            std::max(0.0,box.width-8),std::max(0.0,box.height-4)};
        switch(widget.spec.kind) {
        case Kind::group:fill(box,panel);outline(box,border_color);break;
        case Kind::label:draw_text(visible_text(widget),box,state.font,state.wrap,a.enabled?std::nullopt:std::optional{muted});break;
        case Kind::button:fill(box,a.enabled?control_color:disabled_color);outline(box,border_color);centered_text(state.label,text_box,state.font,color);break;
        case Kind::toggle: {
            const Rect mark{box.x,box.y+3,std::min(20.0,box.width),std::min(20.0,std::max(0.0,box.height-3))};
            fill(mark,a.enabled?(state.checked?accent:control_color):disabled_color);outline(mark,border_color);
            if(state.checked)stroke({mark.x+4,mark.y+10},{mark.x+8,mark.y+14},{mark.x+16,mark.y+5},a.enabled?panel:muted,1.6);
            draw_text(state.label,{box.x+26,box.y+2,std::max(0.0,box.width-26),std::max(0.0,box.height-4)},state.font,TextWrap::none,color);break;
        }
        case Kind::choice:case Kind::menu: {
            fill(box,a.enabled?control_color:disabled_color);outline(box,border_color);const auto label=visible_text(widget);
            if(widget.spec.kind==Kind::choice) {
                draw_text(label,{box.x+2,box.y+2,std::max(0.0,box.width-20),std::max(0.0,box.height-4)},state.font,TextWrap::none,color);
                const double x=box.x+box.width-6,y=box.y+box.height/2;
                stroke({x-3,y-5},{x+2,y},{x-3,y+5},a.enabled?border_color:muted);
            } else draw_text(label,{box.x+5,box.y+2,std::max(0.0,box.width-10),std::max(0.0,box.height-4)},state.font,TextWrap::none,color);
            break;
        }
        case Kind::text: {
            fill(box,a.enabled?panel:disabled_color);outline(box,border_color);
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
            fill(box,panel);outline(box,border_color);const auto offset=scroll_offset(widget.spec.key);
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
        clip_=a.clip;
        if(focused()==widget.spec.key) {
            if(widget.spec.kind==Kind::toggle)outline({box.x,box.y+3,std::min(20.0,box.width),std::min(20.0,std::max(0.0,box.height-3))},accent);
            else outline(box,accent);
        }
        clip_=saved;
    }
    void paint_pages() {
        for(const auto& [page,area]:page_bounds()) {
            fill(area,snapshot().active_page==page.id?selection_color:control_color);
            outline(area,border_color);
            centered_text(page.label,{area.x+6,area.y+std::max(0.0,(area.height-line_height())/2),std::max(0.0,area.width-12),area.height},{},page.enabled?ink:muted);
        }
    }
    void paint_popup() {
        if(!popup())return;
        const auto box=popup_bounds();fill(box,control_color);outline(box,border_color);
        for(std::size_t i=0;i<popup()->options.size();++i) {
            const Rect row{box.x,box.y+double(i)*popup_row_height(),box.width,popup_row_height()};
            if(i==popup()->index)fill(row,selection_color);
            outline(row,border_color);
            draw_text(popup()->options[i].label,{row.x+5,row.y+1,std::max(0.0,row.width-10),std::max(0.0,row.height-2)}, {},TextWrap::none,popup()->options[i].enabled?ink:muted);
        }
    }
    void paint_prompt() {
        if(!prompt())return;
        const auto box=prompt_bounds();fill(box,background);outline(box,border_color);
        draw_text(prompt()->title,{box.x+12,box.y+10,std::max(0.0,box.width-24),28},{14,true},TextWrap::word);
        const auto field=prompt_field_bounds();fill(field,panel);outline(field,accent);
        const auto saved=clip_;clip_=intersect(clip_,{field.x+4,field.y+4,std::max(0.0,field.width-8),std::max(0.0,field.height-8)});
        const auto selection=prompt_selection();
        const auto advance=[&](std::size_t at) {return measure_text({prompt()->value.substr(0,at),{},coordinate_limit,scale_,TextWrap::none}).width;};
        const auto anchor=advance(selection.anchor),caret=advance(selection.caret);
        fill({field.x+4+std::min(anchor,caret),field.y+4,std::abs(caret-anchor),line_height()},selection_color);
        draw_text(prompt()->value,{field.x+4,field.y+4,std::max(0.0,field.width-8),std::max(0.0,field.height-8)},{});
        fill({field.x+4+caret,field.y+4,1/scale_,line_height()},ink);clip_=saved;
        for(const auto& [area,label]:{std::pair{prompt_cancel_bounds(),"Cancel"},std::pair{prompt_accept_bounds(),"OK"}}) {
            fill(area,panel);outline(area,border_color);
            centered_text(label,{area.x+4,area.y+std::max(0.0,(area.height-line_height())/2),std::max(0.0,area.width-8),area.height},{},ink);
        }
    }
};
}
