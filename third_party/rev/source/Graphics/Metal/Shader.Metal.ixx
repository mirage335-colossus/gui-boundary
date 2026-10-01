module;

#include <string>
#include <stdexcept>

#include <dbg.hpp>
#include "./Helpers/MetalBackend.hpp"

export module Rev.Graphics.Shader;

import Rev.Core.Resource;

export namespace Rev::Graphics {

    using namespace Rev::Core;

    struct Shader {

        enum Stage {
            Vertex,
            Fragment,
            Universal
        };

        //GLuint shader = 0;

        void* shader = nullptr;

        // Create
        Shader(void* context, Resource shaderFile, Stage shaderType, std::string definitions = "") {

            // Copy source into a modifiable string
            const char* srcStr = reinterpret_cast<const char*>(shaderFile.data);
            std::string src(srcStr, shaderFile.size);

            // Replace "DEFINITIONS" with definitions
            size_t pos = src.find("DEFINITIONS");
            if (pos != std::string::npos) { src.replace(pos, 11, definitions); }

            //dbg(src.c_str());

            shader = metal_create_shader((MetalContext*)context, src.c_str(), src.size());
            if (!shader) { throw std::runtime_error("Failed to create shader!"); }
        }

        // Destroy
        ~Shader() {

            if (shader) {
                metal_destroy_shader(shader);
                shader = nullptr;
            }
        }
    };
};