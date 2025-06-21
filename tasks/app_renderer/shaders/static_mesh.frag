#version 460
#extension GL_KHR_vulkan_glsl : enable
#extension GL_GOOGLE_include_directive : require


struct Material
{
  vec4 baseColorFactor;
  vec4 metalRougFactor;
  int albedoTexIndex;
  int metRouTexIndex;
  int normalTexIndex;
  int _padding;
};


layout(binding = 0) readonly buffer InstanceMatricesBuffer
{
  mat4x4 instanceMatrices[];
};
layout(binding = 2) readonly buffer DrawMatricesIndiciesBuffer
{
  uint drawMatricesIndicies[];
};
layout(binding = 3) readonly buffer MaterialsBuffer
{
  Material materials[];
};
layout(binding = 4) uniform sampler2D textures[32];


layout(location = 0) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec3 wTangent;
  vec2 wTexCoord;
  flat int materialIndex;
} surf;

layout(location = 0) out vec4 out_albedo;

layout(location = 1) out vec4 out_normal;

layout(location = 2) out vec4 out_metRou;



void main()
{
  out_albedo = vec4(1.0f);
  out_normal = vec4(surf.wNorm, 1.0f);
  out_metRou = vec4(0.0f);


  if (surf.materialIndex != -1)
  {
    Material material = materials[surf.materialIndex];

    if (material.albedoTexIndex != -1)
    {
      out_albedo = texture(textures[material.albedoTexIndex], surf.wTexCoord);
    }
    else
    {
      out_albedo = material.baseColorFactor;
    }

    if (material.metRouTexIndex != -1)
    {
      out_metRou = texture(textures[material.metRouTexIndex], surf.wTexCoord);
    }
    else
    {
      out_metRou = material.metalRougFactor;
    }

    if (material.normalTexIndex != -1)
    {
      const vec3 surfaceNormal = surf.wNorm;
      const vec3 surfaceTangent = surf.wTangent;
      const vec3 biTangent = normalize(cross(surfaceNormal, surfaceTangent));
      const vec3 sampledNormal = texture(textures[material.normalTexIndex], surf.wTexCoord).xyz;

      const vec3 res = surfaceNormal * sampledNormal.x +
                       surfaceTangent * sampledNormal.y +
                       biTangent * sampledNormal.z;

      out_normal = vec4(normalize(res), 1.0f);
    }
  }
}
