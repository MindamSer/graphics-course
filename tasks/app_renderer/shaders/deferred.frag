#version 460
#extension GL_KHR_vulkan_glsl : enable


struct Material
{
  vec4 baseColorFactor;
  vec4 metalRougFactor;
  int albedoTexIndex;
  int metRouTexIndex;
  int normalTexIndex;
  int _padding;
};

struct LightSource
{
  vec4 pos;
  vec4 dir;
  vec4 color;
};


layout(push_constant) uniform params
{
  mat4 proj;
  mat4 view;
  uint lightsCount;
} pushConstant;


layout(binding = 0) uniform sampler2D albedoTex;
layout(binding = 1) uniform sampler2D normalTex;
layout(binding = 2) uniform sampler2D metRouTex;
layout(binding = 3) uniform sampler2D depthTex;
layout(binding = 4) readonly buffer LightSourcesBuffer
{
    LightSource LightSourcesBuf[];
};


layout (location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

layout(location = 0) out vec4 out_fragColor;



const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
  float a      = roughness*roughness;
  float a2     = a*a;
  float NdotH  = max(dot(N, H), 0.0);
  float NdotH2 = NdotH*NdotH;

  float num   = a2;
  float denom = (NdotH2 * (a2 - 1.0) + 1.0);
  denom = PI * denom * denom;

  return num / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
  float r = (roughness + 1.0);
  float k = (r*r) / 8.0;

  float num   = NdotV;
  float denom = NdotV * (1.0 - k) + k;

  return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
  float NdotV = max(dot(N, V), 0.0);
  float NdotL = max(dot(N, L), 0.0);
  float ggx2  = GeometrySchlickGGX(NdotV, roughness);
  float ggx1  = GeometrySchlickGGX(NdotL, roughness);

  return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
  return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}



void main() {
  // collecting fragment info
  vec3 fragAlbedo = texture(albedoTex, surf.texCoord).rgb;
  vec3 fragNormal = texture(normalTex, surf.texCoord).xyz;
  float fragDepth = texture(depthTex, surf.texCoord).r;
  float fragMetallic = 0.0f;
  float fragRoughness = 0.0f;
  {
    vec4 sampledMetRough = texture(metRouTex, surf.texCoord);
    fragMetallic = sampledMetRough.r;
    fragRoughness = sampledMetRough.g;
  }


  // calculating fragment normal, direction to eye
  vec4 viewSpaceFragCoords = inverse(pushConstant.proj) * vec4(surf.texCoord * 2.0f - 1.0f, fragDepth, 1.0f);
  viewSpaceFragCoords /= viewSpaceFragCoords.w;
  fragNormal = normalize(mat3(pushConstant.view) * fragNormal);
  vec3 fragToEyeDir = normalize(-viewSpaceFragCoords.xyz);


  // start calculating lighting

  vec3 F0 = vec3(0.04);
  F0 = mix(F0, fragAlbedo, fragMetallic);

  vec3 Lo = vec3(0.0);
  for (uint i = 0; i < pushConstant.lightsCount; ++i)
  {
    LightSource curLight = LightSourcesBuf[i];

    // calculating direction form fragment to light, halfway direction
    vec4 viewSpaceLightCoords = pushConstant.view * curLight.pos;
    viewSpaceLightCoords /= viewSpaceLightCoords.w;
    vec3 fragToLightDir = normalize(viewSpaceLightCoords.xyz - viewSpaceFragCoords.xyz);
    vec3 fragHalfWayDir = normalize(fragToEyeDir + fragToLightDir);

    // calculating light color and intensity
    vec3 lightColor = curLight.color.rgb;
    float lightIntensity = 1.0f;
    if(curLight.dir.xyz != vec3(0.f))
    {
        lightIntensity = max(0.f, pow(dot(curLight.dir.xyz, -fragToLightDir), 3));
    }


    // processing light source

    // Blinn Phong

    // float fragmentIntensity = max(dot(fragNormal, fragToLightDir), 0.0f);
    // vec3 resultColor = lightColor * lightIntensity * fragmentIntensity * fragAlbedo.rgb;

    // PBR

    float distance    = length(viewSpaceLightCoords.xyz - viewSpaceFragCoords.xyz);
    float attenuation = 1.0 / (distance * distance);
    attenuation = 1.0;
    vec3 radiance     = lightColor * attenuation;

    float NDF = DistributionGGX(fragNormal, fragHalfWayDir, fragRoughness);
    float G   = GeometrySmith(fragNormal, fragToEyeDir, fragToLightDir, fragRoughness);
    vec3 F    = fresnelSchlick(max(dot(fragHalfWayDir, fragToEyeDir), 0.0), F0);

    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - fragMetallic;

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(fragNormal, fragToEyeDir), 0.0) * max(dot(fragNormal, fragToLightDir), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator;

    float NdotL = max(dot(fragNormal, fragToLightDir), 0.0);
    Lo += (kD * fragAlbedo / PI + specular) * radiance * NdotL;
  }

  vec3 ambientColor = vec3(1.0f, 1.0f, 1.0f);
  float ambientIntensity = 0.1f;

  out_fragColor = vec4(ambientColor * ambientIntensity * fragAlbedo + Lo, 1.0f);
}