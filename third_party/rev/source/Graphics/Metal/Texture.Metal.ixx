module;

#include <cstddef>

#include "./Helpers/MetalBackend.hpp"

export module Rev.Graphics.Texture;

export namespace Rev::Graphics {

    struct Texture {

        unsigned char* data = nullptr;
        size_t width = 0, height = 0;
        size_t channels = 0;
        size_t size = 0;

        void* context = nullptr;
        void* handle = nullptr;

        struct Params {
            
            unsigned char* data = nullptr;

            size_t width = 0, height = 0;
            size_t channels = 4;
        };

        // Create
        Texture(void* context, Params params) {
            
            this->context = context;

            data = params.data;
            width = params.width; height = params.height;
            channels = params.channels;

            // By default, use nearest filtering (matches OpenGL implementation)
            handle = metal_create_texture(
                (MetalContext*)context,
                static_cast<const unsigned char*>(data),
                static_cast<int>(width),
                static_cast<int>(height),
                static_cast<int>(channels)
            );
        }

        // Destroy
        ~Texture() {
            if (handle) {
                metal_destroy_texture(handle);
                handle = nullptr;
            }
        }

        void bind(int unit = 0) {
            if (handle) {
                metal_bind_texture((MetalContext*)context, handle, unit);
            }
        }

        void unbind(int unit = 0) {
            // Metal doesn't have a global unbind like OpenGL.
            // We could set a nil texture/sampler at this slot if desired.
            // For now, no-op to keep API symmetry.
        }

        void update(const unsigned char* pixels) {
            if (handle && pixels) { metal_update_texture(handle, pixels); }
        }
    };
};
