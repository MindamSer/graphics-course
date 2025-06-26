#version 460
#extension GL_KHR_vulkan_glsl : enable
#extension GL_ARB_separate_shader_objects : enable



const int tilePerSide = 64;

const vec2[] ds = {
  vec2(0,0),
  vec2(1,0),
  vec2(1,1),
  vec2(0,1)
};



void main()
{
  vec2 uv =
    vec2(-tilePerSide / 2, -tilePerSide / 2) +
    vec2(gl_InstanceIndex % tilePerSide, gl_InstanceIndex / tilePerSide) +
    ds[gl_VertexIndex];

  gl_Position = vec4(uv.x, 0.0, uv.y, 1.0);
}