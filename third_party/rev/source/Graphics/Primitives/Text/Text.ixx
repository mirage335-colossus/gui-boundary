module;

#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <map>
#include <functional>
#include <rev_utf8.hpp>

#include <managed.hpp>

export module Rev.Primitive.Text;

import Rev.Primitive;
import Rev.Core.Shared;
import Rev.Core.Resource;
import Rev.Core.Font;
import Rev.Core.FontAtlas;
import Rev.Core.Pos;
import Rev.Core.Rect;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

export namespace Rev::Primitives {

    using namespace Rev::Core;
    using namespace Rev::Graphics;

    Core::Resource Arial_ttf = File("Rev/resources/Fonts/Arial/Arial.ttf");

    struct Text : public Primitive {

        // Shared
        //--------------------------------------------------
        
        inline static Shared shared;
        inline static Pipeline* pipeline;
        inline static Core::FontAtlas* fontAtlas;

        void createShared() {

            fontAtlas = new Core::FontAtlas(canvas);
            
            pipeline = new Pipeline(canvas->context, {

                .attribs = { 4, 4 },
                
                .openGlVert = File("./Shaders/Text.vert"),
                .openGlFrag = File("./Shaders/Text.frag"),

                .metalUniversal = File("./Shaders/Text.metal")
            });
        }

        void destroyShared() {

            delete fontAtlas;
            delete pipeline;
        }

        // Instance
        //--------------------------------------------------

        // Position and texture coords
        struct CharVertex {
            float x, y, width, height;
            float u0, v0, u1, v1;
        };

        // Instance-specific data
        struct Data {

            struct Pos { float x, y; };
            struct Color { float r, g, b, a; };

            Color color;
            Pos pos;
            float depth = 0.0f;
            float opacity = 1.0f;
        };

        UniformBuffer* databuff = nullptr;
        struct Batch { VertexBuffer* vertices=nullptr; size_t count=0; std::vector<CharVertex> instances; };
        std::map<size_t,Batch> batches;

        Font* font = nullptr;
        Data* data = nullptr;

        std::string content = "Hello World";
        float fontSize = 12.0f;

        struct Line {

            std::string content;            // Line content
            size_t start, end;
            Core::Rect rect;
        };

        std::vector<Line> lines;
        std::vector<Line> geometryLines;
        Font* geometryFont=nullptr;

        float xPos = 0;
        float yPos = 0;
        size_t glyphCount = 0;

        // Create
        Text(Canvas* canvas) : Primitive(canvas) {

            // Create shared pipeline
            shared.create([this]() {
                this->createShared();
            });

            databuff = new UniformBuffer(canvas->context, sizeof(Data));

            data = static_cast<Data*>(databuff->data);
            *data = {
                .color = { 1, 1, 1, 1 }
            };
        }

        // Destroy
        ~Text() {

            // Destroy shared pipeline
            shared.destroy([this]() { this->destroyShared(); });

            for(auto& [page,batch]:batches) delete batch.vertices;
            delete databuff;
        }

        // The font resource to use — set by the element layer via resolveStyle.
        // Falls back to Arial if not explicitly set.
        Core::Resource fontResource;

        // Compute vertices
        void compute() override {

            // Resolve font from the resource set by the element layer.
            Core::Resource res = fontResource.data ? fontResource : Arial_ttf;
            font = fontAtlas->get(res, fontSize, canvas->details.scale);
            // Color, opacity and caret updates do not change glyph geometry.
            // Stable atlas pages keep cached UVs valid as other text adds glyphs.
            const bool unchanged=font==geometryFont&&lines.size()==geometryLines.size()&&
                std::equal(lines.begin(),lines.end(),geometryLines.begin(),[](const Line& a,const Line& b) {
                    return a.content==b.content&&a.rect.x==b.rect.x&&a.rect.y==b.rect.y&&
                        a.rect.w==b.rect.w&&a.rect.h==b.rect.h;
                });
            if(unchanged) return;
            geometryFont=font; geometryLines=lines;

            for(auto& [page,batch]:batches) { batch.count=0; batch.instances.clear(); }
            for(const auto& line:lines) {
                float x=line.rect.x;
                const float baseline=line.rect.y+font->ascent;
                for(const auto rune:RevUtf8::runes(line.content)) {
                    const auto& glyph=font->getGlyph(rune.value);
                    if(glyph.width>0&&glyph.height>0) batches[glyph.page].instances.push_back({
                        x+glyph.bearingX,baseline-glyph.bearingY,glyph.width,glyph.height,
                        glyph.u0,glyph.v0,glyph.u1,glyph.v1});
                    x+=glyph.advance;
                }
            }
            glyphCount=0;
            for(auto& [page,batch]:batches) {
                const auto& instances=batch.instances;
                if(instances.empty()) continue;
                if(!batch.vertices) batch.vertices=new VertexBuffer(canvas->context,{.divisor=1,.attribs={4,4}});
                batch.vertices->resize(instances.size());
                std::copy(instances.begin(),instances.end(),static_cast<CharVertex*>(batch.vertices->data));
                batch.count=instances.size(); glyphCount+=batch.count;
            }
        }

        // Draw vertices
        void draw() override {
        
            pipeline->bind();
            databuff->bind(1);
            for(auto& [page,batch]:batches) {
                if(!batch.count) continue;
                font->bind(page); batch.vertices->bind();
                canvas->drawArraysInstanced(Pipeline::Topology::TriangleList,0,6,batch.count);
            }
        }
    };
};
