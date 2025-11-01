#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require

#include "input_scene.glsl"
#include "input_material.glsl"

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;

struct Vertex {
    vec3 position;
    float uv_x;
    vec3 normal;
    float uv_y;
    vec4 color;
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(push_constant) uniform constants {
    mat4          render_matrix;
    VertexBuffer  vertexBuffer;
} PushConstants;

void main()
{
    Vertex v = PushConstants.vertexBuffer.vertices[gl_VertexIndex];

    vec4 worldPos = PushConstants.render_matrix * vec4(v.position, 1.0);
    gl_Position   = sceneData.viewproj * worldPos;

    // keep exporting normal even if FS ignores it (avoids interface changes)
    outNormal = (PushConstants.render_matrix * vec4(v.normal, 0.0)).xyz;

    // pre-modulate vertex color by material colorFactors if you want tinting
    outColor = (v.color.xyz * materialData.colorFactors.xyz);

    outUV = vec2(v.uv_x, v.uv_y);
}
