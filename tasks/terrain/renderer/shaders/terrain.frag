#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable



layout (location = 0 ) in VS_OUT
{
  vec3 pos;
  vec3 norm;
} surf;

layout(location = 0) out vec4 color;



void main()
{
  vec3 pl = normalize(vec3(1.0, 1.0, 1.0));
  float intens = max(0.0, dot(pl, surf.norm) * 0.75 + 0.25) + 0.05;
  
  color = vec4(vec3(intens), 1.0);
}