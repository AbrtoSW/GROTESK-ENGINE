//heightmap shader vert
#version 450
#extension GL_EXT_buffer_reference : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

#include "input_scene.glsl"
#include "input_terrain.glsl"   // set=1,binding=0 -> TerrainBuf

layout(location=0) out vec3 outColor;
layout(location=1) out vec2 outUV;
layout(location=2) out vec2 outWorldXZ;

struct Vertex { vec3 position; float uv_x; vec3 normal; float uv_y; vec4 color; };
layout(buffer_reference, std430) readonly buffer VertexBuffer { Vertex vertices[]; };

layout(push_constant) uniform PushConstants {
    mat4         render_matrix;
    VertexBuffer vertexBuffer;
} PC;

// matches your descriptor (2D ARRAY)
layout(set=2, binding=0) uniform sampler2DArray heightTex;

void main() {
    Vertex v = PC.vertexBuffer.vertices[gl_VertexIndex];
    uint  i  = gl_InstanceIndex;

    // base UV
    vec2 uvBase = TI.inst[i].uvOrigin + vec2(v.uv_x, v.uv_y) * TI.inst[i].uvScale;

    // ---- seam fixes: texel-centered + clamped ----
    ivec2 Hsize = textureSize(heightTex, 0).xy;
    vec2  texel = 1.0 / vec2(Hsize);

    // map [0,1] to texel centers and clamp to [0.5/H, 1-0.5/H]
    vec2 uv = clamp(
        uvBase * ((vec2(Hsize) - 1.0) / vec2(Hsize)) + texel * 0.5,
        texel * 0.5,
        vec2(1.0) - texel * 0.5
    );

    // per-instance slice
    float layer = float(TI.inst[i].tileIndex);

    // height sample
    float h01 = textureLod(heightTex, vec3(uv, layer), 0.0).r;

    const float amplitude = 700.0;
    const float mid       = 0.5;
    float h = (h01 - mid) * amplitude;

    vec3 wp = vec3(
        v.position.x * TI.inst[i].worldScale + TI.inst[i].originXZ.x,
        h,
        v.position.z * TI.inst[i].worldScale + TI.inst[i].originXZ.y
    );

    gl_Position = sceneData.viewproj * PC.render_matrix * vec4(wp, 1.0);

    outColor   = v.color.rgb;
    outUV      = uv;
    outWorldXZ = wp.xz;
}
