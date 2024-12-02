#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "unpack_attributes.glsl"

layout(binding = 0) readonly buffer MatricesBuffer
{
    mat4x4 InstMatricesBuf[];
};
layout(binding = 1) readonly buffer DrawMatricesIndBuffer
{
    uint DrawMatricesIndBuf[];
};

layout(location = 0) in vec4 vPosNorm;
layout(location = 1) in vec4 vTexCoordAndTang;

layout (location = 0 ) out VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec3 wTangent;
  vec2 texCoord;
} vOut;

layout(push_constant) uniform params
{
  mat4 mProjView;
  uint instanceCount;
  uint relemCount;
} pushConstant;

out gl_PerVertex { vec4 gl_Position; };

void main(void)
{
  uint curMatrixIndex = DrawMatricesIndBuf[gl_InstanceIndex];

  const vec4 wNorm = vec4(decode_baked_normal(floatBitsToUint(vPosNorm.w)),     0.0f);
  const vec4 wTang = vec4(decode_baked_normal(floatBitsToUint(vTexCoordAndTang.z)), 0.0f);
  
  vOut.wPos   = (InstMatricesBuf[curMatrixIndex] * vec4(vPosNorm.xyz, 1.0f)).xyz;
  vOut.wNorm  = normalize(mat3(transpose(inverse(InstMatricesBuf[curMatrixIndex]))) * wNorm.xyz);
  vOut.wTangent = normalize(mat3(transpose(inverse(InstMatricesBuf[curMatrixIndex]))) * wTang.xyz);
  vOut.texCoord = vTexCoordAndTang.xy;
  
  gl_Position   = pushConstant.mProjView * vec4(vOut.wPos, 1.0);
}
