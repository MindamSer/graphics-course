#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable

void main()
{
  int tileCount = 64;
  float tileSize = 16.;
	
  vec2[] ds = {
  vec2(0,0),
  vec2(1,0),
  vec2(1,1),
  vec2(0,1)};

  vec2 uv = 
  vec2(-tileCount / 2, -tileCount / 2) +
  vec2(gl_InstanceIndex % tileCount, gl_InstanceIndex / tileCount) +
  ds[gl_VertexIndex];

  uv *= tileSize;

  gl_Position = vec4(uv.x, 0.0, uv.y, 1.0);
}