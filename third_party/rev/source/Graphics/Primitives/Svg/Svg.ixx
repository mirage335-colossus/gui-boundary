module;

#include <cstddef>
#include <functional>
#include <managed.hpp>

export module Rev.Primitive.Svg;

import Rev.Primitive;
import Rev.Core.Resource;
import Rev.Core.Shared;
import Rev.Core.Pos;
import Rev.Core.Rect;
import Rev.Core.Color;
import Rev.Core.Vertex;
import Rev.Core.Svg;

// Rev graphics modules
import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

export namespace Rev::Primitives {

    using namespace Rev::Core;
    using namespace Rev::Graphics;

    struct Svg : public Primitive {

        // Shared
        //--------------------------------------------------

        inline static Shared shared;
        inline static Pipeline* pipeline = nullptr;
        inline static VertexBuffer* vertices = nullptr;

        void createShared() {

            // Color pipeline
            pipeline = new Pipeline(canvas->context, {

                .attribs = Vertex::attribs,

                .openGlVert = File("./Shaders/Svg.vert"),
                .openGlFrag = File("./Shaders/Svg.frag"),
                .metalUniversal = File("./Shaders/Svg.metal")
            });

            vertices = new VertexBuffer(canvas->context, { .num = 6, .divisor = 1, .attribs = Vertex::attribs });
        }

        void destroyShared() {

            delete pipeline;
            delete vertices;
        }

        // Instance
        //--------------------------------------------------

        // Instance-specific data
        struct Data {

            Core::Rect rect;
            Core::Color color;
            float rotation;
            float opacity;

            static Data Default() {
                return {
                    { 0, 0, 100, 100 },
                    { 1, 0, 0, 1},
                    0.0f, 1.0f
                };
            }
        };

        Core::Svg* svg = nullptr;
        UniformBuffer* databuff = nullptr;
        Data* data = nullptr;

        Resource resource;

        // Create
        Svg(Canvas* canvas) : Primitive(canvas) {

            shared.create([this]() { this->createShared(); });

            //vertices = new VertexBuffer(4);
            svg = new Core::Svg(canvas, resource);
            databuff = new UniformBuffer(canvas->context, sizeof(Data));
            
            data = static_cast<Data*>(databuff->data);
            *data = Data::Default();
        }

        // Destroy
        ~Svg() {

            shared.destroy([this]() { this->destroyShared(); });
            
            delete databuff;
        }

        void compute() override {

            // If no change, do nothing
            if ((svg->resource == resource) &&
                (svg->width == data->rect.w) &&
                (svg->height == data->rect.h)) {
                return;
            }
            
            svg->resource = resource;
            svg->width = data->rect.w;
            svg->height = data->rect.h;
            
            svg->bake();
        }

        // Draw color
        void draw() override {

            if (!svg->texture) { return; }
         
            pipeline->bind();
            vertices->bind();
            databuff->bind(1);
            svg->texture->bind(0);

            canvas->drawArraysInstanced(Pipeline::Topology::TriangleFan, 0, 6, 1);
        }
    };
};
