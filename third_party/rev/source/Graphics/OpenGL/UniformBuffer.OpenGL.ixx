module;

#include <glew/glew.h>
#include <cstring>
#include <stdexcept>
#include <string>
#include <dbg.hpp>

export module Rev.Graphics.UniformBuffer;

import Rev.NativeWindow;

export namespace Rev::Graphics {

    struct UniformBuffer {

        GLuint bufferID = 0;
        void* context = nullptr;
        void* data = nullptr;
        size_t size = 0;

        UniformBuffer(void* context, size_t size) {

            this->context = context;
            this->size = size;
            NativeWindow::requireContext(context, "UniformBuffer construct");

            glGenBuffers(1, &bufferID);
            glBindBuffer(GL_UNIFORM_BUFFER, bufferID);

            glBufferStorage(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(size), nullptr,
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );

            data = glMapBufferRange(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(size),
                GL_MAP_WRITE_BIT |
                GL_MAP_PERSISTENT_BIT |
                GL_MAP_COHERENT_BIT
            );

            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            if (!data) {
                glDeleteBuffers(1, &bufferID);
                bufferID = 0;
                throw std::runtime_error("[UniformBuffer] Failed to map buffer");
            }
        }

        ~UniformBuffer() {

            //dbg("[UniformBuffer] destroying");
            NativeWindow::requireContext(context, "UniformBuffer destroy");

            if (data) {
                glBindBuffer(GL_UNIFORM_BUFFER, bufferID);
                glUnmapBuffer(GL_UNIFORM_BUFFER);
            }

            if (bufferID) {
                glDeleteBuffers(1, &bufferID);
            }
        }

        void set(void* value) {
            if (!data || !value || !size) { return; }
            memcpy(data, value, size);
        }

        void bind(GLuint bindingPoint) {
            glBindBufferBase(GL_UNIFORM_BUFFER, bindingPoint, bufferID);
        }

        void unbind() {
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }
    };
};