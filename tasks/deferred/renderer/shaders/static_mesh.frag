#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable



layout (location = 0) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec3 wTangent;
  vec2 texCoord;
} surf;

layout(location = 0) out vec4 out_albedo;

layout(location = 1) out vec4 out_normal;



void main()
{
  out_albedo = vec4(1.0f);
  out_normal = vec4(surf.wNorm, 1.0f);
}
