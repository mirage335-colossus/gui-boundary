#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace gui {
inline bool valid_utf8(std::string_view text) {
    std::size_t i=0;
    while(i<text.size()) {
        const auto first=static_cast<unsigned char>(text[i++]);
        if(first==0)return false;
        if(first<0x80)continue;
        unsigned count=0,value=0,minimum=0;
        if(first>=0xc2&&first<=0xdf){count=1;value=first&31;minimum=0x80;}
        else if(first>=0xe0&&first<=0xef){count=2;value=first&15;minimum=0x800;}
        else if(first>=0xf0&&first<=0xf4){count=3;value=first&7;minimum=0x10000;}
        else return false;
        if(count>text.size()-i)return false;
        while(count--) {
            const auto next=static_cast<unsigned char>(text[i++]);
            if((next&0xc0)!=0x80)return false;
            value=(value<<6)|(next&63);
        }
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }
    return true;
}
enum class SubmitKey { none, enter, control_enter };
struct TextPolicy {
    bool multiline=false,read_only=false;
    std::size_t max_bytes=1024*1024;
    SubmitKey submit=SubmitKey::none;
    bool operator==(const TextPolicy&) const = default;
};
inline std::string text_error(std::string_view text,const TextPolicy& policy) {
    if(text.size()>policy.max_bytes)return "Text exceeds the byte limit";
    if(!valid_utf8(text))return "Text must be UTF-8 without zero bytes";
    if(!policy.multiline&&text.find_first_of("\r\n")!=std::string_view::npos)return "Text must use one line";
    return {};
}
inline std::size_t text_boundary(std::string_view text,std::size_t offset) {
    offset=std::min(offset,text.size());
    while(offset>0&&offset<text.size()&&(static_cast<unsigned char>(text[offset])&0xc0)==0x80)--offset;
    return offset;
}
struct TextSelection {
    std::size_t anchor=0,caret=0;
    TextSelection clamped(std::string_view text) const {
        return {text_boundary(text,anchor),text_boundary(text,caret)};
    }
    bool operator==(const TextSelection&) const = default;
};
struct TextReplacement { std::string text,error;TextSelection selection;bool changed=false; };
inline TextReplacement replace_text(std::string_view current,TextSelection selection,
                                    std::string_view inserted,const TextPolicy& policy) {
    TextReplacement out{std::string(current),{},selection,false};
    if(policy.read_only){out.error="Text is read only";return out;}
    if(auto error=text_error(current,policy);!error.empty()){out.error=error;return out;}
    selection=selection.clamped(current);
    const auto first=std::min(selection.anchor,selection.caret),last=std::max(selection.anchor,selection.caret);
    const auto retained=current.size()-(last-first);
    if(retained>policy.max_bytes||inserted.size()>policy.max_bytes-retained) {
        out.error="Text exceeds the byte limit";return out;
    }
    auto candidate=std::string(current);candidate.replace(first,last-first,inserted);
    if(auto error=text_error(candidate,policy);!error.empty()){out.error=error;return out;}
    out.changed=candidate!=current;
    out.text=std::move(candidate);
    if(out.changed)out.selection={first+inserted.size(),first+inserted.size()};
    return out;
}
inline bool is_submit(const TextPolicy& policy,bool control,bool shift) {
    return !shift&&((policy.submit==SubmitKey::enter&&!control)||
                   (policy.submit==SubmitKey::control_enter&&control));
}
}
