#pragma once

#include "interaction.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui {

// This cell profile uses printable ASCII output, independent of the terminal's
// locale and ambiguous-width policy. Application strings remain UTF-8; Unicode
// and control characters have literal escape representations in the renderer.
inline std::string terminal_literal(std::string_view text) {
    constexpr char hex[]="0123456789abcdef";
    std::string result;
    for(std::size_t offset=0;offset<text.size();) {
        const auto first=static_cast<unsigned char>(text[offset++]);
        if(first>=32&&first<127) {result+=char(first);continue;}
        if(first=='\n') {result+="\\n";continue;}
        if(first=='\r') {result+="\\r";continue;}
        if(first=='\t') {result+="\\t";continue;}
        if(first<128) {result+="\\x";result+=hex[first>>4];result+=hex[first&15];continue;}
        unsigned value=first,count=0;
        if(first>=0xc2&&first<=0xdf) {value=first&31;count=1;}
        else if(first>=0xe0&&first<=0xef) {value=first&15;count=2;}
        else if(first>=0xf0&&first<=0xf4) {value=first&7;count=3;}
        for(unsigned i=0;i<count&&offset<text.size();++i)
            value=(value<<6)|(static_cast<unsigned char>(text[offset++])&63);
        std::string digits;
        do {digits.insert(digits.begin(),hex[value&15]);value>>=4;} while(value);
        result+="\\u{"+digits+"}";
    }
    return result;
}

inline std::vector<std::string> terminal_lines(std::string_view text,std::size_t columns,bool wrap) {
    columns=std::max(std::size_t{1},columns);std::vector<std::string> result;
    std::size_t begin=0;
    for(;;) {
        const auto end=text.find('\n',begin);
        auto line=terminal_literal(text.substr(begin,end==std::string_view::npos?end:end-begin));
        while(wrap&&line.size()>columns) {
            auto split=line.rfind(' ',columns);
            if(split==std::string::npos||split==0)split=columns;
            result.push_back(line.substr(0,split));
            line.erase(0,split+(split<line.size()&&line[split]==' '?1:0));
        }
        result.push_back(std::move(line));
        if(end==std::string_view::npos)break;
        begin=end+1;
    }
    return result;
}

// Rendering and byte decoding only. Focus, editing, popup/list behavior, page
// selection, services and input validation belong to InteractiveAdapter.
// The same shared logical rectangles are projected into 8 by 16 pixel cells;
// small terminals pan the viewport without flattening columns or changing IDs.
class TerminalAdapter final : public InteractiveAdapter {
public:
    static constexpr double cell_width=8,cell_height=16;
    explicit TerminalAdapter(EventSink sink={})
        : InteractiveAdapter(std::move(sink),[](const TextMeasureRequest& request) {
            validate_measure_request(request);
            const auto columns=std::max(std::size_t{1},std::size_t(request.available_width/cell_width));
            const auto lines=terminal_lines(request.text,columns,request.wrap==TextWrap::word);
            std::size_t width=0;for(const auto& line:lines)width=std::max(width,line.size());
            return Size{std::min(request.available_width,double(width)*cell_width),double(lines.size())*cell_height};
        }) {}

    void terminal_size(unsigned columns,unsigned rows) {
        if(closed())return;
        policy().require_interaction();
        columns_=std::clamp(columns,1u,500u);rows_=std::clamp(rows,2u,200u);
        reveal_focus_=true;
        resize({std::max(640.0,double(columns_)*cell_width),
                std::max(480.0,double(rows_-1)*cell_height)},1);
    }
    unsigned columns() const {(void)closed();return columns_;}
    unsigned rows() const {(void)closed();return rows_;}
    Point viewport_origin() const {(void)closed();return origin_;}

