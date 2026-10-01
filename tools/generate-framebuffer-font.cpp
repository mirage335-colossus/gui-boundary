// Optional offline asset maintenance. See docs/framebuffer-font.md.
#include <ft2build.h>
#include FT_FREETYPE_H
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::array<const char*,2> filenames{"DejaVuSans.ttf","DejaVuSansMono-Bold.ttf"};
constexpr std::array<const char*,2> hashes{
    "54bf827eb99404e8f430c330ad30f063334f637eba0109b6a18d4f566a8e9dd8",
    "0d3c03d1b667192f91223660a3163325cf83132662fe4d9f7d6e596bf7a995c2"};
constexpr std::array<unsigned,31> sizes{
    8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
    36,40,42,48,56,64};
constexpr std::size_t glyph_count=192;
constexpr char base64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
struct Glyph { std::size_t offset; int width,height,left,top,advance; };
struct Face { unsigned pixels; int ascent,descent,line_height; std::size_t glyph_offset; };
void require(bool condition,const std::string& message) { if(!condition)throw std::runtime_error(message); }
std::vector<unsigned char> load(const std::string& path,const char* expected) {
    std::ifstream input(path,std::ios::binary);require(bool(input),"Cannot open font: "+path);
    std::vector<unsigned char> source((std::istreambuf_iterator<char>(input)),{});
    require(!input.bad(),"Cannot read font: "+path);
    std::array<unsigned char,32> digest{};unsigned size=0;
    require(EVP_Digest(source.data(),source.size(),digest.data(),&size,EVP_sha256(),nullptr)==1&&size==digest.size(),
        "Cannot hash font: "+path);
    std::ostringstream hash;
    for(const auto byte:digest)hash<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);
    require(hash.str()==expected,"Font checksum differs from the retained source: "+path);
    return source;
}
unsigned codepoint(std::size_t index) {
    return index<95?unsigned(index+32):index<191?unsigned(index-95+160):0xfffd;
}
struct Library {
    FT_Library value=nullptr;
    Library() {require(FT_Init_FreeType(&value)==0,"Cannot initialize FreeType");}
    ~Library() {FT_Done_FreeType(value);}
};
struct Font {
    FT_Face value=nullptr;
    Font(FT_Library library,const std::vector<unsigned char>& source) {
        require(source.size()<=std::size_t(std::numeric_limits<FT_Long>::max()),"Font is too large");
        require(FT_New_Memory_Face(library,source.data(),static_cast<FT_Long>(source.size()),0,&value)==0,
            "Cannot load font");
    }
    ~Font() {FT_Done_Face(value);}
};
}

