module;

#include <cstddef>
#include <stdexcept>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Graphics.FrameBuffer;

import Rev.Graphics.Texture;

export namespace Rev::Graphics {

    struct FrameBuffer {

        void* context = nullptr;    // Unused for OpenGL implementation
        void* buffer = nullptr;

        struct Params {
            size_t width = 0, height = 0;
            size_t colorChannels = 4;
        };

        Params params;

        // Create
        FrameBuffer(void* context, Params params) {

            this->context = context;
            this->params = params;

            this->resize(params.width, params.height);
        }

        // Destroy
        ~FrameBuffer() {
            if (buffer) { metal_destroy_framebuffer(buffer); }
        }

        void resize(size_t width, size_t height) {

            // Reject invalid size
            if (!width || !height) {
                throw std::runtime_error("[FrameBuffer] Invalid size");
            }

            // Update params
            params.width = width;
            params.height = height;

            if (buffer) { metal_destroy_framebuffer(buffer); }
            buffer = metal_create_framebuffer((MetalContext*)context, params.width, params.height, params.colorChannels);

            // Create stencil buffer
            /*glGenRenderbuffers(1, &stencil);
            glBindRenderbuffer(GL_RENDERBUFFER, stencil);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, width, height);*/

            // Attach to framebuffer
            /*glBindFramebuffer(GL_FRAMEBUFFER, buffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture->id, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil);*/

            // Check completeness
            /*GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (status != GL_FRAMEBUFFER_COMPLETE) {
                throw std::runtime_error("[FrameBuffer] Framebuffer incomplete on resize");
            }*/

            //glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void bind() {
            //glBindFramebuffer(GL_FRAMEBUFFER, buffer);
        }
    };
};