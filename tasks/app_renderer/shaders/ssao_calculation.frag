#version 460
#extension GL_KHR_vulkan_glsl : enable


layout(push_constant) uniform params
{
  mat4 proj;
  mat4 view;
  uvec2 res;
} pushConstant;


layout(binding = 0) uniform sampler2D normalTex;
layout(binding = 1) uniform sampler2D depthTex;
layout(binding = 2) uniform sampler2D noiseTex;
layout(binding = 3) readonly buffer KernelPositionsBuffer
{
    vec4 kernelPositions[];
};



layout (location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

layout(location = 0) out vec4 out_fragColor;



const int kernelSize = 64;
const float sampleRadius = 0.05f;

vec3 getPositionFromDepth(vec2 uv, float depth)
{
  vec4 viewSpaceFragCoords = inverse(pushConstant.proj) * vec4(uv * 2.0f - 1.0f, depth, 1.0f);
  return viewSpaceFragCoords.xyz / viewSpaceFragCoords.w;
}



void main() {
  vec2 fragUV = surf.texCoord;

  vec3 randomVector = texture(noiseTex, fragUV * (pushConstant.res / 4)).xyz;

  vec3 fragNormal = normalize(mat3(pushConstant.view) * texture(normalTex, fragUV).xyz);
  vec3 fragTangent = normalize(randomVector - fragNormal * dot(randomVector, fragNormal));
  vec3 fragBitangent = cross(fragNormal, fragTangent);

  mat3 tbn = mat3(
    fragTangent,
    fragBitangent,
    fragNormal
  );

  float fragDepth = texture(depthTex, fragUV).r;
  vec3 fragViewPosition = getPositionFromDepth(fragUV, fragDepth);


  float occlusion = 0.0f;

  for (int i = 0; i < kernelSize; ++i)
  {
    // get sample position:
    vec3 sampleViewPosition = fragViewPosition + tbn * kernelPositions[i].xyz * sampleRadius;

    // project sample position:
    vec4 sampleScreenPosition = pushConstant.proj * vec4(sampleViewPosition, 1.0);
    sampleScreenPosition /= sampleScreenPosition.w;
    sampleScreenPosition.xy = sampleScreenPosition.xy * 0.5 + 0.5;

    // get sample depth:
    float visibleSampleDepth = texture(depthTex, sampleScreenPosition.xy).r;
    vec3 visibleSampleViewPosition = getPositionFromDepth(sampleScreenPosition.xy, visibleSampleDepth);

    // range check & accumulate:
    float rangeCheck = abs(length(fragViewPosition) - length(visibleSampleViewPosition)) < sampleRadius ? 1.0 : 0.0;
    occlusion += (sampleScreenPosition.z >= visibleSampleDepth ? 1.0 : 0.0) * rangeCheck;
  }

  occlusion = 1.0 - (occlusion / kernelSize);


  out_fragColor = vec4(occlusion);
}