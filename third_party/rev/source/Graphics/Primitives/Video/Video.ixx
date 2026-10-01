module;

#include <cstddef>
#include <functional>
#include <managed.hpp>

export module Rev.Primitive.Video;

import Rev.Primitive;
import Rev.Core.Resource;
import Rev.Core.Shared;
import Rev.Core.Rect;
import Rev.Core.Vertex;
import Rev.Graphics.Canvas;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Texture;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;

export namespace Rev::Primitives {

    using namespace Rev::Core;
    using namespace Rev::Graphics;

    struct Video : public Primitive {

        inline static Shared shared;
        inline static Pipeline* pipeline = nullptr;
        inline static VertexBuffer* vertices = nullptr;

        struct Data {
            Core::Rect rect = { 0, 0, 100, 100 };
            Core::Rect uv = { 0, 0, 1, 1 };
            float opacity = 1.0f;
        };

        UniformBuffer* databuff = nullptr;
        Data* data = nullptr;
        Texture* texture = nullptr;

        void createShared() {
            pipeline = new Pipeline(canvas->context, {
                .attribs = Vertex::attribs,
                .openGlVert = File("./Shaders/Video.vert"),
                .openGlFrag = File("./Shaders/Video.frag"),
                .metalUniversal = File("./Shaders/Video.metal")
            });
            vertices = new VertexBuffer(canvas->context, { .num = 6, .divisor = 1, .attribs = Vertex::attribs });
        }

        void destroyShared() {
            delete pipeline;
            delete vertices;
        }

        explicit Video(Canvas* canvas) : Primitive(canvas) {
            shared.create([this]() { createShared(); });
            databuff = new UniformBuffer(canvas->context, sizeof(Data));
            data = static_cast<Data*>(databuff->data);
            *data = Data{};
        }

        ~Video() {
            delete texture;
            delete databuff;
            shared.destroy([this]() { destroyShared(); });
        }

        void upload(const unsigned char* pixels, size_t width, size_t height) {
            if (!pixels || width == 0 || height == 0) { return; }

            if (!texture || texture->width != width || texture->height != height || texture->channels != 4) {
                delete texture;
                texture = new Texture(canvas->context, {
                    .data = const_cast<unsigned char*>(pixels),
                    .width = width,
                    .height = height,
                    .channels = 4
                });
                return;
            }

            texture->update(pixels);
        }

        void draw() override {
            if (!texture) { return; }
            pipeline->bind();
            vertices->bind();
            databuff->bind(1);
            texture->bind(0);
            canvas->drawArraysInstanced(Pipeline::Topology::TriangleFan, 0, 6, 1);
        }
    };
}
