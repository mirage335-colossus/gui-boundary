module;

#include <stdexcept>
#include <string>

#include <glew/glew.h>

export module Rev.Graphics.FrameBuffer;

import Rev.Graphics.Texture;
import Rev.NativeWindow;

export namespace Rev::Graphics {

    struct FrameBuffer {

        void* context = nullptr;    // Unused for OpenGL implementation

        GLuint buffer = 0;

        // This is still named stencil to avoid disturbing the rest of the
        // framework, but it is now a combined depth/stencil renderbuffer.
        GLuint stencil = 0;

        Texture* texture = nullptr;

        struct Params {
            size_t width = 0, height = 0;
            size_t colorChannels = 4;
        };

        Params params;

        // Create
        FrameBuffer(void* context, Params params) {

            this->context = context;
            this->params = params;
            NativeWindow::requireContext(context, "FrameBuffer construct");
            glGenFramebuffers(1, &buffer);

            this->resize(params.width, params.height);
        }

        // Destroy
        ~FrameBuffer() {

            NativeWindow::requireContext(context, "FrameBuffer destroy");

            if (stencil) { glDeleteRenderbuffers(1, &stencil); }
            if (buffer) { glDeleteFramebuffers(1, &buffer); }

            delete texture;
        }

        void resize(size_t width, size_t height) {

            NativeWindow::requireContext(context, "FrameBuffer resize");

            // Reject invalid size
            if (!width) { width = 1; }
            if (!height) { height = 1; }

            if (texture && stencil && params.width == width && params.height == height) {
                return;
            }

            GLint previousFramebuffer = 0;
            GLint previousRenderbuffer = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &previousRenderbuffer);

            // Update params
            params.width = width;
            params.height = height;

            // Delete old attachments
            if (texture) { delete texture; texture = nullptr; }
            if (stencil) { glDeleteRenderbuffers(1, &stencil); stencil = 0; }

            // Create color texture
            texture = new Texture(context, {
                .width = width,
                .height = height,
                .channels = params.colorChannels
            });

            // Create combined depth/stencil buffer.
            //
            // This preserves stencil functionality while also providing
            // a depth buffer for View3d.
            glGenRenderbuffers(1, &stencil);
            glBindRenderbuffer(GL_RENDERBUFFER, stencil);
            glRenderbufferStorage(
                GL_RENDERBUFFER,
                GL_DEPTH24_STENCIL8,
                static_cast<GLsizei>(width),
                static_cast<GLsizei>(height)
            );

            // Attach to framebuffer
            glBindFramebuffer(GL_FRAMEBUFFER, buffer);

            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D,
                texture->id,
                0
            );

            glFramebufferRenderbuffer(
                GL_FRAMEBUFFER,
                GL_DEPTH_STENCIL_ATTACHMENT,
                GL_RENDERBUFFER,
                stencil
            );

            // Check completeness
            GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
            glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previousRenderbuffer));

            if (status != GL_FRAMEBUFFER_COMPLETE) {
                throw std::runtime_error("[FrameBuffer] Framebuffer incomplete on resize");
            }
        }

        void bind() {
            glBindFramebuffer(GL_FRAMEBUFFER, buffer);
        }
    };
};