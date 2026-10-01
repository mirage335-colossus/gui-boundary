module;

#include <string>
#include <stdexcept>
#include <glew/glew.h>
#include <dbg.hpp>

export module Rev.Graphics.Shader;

import Rev.Core.Resource;

export namespace Rev::Graphics {

    using namespace Rev::Core;

    struct Shader {

        enum Stage {
            Vertex = GL_VERTEX_SHADER,
            Fragment = GL_FRAGMENT_SHADER
        };

        GLuint shader = 0;

        Shader(Resource shaderFile, Stage shaderType, std::string definitions = "") {

            // Copy source into a modifiable string
            const char* srcStr = reinterpret_cast<const char*>(shaderFile.data);
            std::string src(srcStr, shaderFile.size);

            // Replace "DEFINITIONS" with definitions
            size_t pos = src.find("DEFINITIONS");
            if (pos != std::string::npos) { src.replace(pos, 11, definitions); }

            // Prepare and compile
            const char* finalSrc = src.c_str();
            GLint finalLen = static_cast<GLint>(src.size());

            shader = glCreateShader(shaderType);
            glShaderSource(shader, 1, &finalSrc, &finalLen);
            glCompileShader(shader);

            // Check for errors
            GLint success;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

            if (!success) {
                GLint logLength = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);

                std::string infoLog(static_cast<size_t>(logLength > 1 ? logLength : 1), '\0');
                glGetShaderInfoLog(shader, logLength, nullptr, infoLog.data());

                dbg("Shader compilation failed:\n%s", infoLog.c_str());
                throw std::runtime_error(std::string("Shader compilation failed: ") + infoLog);
            }
        }

        ~Shader() {
            glDeleteShader(shader);
        }
    };
};