    // A caller can inspect plain cells without owning a native terminal. Only
    // the host below this renderer is allowed to emit terminal escape commands.
    std::vector<std::string> render() {
        if(closed())return {};
        policy().require_interaction();
        reveal();
        cells_.assign(rows_,std::string(columns_,' '));
        ink_={snapshot().palette.text,snapshot().palette.background};
        inks_.assign(rows_,std::vector<Ink>(columns_,ink_));
        const Rect viewport{origin_.x,origin_.y,double(columns_)*cell_width,double(rows_-1)*cell_height};
        for(const auto& widget_key:paint_order(snapshot())) {
            const auto& widget=*find_widget(snapshot(),widget_key);
            const auto area=resolved_availability(widget.spec.key);
            if(!area.visible)continue;
            clip_=intersect(area.clip,viewport);
            if(!has_area(clip_))continue;
            draw_widget(widget,area);
        }
        draw_pages(viewport);
        draw_popup(viewport);
        draw_prompt(viewport);
        const auto title=terminal_literal(snapshot().title);
        cells_.back()=fit(title+" | Tab focus ^O options ^N page ^Q quit");
        return cells_;
    }
    // Trusted style commands are synthesized from numeric palette values after
    // literal rendering; application text can never become an ANSI command.
    std::vector<std::string> ansi_rows() {
        const auto plain=render();std::vector<std::string> result;
        for(std::size_t y=0;y<plain.size();++y) {
            std::string line;std::optional<Ink> previous;
            for(std::size_t x=0;x<plain[y].size();++x) {
                const auto ink=inks_[y][x];
                if(!previous||*previous!=ink) {
                    line+="\x1b[38;2;"+rgb(ink.foreground)+";48;2;"+rgb(ink.background)+"m";
                    previous=ink;
                }
                line+=plain[y][x];
            }
            line+="\x1b[0m";result.push_back(std::move(line));
        }
        return result;
    }

    // Split UTF-8/escape sequences are retained across reads. A bracketed paste
    // is one atomic text insertion, including its control characters; it cannot
    // trigger navigation or commands. Oversized paste is discarded as a whole.
    void input(std::string_view bytes) {
        if(closed())return;
        policy().require_interaction();
        reveal_focus_=true;
        for(const char byte:bytes) {
            if(closed())break;
            if(pasting_) {
                paste_+=byte;
                if(paste_.ends_with("\x1b[201~")) {
                    paste_.resize(paste_.size()-6);
                    if(!paste_overflow_&&!paste_.empty()&&valid_utf8(paste_))text(std::move(paste_));
                    paste_.clear();paste_overflow_=false;pasting_=false;
                } else if(paste_.size()>65536) {
                    paste_overflow_=true;paste_.erase(0,paste_.size()-6);
                }
                continue;
            }
            if(!escape_.empty()) {
                escape_+=byte;
                if(escape_.size()==2&&byte!='['&&byte!='O') {escape_.clear();continue;}
                if(escape_.size()>=3&&byte>=0x40&&byte<=0x7e) {
                    const auto sequence=std::move(escape_);escape_.clear();escape_sequence(sequence);
                } else if(escape_.size()>64)escape_.clear();
                continue;
            }
            const auto value=static_cast<unsigned char>(byte);
            if(!utf8_.empty()) {
                if((value&0xc0)!=0x80)utf8_.clear();
                else {
                    utf8_+=byte;
                    if(utf8_.size()==utf8_length_) {
                        if(valid_utf8(utf8_))text(std::move(utf8_));
                        utf8_.clear();
                    }
                    continue;
                }
            }
            if(value==27) {escape_="\x1b";continue;}
            if(value>=0xc2&&value<=0xf4) {
                utf8_=byte;utf8_length_=value<0xe0?2u:value<0xf0?3u:4u;continue;
            }
            if(value>=128)continue;
            switch(value) {
                case 1:key(Key::select_all);break;
                case 3:case 17:key(Key::quit);break;
                case 8:case 127:key(Key::backspace);break;
                case 9:key(Key::tab);break;
                case 10:key(Key::enter,false,true);break;
                case 13:key(Key::enter);break;
                case 14:key(Key::next_page);break;
                case 15:key(Key::menu);break;
                case 16:key(Key::previous_page);break;
                case 19:key(Key::enter,true);break;
                case 21:key(Key::select_all);key(Key::backspace);break;
                case 32:text(" ");key(Key::space);break;
                default:if(value>=33)text(std::string(1,byte));break;
            }
        }
    }
    void flush_escape() {
        if(closed())return;
        policy().require_interaction();
        if(escape_=="\x1b")key(Key::escape);
        escape_.clear();
    }

private:
    unsigned columns_=80,rows_=31;
    Point origin_;
    bool reveal_focus_=true,pasting_=false,paste_overflow_=false;
    std::size_t utf8_length_=0;
    std::optional<WidgetKey> rendered_focus_;
    std::string escape_,utf8_,paste_;
    Rect clip_;
    std::vector<std::string> cells_;
    struct Ink {Color foreground,background;bool operator==(const Ink&) const = default;};
    Ink ink_;
    std::vector<std::vector<Ink>> inks_;

