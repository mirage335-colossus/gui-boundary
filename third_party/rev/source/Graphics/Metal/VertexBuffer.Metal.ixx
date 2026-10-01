module;

#include <cstddef>
#include <vector>
#include <numeric>

#include <dbg.hpp>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Graphics.VertexBuffer;

import Rev.Core.Vertex;

export namespace Rev::Graphics {

    using namespace Rev::Core;

    struct VertexBuffer {

        struct Params {
            size_t num = 0;
            size_t divisor = 0;
            std::vector<size_t> attribs;
        };

        Params params;

        // Track buffer
        void* context = nullptr;
        void* buffer = nullptr;

        // Buffer data and size
        void* data = nullptr;
        size_t size = 0;

        VertexBuffer(void* context, Params params) {

            this->params = params;
            this->context = context;

            this->params.num = 0;
            this->resize(params.num);
        }

        ~VertexBuffer() {

            if (buffer) {
                metal_destroy_vertex_buffer(buffer);
            }
        }

        Vertex* verts() {
            return static_cast<Vertex*>(data);
        }

        void set(std::vector<Vertex> newVertices) {
            memcpy(data, newVertices.data(), size);
        }

        void resize(size_t newNum) {

            // If no change, do nothing
            if (newNum == params.num) { return; }
            else { params.num = newNum; }

            // Derive vertex size, calculate needed size
            size_t vertSize = sizeof(float) * std::accumulate(params.attribs.begin(), params.attribs.end(), 0);
            size = params.num * vertSize;

            if (buffer) { metal_destroy_vertex_buffer(buffer); }

            // Create buffer, map data
            buffer = metal_create_vertex_buffer((MetalContext*)context, size);
            data = metal_map_vertex_buffer(buffer);
        }

        void bind() {
            if (!context || !buffer) {
                dbg("Couldn't bind vertex buffer!");
            }
            
            metal_bind_vertex_buffer((MetalContext*)context, buffer, 0);
        }

        void unbind() {
            
        }
    };
};