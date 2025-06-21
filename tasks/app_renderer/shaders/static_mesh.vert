#version 460
#extension GL_KHR_vulkan_glsl : enable
#extension GL_GOOGLE_include_directive : require

#include "unpack_attributes.glsl"


struct RenderElement
{
  uint vertexOffset;
  uint indexOffset;
  uint indexCount;
  int materialIndex;
};


layout(push_constant) uniform params
{
  mat4 mProjView;
} pushConstant;


layout(binding = 0) readonly buffer InstanceMatricesBuffer
{
  mat4x4 instanceMatrices[];
};
layout(binding = 1) readonly buffer RenderElementsBuffer
{
  RenderElement renderElements[];
};
layout(binding = 2) readonly buffer DrawMatricesIndiciesBuffer
{
  uint drawMatricesIndicies[];
};


layout(location = 0) in vec4 vPosNorm;

layout(location = 1) in vec4 vTexCoordAndTang;

layout(location = 0) out VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec3 wTangent;
  vec2 wTexCoord;
  flat int materialIndex;
} vOut;

out gl_PerVertex { vec4 gl_Position; };



void main(void)
{
  const vec3 vPos = vPosNorm.xyz;
  const vec3 vNorm = decode_baked_normal(floatBitsToUint(vPosNorm.w));
  const vec3 vTang = decode_baked_normal(floatBitsToUint(vTexCoordAndTang.z));
  const vec2 vTexCoord = vTexCoordAndTang.xy;

  const mat4x4 instanceMatrix = instanceMatrices[drawMatricesIndicies[gl_InstanceIndex]];


  vec4 wPos = instanceMatrix * vec4(vPos, 1.0f);
  gl_Position = pushConstant.mProjView * wPos;
  wPos = wPos / wPos.w;


  vOut.wPos      = wPos.xyz;
  vOut.wNorm     = normalize(mat3(transpose(inverse(instanceMatrix))) * vNorm);
  vOut.wTangent  = normalize(mat3(transpose(inverse(instanceMatrix))) * vTang);
  vOut.wTexCoord = vTexCoord;
  vOut.materialIndex = renderElements[gl_DrawID].materialIndex;
}
