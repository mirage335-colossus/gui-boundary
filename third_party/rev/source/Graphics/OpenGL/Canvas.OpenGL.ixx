module;

#include <stdexcept>
#include <vector>
#include <glew/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <dbg.hpp>

export module Rev.Graphics.Canvas;

import Rev.NativeWindow;
import Rev.Graphics.CommandBuffer;
import Rev.Graphics.FrameBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.UniformBuffer;

export namespace Rev::Graphics {

    struct Canvas {

        struct Flags {
            bool resize = true;
            bool record = true;
            bool stencil = false;
            bool color = false;
        };

        struct Details {
            size_t width, height;
            float scale = 1.0f;
        };

        // Context management
        void* context = nullptr;  // (context is unused)
        NativeWindow* window = nullptr;

        CommandBuffer* commandBuffer = nullptr;
        UniformBuffer* transform = nullptr;
        FrameBuffer* frameBuffer = nullptr;

        // Configurable details
        Details details;
        Flags flags;

        // Create
        Canvas(NativeWindow* window = nullptr) {

            this->window = window;
            context = window;

            window->createContext();
            window->makeContextCurrent();
            window->loadGlFunctions();

            glEnable(GL_MULTISAMPLE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            transform = new UniformBuffer(context, sizeof(glm::mat4));
            frameBuffer = new FrameBuffer(context, { .width = 1, .height = 1 });
        }

        bool isCurrent() const {

            return window && window->isContextCurrent();
        }

        void requireCurrent(const char* operation) const {

            if (isCurrent()) { return; }

            NativeWindow::requireContext(window, operation);
        }

        // Destroy
        ~Canvas() {
            delete transform;
            delete frameBuffer;
        }

        // Frame setup / blitting
        //--------------------------------------------------

        void beginFrame() {
            
            if (!window) { return; }

            window->makeContextCurrent();

            size_t windowWidth = window->size.w ? static_cast<size_t>(window->size.w) : 1;
            size_t windowHeight = window->size.h ? static_cast<size_t>(window->size.h) : 1;

            if (details.width != windowWidth || details.height != windowHeight || details.scale != window->scale) {
                flags.resize = true;
            }

            // Ensure cache coherency (wait for flush) before proceeding
            // (this is because any changes to buffers need to make it to
            // ram before we can tell the GPU everything is good)
            //glMemoryBarrier(GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT | GL_UNIFORM_BARRIER_BIT);

            // If canvas needs to adjust size to window
            if (flags.resize) {

                // Get width and height from window size
                details.width = windowWidth;
                details.height = windowHeight;
                details.scale = window->scale;

                glViewport(0, 0, static_cast<GLsizei>(details.width), static_cast<GLsizei>(details.height));

                glm::mat4 projection = glm::ortho(
                    0.0f,                                      // left
                    (static_cast<float>(details.width) / details.scale),  // right
                    (static_cast<float>(details.height) / details.scale),  // bottom
                    0.0f,                                      // top
                    -1.0f, 1.0f
                );

                // Resize framebuffer, bind transform
                frameBuffer->resize(details.width, details.height);
                transform->set(&projection);
                flags.record = true;
                flags.resize = false;
            }

            // Framebuffer
            frameBuffer->bind();
            glViewport(0, 0, static_cast<GLsizei>(details.width), static_cast<GLsizei>(details.height));
            glEnable(GL_MULTISAMPLE);
            glDisable(GL_DEPTH_TEST);

            // Blend func and color
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glBlendColor(1.0f, 1.0f, 1.0f, 1.0f);

            // Stencil
            glEnable(GL_STENCIL_TEST);
            glStencilMask(0xFF);
            flags.stencil = true;
            flags.color = true;

            glClearStencil(0x00);
            glClearDepth(1.0);
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

            glClear(
                GL_COLOR_BUFFER_BIT |
                GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT
            );

            transform->bind(0);
        }

        // We end the frame by blitting and swapping buffers (present)
        void endFrame() {

            if (!window) { return; }
            requireCurrent("Canvas endFrame");

            size_t targetWidth = window->size.w ? static_cast<size_t>(window->size.w) : 1;
            size_t targetHeight = window->size.h ? static_cast<size_t>(window->size.h) : 1;

            // Bind both render target and actual (window) framebuffer
            glBindFramebuffer(GL_READ_FRAMEBUFFER, frameBuffer->buffer);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

            // Ensure any newly exposed default-framebuffer area is initialized.
            glViewport(0, 0, static_cast<GLsizei>(targetWidth), static_cast<GLsizei>(targetHeight));
            glDisable(GL_SCISSOR_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            // Copy (blit). Use the actual drawable size as the destination so
            // live-resize frames never leave newly exposed regions untouched.
            glBlitFramebuffer(
                0, 0, static_cast<GLint>(details.width), static_cast<GLint>(details.height),
                0, 0, static_cast<GLint>(targetWidth), static_cast<GLint>(targetHeight),
                GL_COLOR_BUFFER_BIT, GL_NEAREST
            );

            // Unbind and swap
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            window->swapBuffers();
        }

        // Stencil management
        //--------------------------------------------------

        // Enable / disable writing to color buffer
        void colorWrite(bool enable) {

            // Avoid redundant state changes
            if (enable == flags.color) { return; }
            else { flags.color = enable; }

            // Set color mask to enable/disable writing
            if (enable) { glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); }
            else { glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); }
        }

