#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable

void main()
{
  vec2[] ds = {
  vec2(0,0),
  vec2(1,0),
  vec2(1,1),
  vec2(0,1)};

  vec2 uv = vec2(-8, -8) + vec2(gl_InstanceIndex % 16, gl_InstanceIndex / 16);

  uv += ds[gl_VertexIndex];
  uv *= 16;

  gl_Position = vec4(uv.x, 0.0, uv.y, 1.0);
}