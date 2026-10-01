module;

#include <algorithm>
#include <cstring>
#include <vector>
#include <numeric>
#include <stdexcept>
#include <string>
#include <glew/glew.h>

export module Rev.Graphics.VertexBuffer;

import Rev.Core.Vertex;
import Rev.Core.Vertex3;
import Rev.NativeWindow;

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
        GLuint vaoID = 0;
        GLuint bufferID = 0;

        // Buffer data and size
        void* context = nullptr;
        void* data = nullptr;
        size_t size = 0;

        VertexBuffer(void* context, Params params) {

            this->context = context;
            NativeWindow::requireContext(context, "VertexBuffer construct");
            this->params = params;

            glGenVertexArrays(1, &vaoID);
            glBindVertexArray(vaoID);

            this->params.num = 0;
            this->resize(params.num);
        }

        ~VertexBuffer() {

            NativeWindow::requireContext(context, "VertexBuffer destroy");

            if (data) {
                glBindBuffer(GL_ARRAY_BUFFER, bufferID);
                glUnmapBuffer(GL_ARRAY_BUFFER); // optional if persistent
            }

            if (bufferID) {
                glDeleteBuffers(1, &bufferID);
            }

            if (vaoID) {
                glDeleteVertexArrays(1, &vaoID);
            }
        }

        Vertex* verts() {
            return static_cast<Vertex*>(data);
        }

        Vertex3* verts3() {
            return static_cast<Vertex3*>(data);
        }

        void set(const std::vector<Vertex>& newVertices) {
            resize(newVertices.size());
            if (!data || newVertices.empty()) { return; }

            size_t bytes = std::min(size, newVertices.size() * sizeof(Vertex));
            memcpy(data, newVertices.data(), bytes);
        }

        void set3(const std::vector<Vertex3>& newVertices) {
            resize(newVertices.size());
            if (!data || newVertices.empty()) { return; }

            size_t bytes = std::min(size, newVertices.size() * sizeof(Vertex3));
            memcpy(data, newVertices.data(), bytes);
        }

        void resize(size_t newNum) {

            NativeWindow::requireContext(context, "VertexBuffer resize");

            // If no change, do nothing
            if (newNum == params.num) { return; }
            else { params.num = newNum; }

            // Derive vertex size, calculate buffer size
            size_t vertSize = sizeof(float) * std::accumulate(params.attribs.begin(), params.attribs.end(), 0);
            size = params.num * vertSize;
            
            // Delete previous buffer
            if (data) {
                glBindBuffer(GL_ARRAY_BUFFER, bufferID);
                glUnmapBuffer(GL_ARRAY_BUFFER);
                data = nullptr;
            }

            if (bufferID) {
                glDeleteBuffers(1, &bufferID);
                bufferID = 0;
            }

            if (!size) {
                glBindVertexArray(0);
                return;
            }
        
            glBindVertexArray(vaoID);
        
            glGenBuffers(1, &bufferID);
            glBindBuffer(GL_ARRAY_BUFFER, bufferID);
        
            glBufferStorage(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(size), nullptr,
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );
        
            data = glMapBufferRange(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(size),
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );

            if (!data) {
                glDeleteBuffers(1, &bufferID);
                bufferID = 0;
                size = 0;
                throw std::runtime_error("[VertexBuffer] Failed to map buffer");
            }

            size_t idx = 0, offset = 0;
            for (size_t attrib : params.attribs) {

                glVertexAttribPointer(static_cast<GLuint>(idx), static_cast<GLint>(attrib), GL_FLOAT, GL_FALSE, static_cast<GLsizei>(vertSize), (void*)(offset * sizeof(float)));
                glEnableVertexAttribArray(static_cast<GLuint>(idx));

                if (params.divisor) {
                    glVertexAttribDivisor(static_cast<GLuint>(idx), static_cast<GLuint>(params.divisor));
                }

                idx += 1;
                offset += attrib;
            }
        
            glBindVertexArray(0);
        }

        void bind() {
            glBindVertexArray(vaoID);
        }

        void unbind() {
            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
    };
};