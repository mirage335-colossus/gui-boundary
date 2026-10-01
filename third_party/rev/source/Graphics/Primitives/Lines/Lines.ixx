module;

#include <cstdint>
#include <cmath>
#include <vector>
#include <functional>
#include <sentinel.hpp>
#include <managed.hpp>

export module Rev.Primitive.Lines;

import Rev.Primitive;
import Rev.Core.Resource;
import Rev.Core.Shared;
import Rev.Core.Color;
import Rev.Core.Vertex;
import Rev.Core.Rect;

import Rev.Graphics.Canvas;
import Rev.Graphics.UniformBuffer;
import Rev.Graphics.VertexBuffer;
import Rev.Graphics.Pipeline;
import Rev.Graphics.Shader;

export namespace Rev::Primitives {

    using namespace sentinel;
    using namespace Rev::Core;
    using namespace Rev::Graphics;

    struct Lines : public Primitive {

        // Shared
        //--------------------------------------------------
        
        inline static Shared shared;
        inline static Pipeline* pipeline;

        void createShared() {

            pipeline = new Pipeline(canvas->context, {

                .instanced = false,
                .attribs = Vertex::attribs,

                .openGlVert = File("./Shaders/Lines.vert"),
                .openGlFrag = File("./Shaders/Lines.frag"),
                .metalUniversal = File("./Shaders/Lines.metal")
            });
        }

        void destroyShared() {
            delete pipeline;
        }

        // Instance
        //--------------------------------------------------

        // Instance-specific data
        struct Data {
            Color color;
            float depth = 0.0f;
            float opacity = 1.0f;
            float pad2 = 0.0f;
            float pad3 = 0.0f;
        };

        UniformBuffer* databuff = nullptr;
        VertexBuffer* vertices = nullptr;

        Data* data = nullptr;
        bool dirty = true;

        Color color;
        float strokeWidth = 1.0f;
        float smoothing = 1.0f;

        struct Line {

            std::vector<Vertex> points;
            std::vector<Vertex>* pPoints = nullptr;

            Color color = { 1, 1, 1, 1 };
            float strokeWidth = -0.0f;
            float smoothing = -0.0f;

            size_t segs, quads, joins, verts;

            // Allow valid pointer to override points
            std::vector<Vertex>& getPoints() {
                if (pPoints) { return *pPoints; }
                return points;
            }
        };

        std::vector<Line> lines;

        size_t numSegments, numQuads, numJoins, numVerts;

        // Create
        Lines(Canvas* canvas, std::vector<std::vector<Vertex>*> pLines = {}) : Primitive(canvas) {

            // Initialize lines with pLines if possible
            for (std::vector<Vertex>*& pPoints : pLines) {
                lines.push_back({ .pPoints = pPoints });
            }

            // Create shared pipeline
            shared.create([this]() { this->createShared(); });

            vertices = new VertexBuffer(canvas->context, { .attribs = Vertex::attribs });
            databuff = new UniformBuffer(canvas->context, sizeof(Data));

            data = (Data*)databuff->data;
            *data = Data();
        }

        // Destroy
        ~Lines() {

            shared.destroy([this]() { this->destroyShared(); });

            delete vertices;
            delete databuff;
        }

        void compute() override {

            // Reset
            numSegments = numQuads = numJoins = numVerts = 0;

            // Calculate needed quads/joins/verts
            for (Line& line : lines) {

                std::vector<Vertex>& points = line.getPoints();

                if (points.size() < 2) {
                    line.segs = 0;
                    line.quads = 0;
                    line.joins = 0;
                    line.verts = 0;
                    continue;
                }

                // Calculate for line
                line.segs = points.size() - 1;
                line.quads = line.segs;
                line.joins = line.segs - 1;
                line.verts = 6 * line.quads + 3 * line.joins;

                // Sum globally
                numVerts += line.verts;
            }

            // Resize vertex buffer to match needed/expected vertices
            vertices->resize(numVerts);

            size_t offset = 0;

            for (Line& line : lines) {

                std::vector<Vertex>& rPoints = line.getPoints();
                Vertex* pVerts = vertices->verts();
                
                int numTriangles = triangulatePolyline(

                    // Vertex data src / dst
                    rPoints.data(), rPoints.size(),
                    (pVerts + offset), line.verts,

                    // Color, stroke width, smoothing overrides
                    color ? color : line.color,
                    set(line.strokeWidth) ? line.strokeWidth : strokeWidth,
                    set(line.smoothing) ? line.smoothing : smoothing
                );

                offset += line.verts;
            }

            data->color = color;
        }

        void draw() override {

            pipeline->bind();

            databuff->bind(1);
            vertices->bind();
            
            canvas->drawArrays(Pipeline::Topology::TriangleList, 0, numVerts);
        }

        // Triangulation (build line out of quads)
        //--------------------------------------------------

        struct Quad {
            Vertex a, b, c, d; // a,b = inner; c,d = outer
        };

        struct Tri {
            Vertex a, b, c;
        };