    static std::string rgb(Color color) {
        return std::to_string(color.red)+";"+std::to_string(color.green)+";"+std::to_string(color.blue);
    }
    std::string fit(std::string value) const {value.resize(columns_,' ');return value;}
    void reveal() {
        const auto focus=focused();
        if(focus!=rendered_focus_)reveal_focus_=true;
        rendered_focus_=focus;
        Rect target{};
        if(prompt())target=prompt_field_bounds();
        else if(popup())target=popup_bounds();
        else if(focus) {
            target=resolved_availability(*focus).bounds;
            const auto* widget=find_widget(snapshot(),*focus);
            if(widget&&widget->spec.kind==Kind::text) {
                const auto caret=text_selection(*focus).caret;
                const auto box=text_bounds(*widget);
                const auto offset=scroll_offset(*focus);
                const auto position=editor_caret_position(*widget,caret);
                target={box.x+position.x-offset.x,box.y+position.y-offset.y,cell_width,cell_height};
            }
        }
        if(reveal_focus_&&has_area(target)) {
            const auto width=double(columns_)*cell_width,height=double(rows_-1)*cell_height;
            if(target.x<origin_.x)origin_.x=std::floor(target.x/cell_width)*cell_width;
            else if(target.x+target.width>origin_.x+width)
                origin_.x=std::ceil((target.x+std::min(target.width,width)-width)/cell_width)*cell_width;
            if(target.y<origin_.y)origin_.y=std::floor(target.y/cell_height)*cell_height;
            else if(target.y+target.height>origin_.y+height)
                origin_.y=std::ceil((target.y+std::min(target.height,height)-height)/cell_height)*cell_height;
        }
        origin_.x=std::clamp(origin_.x,0.0,std::max(0.0,snapshot().client_size.width-double(columns_)*cell_width));
        origin_.y=std::clamp(origin_.y,0.0,std::max(0.0,snapshot().client_size.height-double(rows_-1)*cell_height));
        reveal_focus_=false;
    }
    void put(int column,int row,char value) {
        if(column<0||row<0||column>=int(columns_)||row>=int(rows_-1))return;
        const Point center{origin_.x+(double(column)+0.5)*cell_width,origin_.y+(double(row)+0.5)*cell_height};
        if(contains(clip_,center)) {
            cells_[std::size_t(row)][std::size_t(column)]=value;
            inks_[std::size_t(row)][std::size_t(column)]=ink_;
        }
    }
    int column(double x) const {return int(std::floor((x-origin_.x)/cell_width));}
    int row(double y) const {return int(std::floor((y-origin_.y)/cell_height));}
    int first_column(double x) const {return int(std::ceil((x-origin_.x)/cell_width-0.5));}
    int first_row(double y) const {return int(std::ceil((y-origin_.y)/cell_height-0.5));}
    void clear(Rect bounds) {
        const auto area=intersect(bounds,clip_);
        for(int y=std::max(0,row(area.y));y<std::min(int(rows_-1),row(area.y+area.height)+1);++y)
            for(int x=std::max(0,column(area.x));x<std::min(int(columns_),column(area.x+area.width)+1);++x)put(x,y,' ');
    }
    void label(Rect bounds,std::string_view value,bool wrap=false) {
        const auto saved=clip_;clip_=intersect(clip_,bounds);
        const auto first=first_column(bounds.x),last=first_column(bounds.x+bounds.width);
        const auto top=first_row(bounds.y),bottom=first_row(bounds.y+bounds.height);
        const auto lines=terminal_lines(value,std::size_t(std::max(1,last-first)),wrap);
        int y=top;
        for(const auto& line:lines) {
            if(y>=bottom)break;
            int x=first;
            for(const auto character:line) {if(x>=last)break;put(x++,y,character);}
            ++y;
        }
        clip_=saved;
    }
    void frame(Rect bounds) {
        const auto left=column(bounds.x),top=row(bounds.y);
        const auto right=column(bounds.x+bounds.width)-1,bottom=row(bounds.y+bounds.height)-1;
        if(right<=left||bottom<=top)return;
        for(int x=std::max(0,left);x<=std::min(int(columns_)-1,right);++x) {put(x,top,'-');put(x,bottom,'-');}
        for(int y=std::max(0,top);y<=std::min(int(rows_)-2,bottom);++y) {put(left,y,'|');put(right,y,'|');}
        put(left,top,'+');put(right,top,'+');put(left,bottom,'+');put(right,bottom,'+');
    }
    void draw_widget(const Widget& widget,const Availability& area) {
        const auto& state=widget.state;const auto bounds=area.bounds;
        const bool selected=focused()==widget.spec.key;
        const auto& palette=snapshot().palette;
        ink_={area.enabled?tone_color(palette,state.font.tone):palette.muted,
              area.enabled?(selected?palette.selection:palette.surface):palette.disabled};
        if(widget.spec.kind!=Kind::label)clear(bounds);
        auto text_bounds=bounds;
        text_bounds.x+=cell_width;text_bounds.width=std::max(0.0,bounds.width-2*cell_width);
        text_bounds.y+=std::max(0.0,std::floor((bounds.height-cell_height)/2/cell_height)*cell_height);
        text_bounds.height=std::max(cell_height,std::min(bounds.height,cell_height));
        std::string value;
        switch(widget.spec.kind) {
            case Kind::group:ink_.foreground=palette.border;frame(bounds);return;
            case Kind::label:label(bounds,state.text,state.wrap==TextWrap::word);return;
            case Kind::button:value="[ "+state.label+" ]";break;
            case Kind::toggle:value=std::string(state.checked?"[x] ":"[ ] ")+state.label;break;
            case Kind::choice:
                value=state.label+": [";
                for(const auto& option:state.options)if(state.selected==option.id)value+=option.label;
                value+="] v";break;
            case Kind::menu:value="[ "+state.label+" v ]";break;
            case Kind::text: {
                text_bounds=this->text_bounds(widget);
                draw_editor(widget,area);break;
            }
            case Kind::list:draw_list(widget,bounds);break;
            case Kind::bitmap:draw_bitmap(widget,bounds);break;
        }
        if(!value.empty())label(text_bounds,value);
        if(selected)put(first_column(bounds.x),first_row(text_bounds.y),'>');
        if(!area.enabled)put(first_column(bounds.x),first_row(text_bounds.y),'!');
    }
    void draw_editor(const Widget& widget,const Availability& area) {
        const auto& state=widget.state;const auto saved=clip_;
        const auto box=text_bounds(widget);const auto offset=scroll_offset(widget.spec.key);
        const auto height=line_height(state.font);const auto selection=text_selection(widget.spec.key);
        ink_.background=area.enabled?snapshot().palette.surface:snapshot().palette.disabled;clear(area.bounds);
        clip_=intersect(clip_,box);
        if(state.text.empty())label(box,state.placeholder,widget.spec.text_policy.multiline&&state.wrap==TextWrap::word);
        const auto lines=editor_lines(widget);
        for(std::size_t row=0;row<lines.size();++row) {
            const auto& line=lines[row];const auto y=box.y+double(row)*height-offset.y;
            label({box.x-offset.x,y,box.width+offset.x,height},state.text.substr(line.begin,line.end-line.begin));
            if(focused()==widget.spec.key&&selection.anchor!=selection.caret) {
                const auto left=std::clamp(std::min(selection.anchor,selection.caret),line.begin,line.end);
                const auto right=std::clamp(std::max(selection.anchor,selection.caret),line.begin,line.end);
                const auto width=[&](std::size_t end) {
                    return measure_text({state.text.substr(line.begin,end-line.begin),state.font,coordinate_limit,
                        snapshot().display_scale,TextWrap::none}).width;
                };
                const auto first=first_column(box.x-offset.x+width(left)),last=first_column(box.x-offset.x+width(right));
                const auto cell_y=first_row(y);
                if(cell_y>=0&&cell_y<int(rows_-1))for(auto x=std::max(0,first);x<std::min(int(columns_),last);++x) {
                    const Point center{origin_.x+(double(x)+0.5)*cell_width,origin_.y+(double(cell_y)+0.5)*cell_height};
                    if(contains(clip_,center))inks_[std::size_t(cell_y)][std::size_t(x)].background=snapshot().palette.selection;
                }
            }
        }
        clip_=saved;
        if(focused()==widget.spec.key&&!widget.spec.text_policy.read_only) {
            const auto caret=editor_caret_position(widget,selection.caret);
            ink_.foreground=snapshot().palette.accent;
            put(first_column(box.x+caret.x-offset.x),first_row(box.y+caret.y-offset.y),'|');
        }
    }
    void draw_list(const Widget& widget,Rect bounds) {
        const auto& state=widget.state;
        if(state.records.empty()) {label(bounds,state.placeholder);return;}
        const auto offset=scroll_offset(widget.spec.key);
        for(std::size_t index=0;index<state.records.size();++index) {
            const auto& record=state.records[index];
            const auto top=bounds.y+double(index)*widget.spec.row_height-offset.y;
            const Rect line{bounds.x,top,bounds.width,widget.spec.row_height};
            if(!has_area(intersect(line,clip_)))continue;
            if(record.cells.empty())label(line,record.accessible_text);
            for(const auto& cell:record.cells)label({bounds.x+cell.bounds.x-offset.x,top+cell.bounds.y,
                cell.bounds.width,cell.bounds.height},cell.text,cell.wrap==TextWrap::word);
            if(state.selected==record.id)put(column(bounds.x),row(top),'*');
            else if(!record.enabled)put(column(bounds.x),row(top),'!');
        }
    }
    void draw_bitmap(const Widget& widget,Rect bounds) {
        if(!has_area(bounds))return;
        const auto width=std::max(1u,unsigned(std::ceil(bounds.width/cell_width)));
        const auto height=std::max(1u,unsigned(std::ceil(bounds.height/cell_height)));
        const auto& source=widget.state.bitmap.source;
        const auto minimum=source.minimum_extent();
        if(source.sampling()==BitmapSampling::discrete&&(width<minimum.width||height<minimum.height)) {
            label(bounds,"[Bitmap needs more cells]",true);return;
        }
        const auto image=policy().sample_bitmap(widget.spec.key,
            {width,height,{0,0,width,height},PixelFormat::rgb24,cell_width/cell_height,true});
        const auto block=image.block();
        if(!block.width||!block.height||!has_area(bounds))return;
        constexpr char ramp[]=" .:-=+*#%@";
        const auto area=intersect(bounds,clip_);
        for(int y=std::max(0,row(area.y));y<std::min(int(rows_-1),row(area.y+area.height)+1);++y)
            for(int x=std::max(0,column(area.x));x<std::min(int(columns_),column(area.x+area.width)+1);++x) {
                const auto dx=origin_.x+(double(x)+0.5)*cell_width-bounds.x;
                const auto dy=origin_.y+(double(y)+0.5)*cell_height-bounds.y;
                if(dx<0||dy<0||dx>=bounds.width||dy>=bounds.height)continue;
                const auto sx=std::min(block.width-1,unsigned(dx/bounds.width*block.width));
                const auto sy=std::min(block.height-1,unsigned(dy/bounds.height*block.height));
                const auto rgb=bitmap_detail::read(block.bytes.data()+std::size_t(sy)*block.stride_bytes,sx,block.format);
                const auto gray=(unsigned(rgb[0])*77+unsigned(rgb[1])*150+unsigned(rgb[2])*29)>>8;
                ink_={{rgb[0],rgb[1],rgb[2]},{rgb[0],rgb[1],rgb[2]}};
                put(x,y,ramp[gray*9/255]);
            }
    }
    void draw_pages(Rect viewport) {
        for(const auto& tab:page_tabs(snapshot())) {
            ink_={tab.enabled?snapshot().palette.text:snapshot().palette.muted,
                tab.selected?snapshot().palette.selection:snapshot().palette.surface};
            clip_=intersect(viewport,tab.bounds);clear(tab.bounds);
            label(tab.bounds,(tab.selected?"[":" ")+tab.label+(tab.selected?"]":" "));
        }
    }
    void draw_popup(Rect viewport) {
        if(!popup())return;
        ink_={snapshot().palette.text,snapshot().palette.surface};
        const auto bounds=popup_bounds();clip_=intersect(viewport,bounds);clear(bounds);frame(bounds);
        const auto& open=*popup();
        for(std::size_t i=0;i<open.options.size();++i) {
            const Rect line{bounds.x+cell_width,bounds.y+double(i)*popup_row_height(),
                std::max(0.0,bounds.width-cell_width),popup_row_height()};
            ink_={open.options[i].enabled?snapshot().palette.text:snapshot().palette.muted,
                i==open.index?snapshot().palette.selection:snapshot().palette.surface};
            clear(line);label(line,(i==open.index?"> ":"  ")+open.options[i].label+(open.options[i].enabled?"":" (disabled)"));
        }
    }
    void draw_prompt(Rect viewport) {
        if(!prompt())return;
        ink_={snapshot().palette.text,snapshot().palette.surface};
        const auto bounds=prompt_bounds();clip_=intersect(viewport,bounds);clear(bounds);frame(bounds);
        label({bounds.x+cell_width,bounds.y+cell_height,bounds.width-2*cell_width,cell_height},prompt()->title);
        const auto caret=prompt_selection().caret;
        label(prompt_field_bounds(),"["+prompt()->value.substr(0,caret)+"|"+prompt()->value.substr(caret)+"]");
        label(prompt_accept_bounds(),"[ OK ]");label(prompt_cancel_bounds(),"[ Cancel ]");
    }
    void escape_sequence(const std::string& sequence) {
        if(sequence=="\x1b[200~") {pasting_=true;paste_.clear();paste_overflow_=false;return;}
        // Xterm's modifier form: CSI 1 ; (1+Shift+2*Alt+4*Ctrl) final.
        if(sequence.size()==6&&sequence.starts_with("\x1b[1;")&&sequence[4]>='2'&&sequence[4]<='8') {
            const auto modifier=unsigned(sequence[4]-'1');
            const auto shifted=(modifier&1)!=0,alt=(modifier&2)!=0,control=(modifier&4)!=0;
            const auto final=sequence.back();
            if(final=='A')key(Key::up,control,shifted,alt);
            else if(final=='B')key(Key::down,control,shifted,alt);
            else if(final=='C')key(Key::right,control,shifted,alt);
            else if(final=='D')key(Key::left,control,shifted,alt);
            else if(final=='H')key(Key::home,control,shifted,alt);
            else if(final=='F')key(Key::end,control,shifted,alt);
            return;
        }
        if(sequence=="\x1b[A"||sequence=="\x1bOA")key(Key::up);
        else if(sequence=="\x1b[B"||sequence=="\x1bOB")key(Key::down);
        else if(sequence=="\x1b[C"||sequence=="\x1bOC")key(Key::right);
        else if(sequence=="\x1b[D"||sequence=="\x1bOD")key(Key::left);
        else if(sequence=="\x1b[H"||sequence=="\x1bOH"||sequence=="\x1b[1~"||sequence=="\x1b[7~")key(Key::home);
        else if(sequence=="\x1b[F"||sequence=="\x1bOF"||sequence=="\x1b[4~"||sequence=="\x1b[8~")key(Key::end);
        else if(sequence=="\x1b[Z")key(Key::tab,false,true);
        else if(sequence=="\x1b[3~")key(Key::delete_key);
        else if(sequence=="\x1b[5~")key(Key::page_up);
        else if(sequence=="\x1b[6~")key(Key::page_down);
        else if(sequence=="\x1bOP"||sequence=="\x1b[11~")key(Key::f1);
        else if(sequence=="\x1bOQ"||sequence=="\x1b[12~")key(Key::f2);
        else if(sequence=="\x1bOR"||sequence=="\x1b[13~")key(Key::f3);
        else if(sequence=="\x1bOS"||sequence=="\x1b[14~")key(Key::f4);
        else if(sequence=="\x1b[15~")key(Key::f5);
        else if(sequence=="\x1b[17~")key(Key::f6);
        else if(sequence=="\x1b[18~")key(Key::f7);
        else if(sequence=="\x1b[19~")key(Key::f8);
        else if(sequence=="\x1b[20~")key(Key::f9);
        else if(sequence=="\x1b[21~")key(Key::f10);
        else if(sequence=="\x1b[23~")key(Key::f11);
        else if(sequence=="\x1b[24~")key(Key::f12);
    }
};

} // namespace gui
