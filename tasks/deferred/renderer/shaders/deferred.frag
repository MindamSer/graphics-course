#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable



layout(push_constant) uniform params
{
  uvec2 res;
  mat4 proj;
  mat4 view;
  mat4 projView;
  vec3 cameraPos;
  uint instanceCount;
  uint relemCount;
} pushConstant;

layout(binding = 0) uniform sampler2D albedoTex;

layout(binding = 1) uniform sampler2D normalTex;

layout(binding = 2) uniform sampler2D depthTex;



layout (location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

layout(location = 0) out vec4 out_fragColor;



void main() {
  float albedoValue = textureLod(albedoTex, surf.texCoord, 0).r;
  vec4 normalValue = textureLod(normalTex, surf.texCoord, 0);
  float depthValue = textureLod(depthTex, surf.texCoord, 0).r;

  vec4 screenSpaceCoords = vec4(surf.texCoord * 2.0f - 1.0f, depthValue, 1.0f);
  vec4 viewSpaceCoords = inverse(pushConstant.proj) * screenSpaceCoords;
  viewSpaceCoords /= viewSpaceCoords.w;

  out_fragColor = vec4(albedoValue);
  out_fragColor = normalValue;
  out_fragColor = vec4(depthValue);
  out_fragColor = normalValue;
}