        int32_t triangulatePolyline(
            
                Vertex* polyline, int32_t polylineCount,
                Vertex* triangles, int32_t triangleCount,
                Color color, float strokeWidth, float smoothing
            ) {

            if (polylineCount < 2) { return 0; }

            // Thickness and half-thickness
            const float thickness = smoothing ? strokeWidth + smoothing + 3.0f : strokeWidth;
            const float halfT = 0.5f * thickness;

            // Positive and negative edge distances
            const float edgeDistL = 0.5 * thickness - 0.5 * strokeWidth;
            const float edgeDistR = -1.0f * edgeDistL;

            int32_t triOut = 0;

            // --- Build initial segment quad ---
            Vertex A = polyline[0];
            Vertex B = polyline[1];

            Vertex dir{ B.x - A.x, B.y - A.y };
            float len = std::hypot(dir.x, dir.y);
            if (len == 0) { return 0; }

            dir.x /= len;
            dir.y /= len;
            Vertex perp{ -dir.y, dir.x };

            // Override color if none set
            bool posHasColor = (true && A.color);
            Core::Color aColor = posHasColor ? A.color : color;
            Core::Color bColor = posHasColor ? B.color : color;

            Quad thisQuad = {
                { A.x + perp.x * halfT, A.y + perp.y * halfT, aColor, edgeDistL, smoothing }, // A+
                { A.x - perp.x * halfT, A.y - perp.y * halfT, aColor, edgeDistR, smoothing }, // A-
                { B.x - perp.x * halfT, B.y - perp.y * halfT, bColor, edgeDistR, smoothing }, // B-
                { B.x + perp.x * halfT, B.y + perp.y * halfT, bColor, edgeDistL, smoothing }  // B+
            };

            // --- Iterate through remaining segments ---
            for (int32_t i = 1; i < polylineCount - 1; ++i) {

                Vertex C = polyline[i + 1];

                Vertex dirNext{ C.x - B.x, C.y - B.y };
                float lenNext = std::hypot(dirNext.x, dirNext.y);
                bool hasNext = (lenNext > 0);
                if (!hasNext) continue;

                dirNext.x /= lenNext;
                dirNext.y /= lenNext;
                Vertex perpNext{ -dirNext.y, dirNext.x };

                // Override color if none set
                bool posHasColor = (true && B.color);
                Core::Color bColor = posHasColor ? B.color : color;
                Core::Color cColor = posHasColor ? C.color : color;

                // Build next quad for BC
                Quad nextQuad = {
                    { B.x + perpNext.x * halfT, B.y + perpNext.y * halfT, bColor, edgeDistL, smoothing },
                    { B.x - perpNext.x * halfT, B.y - perpNext.y * halfT, bColor, edgeDistR, smoothing },
                    { C.x - perpNext.x * halfT, C.y - perpNext.y * halfT, cColor, edgeDistR, smoothing },
                    { C.x + perpNext.x * halfT, C.y + perpNext.y * halfT, cColor, edgeDistL, smoothing }
                };

                // Compute join geometry
                float cross = dir.x * dirNext.y - dir.y * dirNext.x;

                Vertex inner1, inner2, outer1, outer2;
                
                if (cross < 0) {
                    inner1 = thisQuad.b;
                    inner2 = nextQuad.b;
                    outer1 = thisQuad.d;
                    outer2 = nextQuad.a;
                } else {
                    inner1 = thisQuad.a;
                    inner2 = nextQuad.a;
                    outer1 = thisQuad.c;
                    outer2 = nextQuad.b;
                }

                float denom = dir.x * dirNext.y - dir.y * dirNext.x;
                Vertex innerJoin = inner2;
                if (std::fabs(denom) > 1e-3f) {
                    float t = ((inner2.x - inner1.x) * dirNext.y - (inner2.y - inner1.y) * dirNext.x) / denom;
                    innerJoin.x = inner1.x + dir.x * t;
                    innerJoin.y = inner1.y + dir.y * t;
                }

                // Construct and emit join triangle
                Tri joinCap{ outer1, outer2, innerJoin };
                triangles[triOut + 0] = joinCap.a;
                triangles[triOut + 1] = joinCap.b;
                triangles[triOut + 2] = joinCap.c;
                triOut += 3;

                // Apply shared vertex correction
                if (cross < 0) {
                    // Right turn → inside = right
                    thisQuad.c = innerJoin;  // current segment’s end inner
                    nextQuad.b = innerJoin;  // next segment’s start inner
                } else {
                    // Left turn → inside = left
                    thisQuad.d = innerJoin;  // current segment’s end inner
                    nextQuad.a = innerJoin;  // next segment’s start inner
                }

                // ✅ Emit corrected current quad (after join adjustment)
                triangles[triOut + 0] = thisQuad.a;
                triangles[triOut + 1] = thisQuad.b;
                triangles[triOut + 2] = thisQuad.c;
                triangles[triOut + 3] = thisQuad.a;
                triangles[triOut + 4] = thisQuad.c;
                triangles[triOut + 5] = thisQuad.d;
                triOut += 6;

                // Move forward
                thisQuad = nextQuad;
                A = B;
                B = C;
                dir = dirNext;
            }

            // Emit final segment (no next)
            triangles[triOut + 0] = thisQuad.a;
            triangles[triOut + 1] = thisQuad.b;
            triangles[triOut + 2] = thisQuad.c;
            triangles[triOut + 3] = thisQuad.a;
            triangles[triOut + 4] = thisQuad.c;
            triangles[triOut + 5] = thisQuad.d;
            triOut += 6;

            return triOut / 3;
        }
    };
};
