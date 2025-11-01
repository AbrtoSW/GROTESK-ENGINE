//input terrain glsl 
#extension GL_EXT_scalar_block_layout : require
struct TerrainInstance {
    vec2  originXZ;
    vec2  uvOrigin;
    vec2  uvScale;
    float worldScale;
    uint  tileIndex;
    uvec2 _pad;          // was float _pad; now 8B to make stride 40
};

layout(set = 1, binding = 0, scalar) readonly buffer TerrainBuf {
    TerrainInstance inst[];
} TI;