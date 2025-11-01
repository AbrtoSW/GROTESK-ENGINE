#version 450
#extension GL_GOOGLE_include_directive : require

#include "input_scene.glsl"
#include "input_material.glsl"

layout (location = 0) in vec3 inNormal; // unused on purpose
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;

layout (location = 0) out vec4 outFragColor;


void main()
{
    vec3 tex = texture(colorTex, inUV).rgb;  // colorTex should be in your material set
    vec3 rgb = inColor * tex;                // pure albedo; no ambient/sun/BRDF
    
    // outFragColor = vec4(1,0,1,1);
   outFragColor = vec4(tex, 1.0);
}
    