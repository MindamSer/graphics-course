#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable

void main()
{
  vec2 xy = gl_VertexIndex == 0 ? vec2(-0.5, -0.5) : (gl_VertexIndex == 1 ? vec2(0.5, -0.5) : vec2(0, 0.5));
  gl_Position = vec4(xy, 0.0, 1.0);
}