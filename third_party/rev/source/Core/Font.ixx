module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <managed.hpp>

export module Rev.Core.Font;
import Rev.Core.Resource;
import Rev.Graphics.Canvas;
import Rev.Graphics.Texture;

export namespace Rev::Core {
    using namespace Rev::Graphics;
    Resource Arial_ttf = File("Rev/resources/Fonts/Arial/Arial.ttf");

    struct Font {
        Resource resource=Arial_ttf;
        inline static FT_Library ft=nullptr;
        inline static size_t users=0;
        FT_Face face=nullptr;
        Canvas* canvas=nullptr;
        float scale=1,size=12;
        int weight=200;
        float ascent=0,descent=0,lineGap=0,lineHeight=0;
        static constexpr unsigned atlasSize=1024,padding=2;
        struct Glyph {
            unsigned index=0;
            std::size_t page=0;
            float width=0,height=0,bearingX=0,bearingY=0,advance=0;
            float u0=0,v0=0,u1=0,v1=0;
        };
        // Stable atlas coordinates: adding another Unicode range allocates a
        // page rather than moving glyphs already referenced by other elements.
        struct Page {
            std::vector<unsigned char> pixels=std::vector<unsigned char>(atlasSize*atlasSize,0);
            std::unique_ptr<Texture> texture;
            unsigned x=padding,y=padding,rowHeight=0;
            bool dirty=true;
        };
        std::map<char32_t,Glyph> glyphs;
        std::vector<std::unique_ptr<Page>> pages;

        Font(Canvas* canvas,Resource resource=Arial_ttf,float size=12,float scale=1)
            :resource(resource),canvas(canvas),scale(scale),size(size) {
            if(users==0&&FT_Init_FreeType(&ft)) throw std::runtime_error("Failed to initialize FreeType");
            ++users;
            try {
                if(FT_New_Memory_Face(ft,resource.data,static_cast<FT_Long>(resource.size),0,&face)) throw std::runtime_error("Failed to load font from memory");
                if(FT_Set_Pixel_Sizes(face,0,static_cast<FT_UInt>(scale*size))) throw std::runtime_error("Failed to set font pixel size");
                ascent=static_cast<float>(face->size->metrics.ascender)/64/scale;
                descent=std::abs(static_cast<float>(face->size->metrics.descender))/64/scale;
                lineHeight=static_cast<float>(face->size->metrics.height)/64/scale;
                lineGap=lineHeight-ascent-descent;
                pages.push_back(std::make_unique<Page>());
            } catch(...) {
                if(face) FT_Done_Face(face);
                if(--users==0) { FT_Done_FreeType(ft); ft=nullptr; }
                throw;
            }
        }
        ~Font() {
            pages.clear();
            if(face) FT_Done_Face(face);
            if(--users==0&&ft) { FT_Done_FreeType(ft); ft=nullptr; }
        }
        const Glyph& getGlyph(char32_t codepoint) {
            if(const auto found=glyphs.find(codepoint);found!=glyphs.end()) return found->second;
            Glyph glyph;
            if(codepoint=='\n'||codepoint=='\r'||codepoint==0) return glyphs.emplace(codepoint,glyph).first->second;
            if(codepoint=='\t') {
                glyph=getGlyph(U' '); glyph.advance*=4; glyph.width=glyph.height=0;
                return glyphs.emplace(codepoint,glyph).first->second;
            }
            glyph.index=FT_Get_Char_Index(face,static_cast<FT_ULong>(codepoint));
            if(glyph.index==0&&codepoint!=0xfffd) return glyphs.emplace(codepoint,getGlyph(0xfffd)).first->second;
            // Missing font coverage uses FreeType's visible .notdef glyph.
            if(FT_Load_Glyph(face,glyph.index,FT_LOAD_RENDER)) throw std::runtime_error("Could not render a font glyph");
            const auto& bitmap=face->glyph->bitmap;
            if(bitmap.width+2*padding>=atlasSize||bitmap.rows+2*padding>=atlasSize) throw std::runtime_error("Font glyph exceeds atlas page size");
            auto* page=pages.back().get();
            if(page->x+bitmap.width+padding>=atlasSize) { page->x=padding; page->y+=page->rowHeight+padding; page->rowHeight=0; }
            if(page->y+bitmap.rows+padding>=atlasSize) { pages.push_back(std::make_unique<Page>()); page=pages.back().get(); }
            glyph.page=pages.size()-1;
            glyph.width=static_cast<float>(bitmap.width)/scale; glyph.height=static_cast<float>(bitmap.rows)/scale;
            glyph.bearingX=static_cast<float>(face->glyph->bitmap_left)/scale; glyph.bearingY=static_cast<float>(face->glyph->bitmap_top)/scale;
            glyph.advance=static_cast<float>(face->glyph->advance.x)/64/scale;
            for(unsigned y=0;y<bitmap.rows;++y) {
                const auto* row=bitmap.pitch>=0?bitmap.buffer+y*static_cast<unsigned>(bitmap.pitch):bitmap.buffer+(bitmap.rows-1-y)*static_cast<unsigned>(-bitmap.pitch);
                for(unsigned x=0;x<bitmap.width;++x) {
                    const auto value=bitmap.pixel_mode==FT_PIXEL_MODE_MONO?((row[x/8]&(0x80>>(x%8)))?255:0):row[x];
                    page->pixels[(page->y+y)*atlasSize+page->x+x]=static_cast<unsigned char>(value);
                }
            }
            glyph.u0=static_cast<float>(page->x)/atlasSize; glyph.v0=static_cast<float>(page->y)/atlasSize;
            glyph.u1=static_cast<float>(page->x+bitmap.width)/atlasSize; glyph.v1=static_cast<float>(page->y+bitmap.rows)/atlasSize;
            page->x+=bitmap.width+padding; page->rowHeight=std::max(page->rowHeight,bitmap.rows); page->dirty=true;
            return glyphs.emplace(codepoint,glyph).first->second;
        }
        void bind(std::size_t index) {
            auto& page=*pages.at(index);
            if(!page.texture) page.texture=std::make_unique<Texture>(canvas->context,Texture::Params{
                .data=page.pixels.data(),.width=atlasSize,.height=atlasSize,.channels=1});
            else if(page.dirty) page.texture->update(page.pixels.data());
            page.dirty=false; page.texture->bind(0);
        }
    };
}
