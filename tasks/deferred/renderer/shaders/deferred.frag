#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable



struct LightSource
{
  vec3 pos;
  vec3 dir;
  vec3 color;
};



layout(push_constant) uniform params
{
  mat4 proj;
  mat4 view;
  mat4 projView;
  uint instanceCount;
  uint relemCount;
  uint lightsCount;
  vec3 cameraPos;
} pushConstant;

layout(binding = 0) uniform sampler2D albedoTex;

layout(binding = 1) uniform sampler2D normalTex;

layout(binding = 2) uniform sampler2D depthTex;

layout(binding = 3) readonly buffer LightSourcesBuffer
{
    LightSource LightSourcesBuf[];
};



layout (location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

layout(location = 0) out vec4 out_fragColor;



void main() {
  float albedoValue = textureLod(albedoTex, surf.texCoord, 0).r;
  vec3 normalValue = textureLod(normalTex, surf.texCoord, 0).xyz;
  float depthValue = textureLod(depthTex, surf.texCoord, 0).r;

  vec4 screenSpaceCoords = vec4(surf.texCoord * 2.0f - 1.0f, depthValue, 1.0f);
  mat4 projInv = inverse(pushConstant.proj);
  vec4 viewSpaceCoords = projInv * screenSpaceCoords;
  viewSpaceCoords /= viewSpaceCoords.w;

  out_fragColor = vec4(0.0f);

  for (uint i = 0; i < pushConstant.lightsCount; ++i)
  {
    LightSource curLight = LightSourcesBuf[i];
    vec3 viewSpaceLightCoords = (pushConstant.view * vec4(curLight.pos, 1.0f)).xyz;

    vec3 fragmentToLightDir = normalize(curLight.pos - viewSpaceCoords.xyz);

    float lightIntens = 1.0f;
    if(curLight.dir != vec3(0.f))
    {
        lightIntens = max(0.f, dot(curLight.dir, -fragmentToLightDir));
    }

    float fragmentIntens = max(0.f, dot(normalValue, fragmentToLightDir));

    vec4 resultColor = vec4(curLight.color * lightIntens * fragmentIntens * albedoValue, 1.f);

    out_fragColor += resultColor;
  }
}