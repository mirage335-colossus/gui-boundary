module;

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <stdexcept>
#include <fstream>

export module Rev.Core.Resource;

export namespace Rev::Core {

    struct Resource {

        const unsigned char* data;
        size_t size;

        bool operator==(const Resource& other) const {
            return data == other.data && size == other.size;
        }

        // clever inlines resource bytes at transpile time and rewrites
        // File("./x") -> Resource::FromString(<bytes literal>, <size>). The
        // literal has static storage, so the pointer stays valid for the
        // program's lifetime. (The CMake build still uses FromFile + the atlas.)
        static Resource FromString(const char* data, size_t size)
        {
            return Resource{ reinterpret_cast<const unsigned char*>(data), size };
        }

        // Read a resource straight from disk at runtime.
        //  - "./" prefix  => resolved relative to `anchor` (the calling source file)
        //  - otherwise    => resolved relative to PROJECT_ROOT
        // The bytes live in a program-lifetime cache; the returned Resource is a
        // NON-OWNING view into that cache -- the same contract as an embedded
        // resource (stable pointer for the program's life). The file is re-read
        // when its on-disk timestamp changes, which is the hook for hot-reload
        // (re-call FromFile to pick up edits; previously returned views to that
        // path become stale after a reload).
        static Resource FromFile(const std::string& anchor, const std::string& relativePath)
        {
            namespace fs = std::filesystem;

            std::string clean = relativePath;
            std::replace(clean.begin(), clean.end(), '\\', '/');

            fs::path resolved;
            if (clean.starts_with("./"))
            {
                fs::path srcDir = fs::path(anchor).lexically_normal().parent_path();
                resolved = (srcDir / clean.substr(2)).lexically_normal();
            }
            else
            {
                resolved = (fs::path(PROJECT_ROOT) / clean).lexically_normal();
            }

            const std::string key = resolved.generic_string();

            // Program-lifetime byte cache + last-seen timestamps, keyed by path.
            static std::unordered_map<std::string, std::vector<unsigned char>> cache;
            static std::unordered_map<std::string, fs::file_time_type> stamps;

            std::error_code ec;
            fs::file_time_type mtime = fs::last_write_time(resolved, ec);

            auto it = cache.find(key);
            const bool missing = (it == cache.end());
            const bool changed  = (!ec && !missing && stamps[key] != mtime);

            // First time we're asked for this path and it isn't on disk: fail
            // loudly with the resolved path instead of silently returning an
            // empty Resource (which only blows up opaquely much later, when some
            // consumer tries to parse zero bytes of font/SVG/etc).
            if (missing && !fs::exists(resolved, ec))
            {
                const std::string msg =
                    "Rev::Core::Resource: file not found: \"" + key + "\""
                    " (requested \"" + relativePath + "\", "
                    + (clean.starts_with("./")
                          ? "relative to source \"" + anchor + "\""
                          : std::string("relative to PROJECT_ROOT \"") + PROJECT_ROOT + "\"")
                    + ")";
                // Surfaces via the runtime's terminate handler (what()) if
                // uncaught -- a clear message, no OS/stdio dependency.
                throw std::runtime_error(msg);
            }

            if (missing || changed)
            {
                // Read with the standard library only -- no OS-specific headers
                // in this core module (the OS layer, e.g. Rev::OS::File, owns
                // platform code). Binary mode; the bytes are cached for the
                // program's lifetime.
                std::vector<unsigned char> bytes;
                std::ifstream f(resolved, std::ios::binary);
                if (f)
                {
                    bytes.assign(std::istreambuf_iterator<char>(f),
                                 std::istreambuf_iterator<char>());
                }
                cache[key] = std::move(bytes);
                if (!ec) stamps[key] = mtime;
                it = cache.find(key);
            }

            return Resource{ it->second.data(), it->second.size() };
        }

    };
}