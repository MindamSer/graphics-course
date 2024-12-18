#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable



layout(binding = 0, r32f) readonly uniform image2D albedoTex;

layout(binding = 1, rgba8) readonly uniform image2D normalTex;

layout(binding = 2, r32f) readonly uniform image2D depthTex;



layout (location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

layout(location = 0) out vec4 out_fragColor;



void main() {
  ivec2 pixelCoord = ivec2(gl_FragCoord.xy);

  float albedoValue = imageLoad(normalTex, pixelCoord).r;
  vec4 normalValue = imageLoad(normalTex, pixelCoord);
  float depthValue = imageLoad(normalTex, pixelCoord).r;

  out_fragColor = vec4(albedoValue);
  out_fragColor = normalValue;
  out_fragColor = vec4(depthValue);
  out_fragColor = normalValue;
}