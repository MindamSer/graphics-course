#version 460
#extension GL_KHR_vulkan_glsl : enable


layout(binding = 3) uniform sampler2D particleTextures[32];


layout (location = 0 ) in VS_OUT
{
    vec3 norm;
    vec2 texCoord;
    flat int particleTextureIndex;
    vec4 particleColor;
} vOut;

layout(location = 0) out vec4 out_fragColor;
layout(location = 1) out vec4 out_normal;
layout(location = 2) out vec4 out_metRou;



void main()
{
    vec4 sampledColor = texture(particleTextures[vOut.particleTextureIndex], vOut.texCoord);

    out_fragColor = sampledColor * vOut.particleColor;
    out_normal = vec4(vOut.norm, 1.0f);
}