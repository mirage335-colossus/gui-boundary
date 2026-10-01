module;

#include <cstddef>
#include <functional>
#include <managed.hpp>

export module Rev.Primitive.Rectangle;

import Rev.Primitive;
import Rev.Core.Resource;
import Rev.Core.Shared;
import Rev.Core.Pos;
import Rev.Core.Rect;
import Rev.Core.Color;
import Rev.Core.Vertex;

// Rev graphics modules
import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

export namespace Rev::Primitives {

    using namespace Rev::Core;
    using namespace Rev::Graphics;

    struct Rectangle : public Primitive {

        // Shared
        //--------------------------------------------------

        inline static Shared shared;
        inline static Pipeline* pipeline = nullptr;
        inline static Pipeline* stencilPipeline = nullptr;
        inline static VertexBuffer* vertices = nullptr;

        void createShared() {

            // Color pipeline
            pipeline = new Pipeline(canvas->context, {

                .attribs = Vertex::attribs,

                .openGlVert = File("./Shaders/Rectangle.vert"),
                .openGlFrag = File("./Shaders/Rectangle.frag"),
                .metalUniversal = File("./Shaders/Rectangle.metal")
            });

            stencilPipeline = new Pipeline(canvas->context, {

                .attribs = Vertex::attribs,

                .definitions = "#define STENCIL",

                .openGlVert = File("./Shaders/Rectangle.vert"),
                .openGlFrag = File("./Shaders/Rectangle.frag"),
                .metalUniversal = File("./Shaders/Rectangle.metal")
            });

            vertices = new VertexBuffer(canvas->context, { .num = 6, .divisor = 1, .attribs = Vertex::attribs });
        }

        void destroyShared() {

            delete pipeline;
            delete stencilPipeline;
            delete vertices;
        }

        // Instance
        //--------------------------------------------------

        // Instance-specific data
        struct Data {

            struct Corners { float tl, tr, bl, br; };            
            struct BorderWidth { float l, r, t, b; };
            struct BorderColor { Core::Color l, r, t, b; };
            struct Shadow { float x, y, size, blur; Core::Color color; };

            Core::Rect rect;
            Core::Color color;
            Corners corners;

            BorderWidth borderWidth;
            BorderColor borderColor;

            Shadow shadow;

            float opacity = 1.0f;
        };

        UniformBuffer* databuff = nullptr;
        Data* data = nullptr;

        // Create
        Rectangle(Canvas* canvas) : Primitive(canvas) {

            shared.create([this]() { this->createShared(); });

            //vertices = new VertexBuffer(4);
            databuff = new UniformBuffer(canvas->context, sizeof(Data));
            data = static_cast<Data*>(databuff->data);

            *data = {

                .rect = { 100, 100, 100, 100 },
                .color = { 1, 1, 1, 1 },

                .corners = { 5, 10, 15, 25 },

                .borderWidth = { 0, 0, 0, 0 },
                .borderColor = {
                    { 1, 1, 1, 1 },
                    { 1, 1, 1, 1 },
                    { 1, 1, 1, 1 },
                    { 1, 1, 1, 1 }
                },

                .shadow = {
                    .x = 0, .y = 0,
                    .size = 10, .blur = 10,
                    .color = { 1, 1, 1, 0 }
                }
            };
        }

        // Destroy
        ~Rectangle() {

            shared.destroy([this]() { this->destroyShared(); });
            
            delete databuff;
        }

        void compute() override {
            Data& data = (*this->data);
        }

        // Draw stencil
        void stencil() {

            stencilPipeline->bind();
            vertices->bind();
            databuff->bind(1);

            canvas->stencilWrite(true);
            canvas->drawArraysInstanced(Pipeline::Topology::TriangleFan, 0, 6, 1);
            canvas->stencilWrite(false);
        }

        // Draw color
        void draw() override {

            // Layout containers and text elements commonly have no painted
            // rectangle. Avoid shading their whole bounds just to blend zero
            // alpha, especially with a software OpenGL implementation. Stencil
            // submission remains independent: transparent boxes still clip.
            const auto& value = *data;
            if (value.opacity <= 0.0f) { return; }
            const bool borderVisible =
                (value.borderWidth.l > 0.0f && value.borderColor.l.a > 0.0f) ||
                (value.borderWidth.r > 0.0f && value.borderColor.r.a > 0.0f) ||
                (value.borderWidth.t > 0.0f && value.borderColor.t.a > 0.0f) ||
                (value.borderWidth.b > 0.0f && value.borderColor.b.a > 0.0f);
            if (value.color.a <= 0.0f && value.shadow.color.a <= 0.0f && !borderVisible) { return; }

            pipeline->bind();
            vertices->bind();
            databuff->bind(1);

            canvas->drawArraysInstanced(Pipeline::Topology::TriangleFan, 0, 6, 1);
        }
    };
};
