module;

#include <cstdint>
#include <vector>

export module Rev.Graphics.CommandBuffer;

export namespace Rev::Graphics {

    struct CommandBuffer {

        struct Command {

            enum class Type {
                BindUbo, BindVbo, BindPipeline,
                DrawArrays, DrawArraysInstanced
            };

            Type type;
            void* subject;
            int32_t start, verticesPer, numInstances;
        };

        std::vector<Command> commands;
    };
};