        // Enable / disable writing to stencil buffer
        void stencilWrite(bool enable) {

            // Avoid redundant state changes
            if (enable == flags.stencil ) { return; }
            else { flags.stencil = enable; }

            // Set stencil mask to enable/disable writing
            if (enable) { glStencilMask(0xFF); }
            else { glStencilMask(0x00); }
        }

        void stencilReset(size_t value) {
            stencilWrite(true);
            stencilFill(value);
            stencilDepth(value);
            stencilWrite(false);
        }

        // Set stencil depth
        void stencilDepth(size_t value) {
            glStencilFunc(GL_LEQUAL, value, 0xFF);
        }

        // Set to all zeroes
        void stencilClear() {
            glClearStencil(0.0f);
            glClear(GL_STENCIL_BUFFER_BIT);
        }

        // Fill stencil buffer with uniform value(s)
        void stencilFill(size_t value) {
            glClearStencil(value);
            glClear(GL_STENCIL_BUFFER_BIT);
        }

        // Pushing to stencil (increasing depth where test passes)
        void stencilPush(size_t depth) {
            glStencilFunc(GL_LEQUAL, depth, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
        }

        // Popping from stencil (decreasing depth where test passes)
        void stencilPop(size_t depth) {
            glStencilFunc(GL_LEQUAL, depth, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
        }

        // Setting stencil (set depth where test passes)
        void stencilSet(size_t depth) {
            glStencilFunc(GL_LEQUAL, depth, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        }

        // 3D render state
        //--------------------------------------------------
        //
        // Depth testing is off during the 2D pass (see beginFrame); a 3D pass turns
        // it on, then off again. Exposed as canvas verbs so the layers above never
        // touch raw GL state.

        void depthTest(bool enable) {
            if (enable) {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
            }
            else {
                glDisable(GL_DEPTH_TEST);
            }
        }

        void depthWrite(bool enable) {
            glDepthMask(enable ? GL_TRUE : GL_FALSE);
        }

        // Polygon depth bias (glPolygonOffset). Negative values pull fragments
        // TOWARD the camera (smaller depth), positive push them away. Used to lift a
        // coplanar overlay (e.g. a transparent ghost) off the surface it coincides with,
        // so it wins/loses the depth test deterministically instead of z-fighting.
        // factor == units == 0 disables it.
        void depthBias(float factor, float units) {
            if (factor == 0.0f && units == 0.0f) {
                glDisable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(0.0f, 0.0f);
            }
            else {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(factor, units);
            }
        }

        void clearDepth() {
            glClearDepth(1.0);
            glClear(GL_DEPTH_BUFFER_BIT);
        }

        // Drawing functions
        //--------------------------------------------------

        void drawArrays(Pipeline::Topology topology, size_t start, size_t verticesPer) {
            glDrawArrays(topology, start, verticesPer);
        }

        void drawArraysInstanced(Pipeline::Topology topology, size_t start, size_t verticesPer, size_t numInstances) {
            glDrawArraysInstanced(topology, start, verticesPer, numInstances);
        }
    };
};