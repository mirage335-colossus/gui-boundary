#version 430 core
layout(location = 0) in vec4 iRect;
layout(location = 1) in vec4 iUv;
layout(std140, binding = 0) uniform Transform { mat4 uProjection; };
layout(std140, binding = 1) uniform Data { vec4 color; vec2 pos; float depth, opacity; };
out vec2 fragUV;
void main() {
    const vec2 corners[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(1,1),vec2(0,0),vec2(1,1),vec2(0,1));
    vec2 corner=corners[gl_VertexID%6];
    fragUV=mix(iUv.xy,iUv.zw,corner);
    gl_Position=uProjection*vec4(iRect.xy+iRect.zw*corner,0.0,1.0);
}
