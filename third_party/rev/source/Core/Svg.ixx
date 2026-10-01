module;

#include <cmath>
#include <cstring>
#include <stdexcept>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>

#include <dbg.hpp>

export module Rev.Core.Svg;

import Rev.Core.Resource;

import Rev.Graphics.Canvas;
import Rev.Graphics.Texture;

export namespace Rev::Core {

    using namespace Rev::Graphics;

    struct Svg {

        // Bitmap texture
        //--------------------------------------------------

        NSVGimage* image = nullptr;
        NSVGrasterizer* rast = nullptr;

        unsigned char* pixels = nullptr;
        float width = 0;
        float height = 0;
        float scale = 1.0f;

        Rev::Graphics::Canvas* canvas = nullptr;
        Rev::Graphics::Texture* texture = nullptr;

        struct Bitmap {
            
            size_t width, height;
            size_t size;

            unsigned char* data = nullptr;
        };

        Resource resource;
        Bitmap bitmap;

        Svg(Canvas* canvas, Resource resource) {

            this->canvas = canvas;
            this->resource = resource;

            bitmap.width = 1;
            bitmap.height = 1;

            /*texture = new Texture(canvas->context, {
                .data = bitmap.data,
                .width = bitmap.width, .height = bitmap.height,
                .channels = 4
            });*/
        }

        ~Svg() {

            if (bitmap.data) { delete[] bitmap.data; }
            if (texture) { delete texture; }
        }

        // CPU-only rasterization: parse `resource` and render it, aspect-fit and
        // centred, into a freshly allocated RGBA8 buffer of outW x outH. No GPU,
        // no Canvas -- the caller owns bitmap.data and frees it with delete[].
        // On any failure the returned Bitmap has data == nullptr. This is the
        // shared SVG pipeline; bake() layers a texture upload on top, and the
        // window icon path (NativeWindow) consumes the raw pixels directly.
        static Bitmap rasterize(const Resource& resource, int outW, int outH) {

            Bitmap out{};

            if (!resource.data || resource.size == 0) { return out; }
            if (outW <= 0 || outH <= 0) { return out; }

            // SVG text must be null-terminated for NanoSVG
            std::string svgText((char*)resource.data, resource.size);

            NSVGimage* image = nsvgParse((char*)svgText.c_str(), "px", 96.0f);
            if (!image) { return out; }

            if (image->width <= 0 || image->height <= 0) {
                nsvgDelete(image);
                return out;
            }

            // Allocate pixel buffer (RGBA 8-bit)
            out.width = outW; out.height = outH;
            out.size = out.width * out.height * 4 * sizeof(char);
            out.data = new unsigned char[out.size];
            std::memset(out.data, 0, out.size);

            NSVGrasterizer* rast = nsvgCreateRasterizer();
            if (!rast) {
                nsvgDelete(image);
                delete[] out.data;
                return Bitmap{};
            }

            // Aspect-fit and centre the artwork within the target box.
            float s = std::fmin(outW / image->width, outH / image->height);
            float tx = (outW - image->width  * s) * 0.5f;
            float ty = (outH - image->height * s) * 0.5f;

            nsvgRasterize(rast, image, tx, ty, s, out.data, out.width, out.height, out.width * 4);

            nsvgDeleteRasterizer(rast);
            nsvgDelete(image);

            return out;
        }

        void bake() {

            // Free old resources if rebaking
            if (texture) { delete texture; texture = nullptr; }
            if (bitmap.data) { delete[] bitmap.data; bitmap.data = nullptr; }

            // Render through the shared CPU pipeline...
            bitmap = rasterize(resource, (int)width, (int)height);

            if (!bitmap.data) {
                throw std::runtime_error("Svg::bake(): rasterization failed (empty/invalid SVG).");
            }

            // ...then upload the pixels to a GPU texture for drawing.
            texture = new Texture(canvas->context, {
                .data = bitmap.data,
                .width  = bitmap.width,
                .height = bitmap.height,
                .channels = 4,
                .filter = Texture::Filter::Bilinear
            });
        }
    };
};