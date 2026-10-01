module;

#include <string>
#include <stdexcept>
#include <vector>

#include <glew/glew.h>
#include <dbg.hpp>

export module Rev.Graphics.Pipeline;

import Rev.Core.Resource;
import Rev.Graphics.Shader;
import Rev.NativeWindow;

export namespace Rev::Graphics {

    using namespace Rev::Core;

    struct Pipeline {

        enum Topology {
            TriangleFan = GL_TRIANGLE_FAN,
            TriangleList = GL_TRIANGLES,
            LineList = GL_LINES
        };

        GLuint id = 0;
        size_t users = 0;

        Shader* vert = nullptr;
        Shader* frag = nullptr;
        void* context = nullptr;

        struct Params {

            bool instanced = true;
            std::vector<size_t> attribs;
            std::string definitions = "";

            Resource openGlVert;
            Resource openGlFrag;

            Resource metalUniversal;

            Resource vulkanVert;
            Resource vulkanFrag;
        };

        // Create
        Pipeline(void* context, Params params) {

            this->context = context;
            NativeWindow::requireContext(context, "Pipeline construct");
            
            vert = new Shader(params.openGlVert, Shader::Stage::Vertex, params.definitions);
            frag = new Shader(params.openGlFrag, Shader::Stage::Fragment, params.definitions);

            id = glCreateProgram();
            glAttachShader(id, vert->shader);
            glAttachShader(id, frag->shader);
            glLinkProgram(id);

            GLint success;
            glGetProgramiv(id, GL_LINK_STATUS, &success);

            if (!success) {

                GLint logLength = 0;
                glGetProgramiv(id, GL_INFO_LOG_LENGTH, &logLength);

                std::string infoLog(static_cast<size_t>(logLength > 1 ? logLength : 1), '\0');
                glGetProgramInfoLog(id, logLength, nullptr, infoLog.data());
                
                dbg("Pipeline link error:\n%s", infoLog.c_str());
                throw std::runtime_error(std::string("Pipeline link error: ") + infoLog);
            }
        }

        // Destroy
        ~Pipeline() {

            NativeWindow::requireContext(context, "Pipeline destroy");

            delete vert;
            delete frag;

            if (id) {
                glDeleteProgram(id);
            }
        }

        void bind() {
            glUseProgram(id);
        }
    };
};