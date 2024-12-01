#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable



layout(push_constant) uniform params
{
  vec2 res;
} pushConstant;

layout(binding = 0) uniform sampler2D colorTex;

layout (location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

layout(location = 0) out vec4 out_fragColor;



void main() {
    out_fragColor = textureLod(colorTex, surf.texCoord, 0);
}