int main(int argc,char** argv) {
    try {
        require(argc==3,"Usage: generate-framebuffer-font FONT_DIRECTORY OUTPUT.hpp");
        const std::string directory=argv[1];
        Library library;
        std::vector<Glyph> glyphs;std::vector<Face> faces;std::vector<unsigned char> coverage;
        for(unsigned weight=0;weight<filenames.size();++weight) {
            const auto source=load(directory+"/"+filenames[weight],hashes[weight]);
            Font font(library.value,source);
            for(const auto pixels:sizes) {
                require(FT_Set_Pixel_Sizes(font.value,0,pixels)==0,"Cannot set font size");
                const auto& metrics=font.value->size->metrics;
                faces.push_back({pixels,int(metrics.ascender/64),int(-metrics.descender/64),
                    int(metrics.height/64),glyphs.size()});
                for(std::size_t index=0;index<glyph_count;++index) {
                    // Identical hinting and grayscale rasterization to the native font adapter.
                    const auto id=FT_Get_Char_Index(font.value,codepoint(index));
                    require(id!=0,"Retained font does not cover a requested codepoint");
                    require(FT_Load_Glyph(font.value,id,FT_LOAD_RENDER)==0,"Cannot rasterize glyph");
                    const auto& bitmap=font.value->glyph->bitmap;
                    require(bitmap.pixel_mode==FT_PIXEL_MODE_GRAY,"Unexpected glyph coverage format");
                    const auto advance=font.value->glyph->advance.x;
                    require(advance%64==0,"Unexpected fractional hinted advance");
                    require(bitmap.width<=std::numeric_limits<std::uint16_t>::max()&&
                        bitmap.rows<=std::numeric_limits<std::uint16_t>::max(),"Glyph dimensions exceed storage");
                    for(const auto metric:{FT_Pos(font.value->glyph->bitmap_left),FT_Pos(font.value->glyph->bitmap_top),advance/64})
                        require(metric>=std::numeric_limits<std::int16_t>::min()&&metric<=std::numeric_limits<std::int16_t>::max(),
                            "Glyph metric exceeds storage");
                    glyphs.push_back({coverage.size(),int(bitmap.width),int(bitmap.rows),font.value->glyph->bitmap_left,
                        font.value->glyph->bitmap_top,int(advance/64)});
                    std::vector<unsigned char> samples;
                    for(unsigned y=0;y<bitmap.rows;++y) {
                        const auto* row=bitmap.pitch>=0?bitmap.buffer+y*unsigned(bitmap.pitch):
                            bitmap.buffer+(bitmap.rows-y-1)*unsigned(-bitmap.pitch);
                        for(unsigned x=0;x<bitmap.width;++x)samples.push_back(static_cast<unsigned char>((unsigned(row[x])+8)/17));
                    }
                    for(std::size_t i=0;i<samples.size();) {
                        std::size_t run=1;
                        while(run<16&&i+run<samples.size()&&samples[i+run]==samples[i])++run;
                        coverage.push_back(static_cast<unsigned char>(((run-1)<<4)|samples[i]));i+=run;
                    }
                }
            }
        }
        require(coverage.size()<=std::numeric_limits<std::uint32_t>::max(),"Atlas offset exceeds storage");
        int major=0,minor=0,patch=0;FT_Library_Version(library.value,&major,&minor,&patch);
        std::ofstream out(argv[2],std::ios::binary|std::ios::trunc);require(bool(out),"Cannot open output");
        out<<"// Generated by tools/generate-framebuffer-font.cpp; do not edit by hand.\n"
           <<"// DejaVu Sans Mono regular/bold 2.37; source files and license retained in\n"
           <<"// third_party/rev/resources. See docs/framebuffer-font.md for regeneration.\n"
           <<"// Regular SHA256: "<<hashes[0]<<"\n// Bold SHA256: "<<hashes[1]<<"\n"
           <<"// FreeType "<<major<<'.'<<minor<<'.'<<patch<<"; FT_LOAD_RENDER; 4-bit grayscale coverage.\n"
           <<"// Copyright (c) 2003 Bitstream, Inc.; DejaVu changes public domain.\n"
           <<"// Redistribution notice: third_party/rev/resources/DejaVu-LICENSE.\n"
           <<"#pragma once\n#include <array>\n#include <cstddef>\n#include <cstdint>\n#include <vector>\n\n"
           <<"namespace gui::framebuffer_font {\n"
           <<"struct Glyph { std::uint32_t offset; std::uint16_t width,height; std::int16_t left,top,advance; };\n"
           <<"struct Face { unsigned pixels,ascent,descent,line_height; std::size_t glyph_offset; };\n"
           <<"inline constexpr std::size_t glyph_count="<<glyph_count<<";\n"
           <<"inline constexpr std::size_t size_count="<<sizes.size()<<";\n"
           <<"inline constexpr std::array<Face,"<<faces.size()<<"> faces{{\n";
        for(const auto& face:faces)out<<"    {"<<face.pixels<<','<<face.ascent<<','<<face.descent<<','<<face.line_height<<','<<face.glyph_offset<<"},\n";
        out<<"}};\ninline constexpr std::array<Glyph,"<<glyphs.size()<<"> glyphs{{\n";
        for(const auto& glyph:glyphs)out<<"    {"<<glyph.offset<<','<<glyph.width<<','<<glyph.height<<','<<glyph.left<<','<<glyph.top<<','<<glyph.advance<<"},\n";
        out<<"}};\n// RLE: upper nibble is run length minus one, lower nibble is coverage.\n"
           <<"// Base64 literals keep this generated data compact and inexpensive to compile.\n"
           <<"// Bounded literal chunks also support compilers with small string limits.\n"
           <<"inline constexpr char coverage[][65]={\n";
        for(std::size_t i=0;i<coverage.size();i+=3) {
            if(i%48==0)out<<"    \"";
            const unsigned bits=(unsigned(coverage[i])<<16)|(i+1<coverage.size()?unsigned(coverage[i+1])<<8:0U)|
                (i+2<coverage.size()?unsigned(coverage[i+2]):0U);
            out<<base64[bits>>18]<<base64[(bits>>12)&63]<<
                (i+1<coverage.size()?base64[(bits>>6)&63]:'=')<<(i+2<coverage.size()?base64[bits&63]:'=');
            if(i%48==45||i+3>=coverage.size()) {
                out<<"\",\n";
            }
        }
        out<<R"(};
inline constexpr const Face& face(unsigned requested_pixels,bool bold=false) {
    const std::size_t begin=bold?size_count:0;
    for(std::size_t i=begin;i+1<begin+size_count;++i)
        if(requested_pixels<(faces[i].pixels+faces[i+1].pixels+1u)/2u)return faces[i];
    return faces[begin+size_count-1];
}
inline constexpr const Glyph& glyph(const Face& selected,char32_t codepoint) {
    const auto index=codepoint>=32&&codepoint<=126?std::size_t(codepoint-32):
        codepoint>=160&&codepoint<=255?std::size_t(codepoint-160+95):glyph_count-1;
    return glyphs[selected.glyph_offset+index];
}
inline constexpr unsigned coverage_symbol(char c) {
    return c>='A'&&c<='Z'?unsigned(c-'A'):c>='a'&&c<='z'?unsigned(c-'a'+26):
        c>='0'&&c<='9'?unsigned(c-'0'+52):c=='+'?62:c=='/'?63:0;
}
inline constexpr unsigned coverage_byte(std::size_t offset) {
    const auto part=offset%3,index=offset/3*4+part;
    const auto high=coverage_symbol(coverage[index/64][index%64]);
    const auto low=coverage_symbol(coverage[(index+1)/64][(index+1)%64]);
    return ((high<<6)|low)>>((2-part)*2)&255u;
}
inline std::vector<std::uint8_t> raster(const Glyph& selected) {
    std::vector<std::uint8_t> result;
    const auto count=std::size_t(selected.width)*selected.height;
    result.reserve(count);
    for(auto offset=std::size_t(selected.offset);result.size()<count;++offset) {
        const auto byte=coverage_byte(offset);
        result.insert(result.end(),(byte>>4)+1u,static_cast<std::uint8_t>((byte&15u)*17u));
    }
    return result;
}
}
)";
        out.close();require(bool(out),"Cannot write output");
        std::cout<<"Generated "<<faces.size()<<" faces, "<<glyphs.size()<<" glyphs, "
            <<coverage.size()<<" coverage bytes using FreeType "<<major<<'.'<<minor<<'.'<<patch<<"\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
