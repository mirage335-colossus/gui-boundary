#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

// UTF-8 cursor positions stay byte offsets, matching std::string and clipboard
// APIs. Invalid input advances one byte and paints a visible replacement glyph.
namespace RevUtf8 {
struct Rune { char32_t value=0; std::size_t begin=0,end=0; };
inline Rune next(std::string_view text,std::size_t offset) {
    if(offset>=text.size()) return {0,text.size(),text.size()};
    const auto first=static_cast<unsigned char>(text[offset]);
    Rune rune{0xfffd,offset,offset+1};
    if(first<0x80) { rune.value=first; return rune; }
    unsigned count=0; char32_t minimum=0,value=0;
    if(first>=0xc2&&first<=0xdf) { count=1; minimum=0x80; value=first&0x1f; }
    else if(first>=0xe0&&first<=0xef) { count=2; minimum=0x800; value=first&0x0f; }
    else if(first>=0xf0&&first<=0xf4) { count=3; minimum=0x10000; value=first&7; }
    else return rune;
    if(count>text.size()-offset-1) return rune;
    for(unsigned i=1;i<=count;++i) {
        const auto byte=static_cast<unsigned char>(text[offset+i]);
        if((byte&0xc0)!=0x80) return rune;
        value=(value<<6)|(byte&0x3f);
    }
    if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff)) return rune;
    return {value,offset,offset+count+1};
}
struct Range {
    std::string_view text;
    struct Iterator {
        std::string_view text; std::size_t offset;
        Rune operator*() const { return next(text,offset); }
        Iterator& operator++() { offset=next(text,offset).end; return *this; }
        bool operator!=(const Iterator& other) const { return offset!=other.offset; }
    };
    Iterator begin() const { return {text,0}; }
    Iterator end() const { return {text,text.size()}; }
};
inline Range runes(std::string_view text) { return {text}; }
inline std::size_t boundary(std::string_view text,std::size_t offset) {
    offset=std::min(offset,text.size());
    while(offset>0&&offset<text.size()&&(static_cast<unsigned char>(text[offset])&0xc0)==0x80) --offset;
    return offset;
}
inline std::size_t previous(std::string_view text,std::size_t offset) {
    offset=std::min(offset,text.size()); return offset?boundary(text,offset-1):0;
}
}
