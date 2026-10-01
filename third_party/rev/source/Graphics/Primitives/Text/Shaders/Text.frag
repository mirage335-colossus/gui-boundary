#version 430 core

in vec2 fragUV;
out vec4 FragColor;

// Texture sampler bound at texture unit 0
layout(binding = 0) uniform sampler2D tex;

// Per-instance color data bound at UBO binding 1
layout(std140, binding = 1) uniform Data {
    vec4 color;
    vec2 pos;
    float depth, opacity;
};

void main() {

    float a = texture(tex, fragUV).r;

    // --- Hard contrast reconstruction ---
    // Center threshold around 0.5 with a very narrow blend band
    const float lo = 0.0;
    const float hi  = 0.9;

    //float punchy = smoothstep(lo, hi, a + 0.05);

    //FragColor = vec4(color.rgb, punchy * color.a);
    FragColor = vec4(color.rgb, color.a * a);
    gl_FragDepth = depth;

    FragColor.a *= opacity; // Opacity
}