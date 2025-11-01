//heightmap shader frag
#version 450
layout(location=1) in vec2 inUV;
layout(location=2) in vec2 inWorldXZ;

layout(location=0) out vec4 outFragColor;

layout(set=3, binding=0) uniform sampler2D diffuseTex;

// WORLD-SPACE brush UBO at set=1,binding=3
layout(set=1, binding=3) uniform BrushUBO {
    vec2  centerXZ;  // world meters
    float radiusM;   // meters
    float edgeM;     // meters
    vec4  color;
    float opacity;
} brush;

void main() {
    vec3 base = texture(diffuseTex, inUV).rgb;
    float d = distance(inWorldXZ, brush.centerXZ);
    float mask = 1.0 - smoothstep(brush.radiusM, brush.radiusM + brush.edgeM, d);
    vec3 rgb = mix(base, brush.color.rgb, brush.opacity * mask);
    outFragColor = vec4(rgb, 1.0);
}
