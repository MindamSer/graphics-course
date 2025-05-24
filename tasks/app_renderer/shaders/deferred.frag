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
  uint lightsCount;
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
  float fragAlbedo = textureLod(albedoTex, surf.texCoord, 0).r;
  vec3 fragNormal = textureLod(normalTex, surf.texCoord, 0).xyz;
  float fragDepth = textureLod(depthTex, surf.texCoord, 0).r;



  vec4 screenSpaceFragCoords = vec4(surf.texCoord * 2.0f - 1.0f, fragDepth, 1.0f);

  vec4 viewSpaceFragCoords = inverse(pushConstant.proj) * screenSpaceFragCoords;
  viewSpaceFragCoords /= viewSpaceFragCoords.w;

  vec3 viewSpaceFragNormal = normalize(mat3(transpose(inverse(pushConstant.view))) * fragNormal);



  out_fragColor = vec4(fragAlbedo * 0.05f);

  for (uint i = 0; i < pushConstant.lightsCount; ++i)
  {
    LightSource curLight = LightSourcesBuf[i];


    vec4 viewSpaceLightCoords = pushConstant.view * vec4(curLight.pos, 1.0f);
    viewSpaceLightCoords /= viewSpaceLightCoords.w;


    vec3 fragmentToLightDir = normalize(viewSpaceLightCoords.xyz - viewSpaceFragCoords.xyz);

    float lightIntens = 1.0f;
    if(curLight.dir != vec3(0.f))
    {
        lightIntens = max(0.f, pow(dot(curLight.dir, -fragmentToLightDir), 3));
    }

    float fragmentIntens = max(0.f, dot(viewSpaceFragNormal, fragmentToLightDir));


    vec4 resultColor = vec4(curLight.color * lightIntens * fragmentIntens * fragAlbedo, 0.f);

    out_fragColor += resultColor;
  }

  out_fragColor.a = 1.0f;
}