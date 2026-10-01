module;

#include <cstddef>
#include <map>

export module Rev.Core.FontAtlas;

import Rev.Core.Font;
import Rev.Core.Resource;
import Rev.Graphics.Canvas;
import Rev.NativeWindow;

export namespace Rev::Core {

    struct FontAtlas {

        struct FontKey {

            const unsigned char* data;
            size_t size;
            float fontSize;
            float scale;

            bool operator<(const FontKey& other) const noexcept {
                if (data != other.data) return data < other.data;
                if (size != other.size) return size < other.size;
                if (fontSize != other.fontSize) return fontSize < other.fontSize;
                return scale < other.scale;
            }
        };

        Graphics::Canvas* canvas = nullptr;
        NativeWindow* resourceWindow = nullptr;
        std::map<FontKey, Font*> fonts;

        FontAtlas(Graphics::Canvas* canvas) {

            this->canvas = canvas;
            resourceWindow = canvas ? canvas->window : nullptr;
        }

        ~FontAtlas() {

            NativeWindow::ResourceContextGuard guard(resourceWindow);

            for (auto& [key, font] : fonts) {
                delete font;
            }
        }

        Font* get(Resource& resource, float fontSize, float scale) {

            FontKey key { resource.data, resource.size, fontSize, scale };

            auto it = fonts.find(key);
            if (it != fonts.end()) { return it->second; }

            NativeWindow::ResourceContextGuard guard(resourceWindow);

            Font* font = new Font(canvas, resource, fontSize, scale);
            fonts[key] = font;
            
            return font;
        }
    };
};