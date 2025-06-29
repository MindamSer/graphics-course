#version 460
#extension GL_KHR_vulkan_glsl : enable


struct Particle
{
    vec4 position;
    vec4 velocity;
    vec4 color;

    uint textureId;
    float textureScale;
    float gravityScale;
    float lifetime;
};


layout (location = 0 ) out VS_OUT
{
    vec3 norm;
    vec2 texCoord;
    flat uint particleTextureIndex;
    vec4 particleColor;
} vOut;

layout(push_constant) uniform params
{
    mat4x4 projView;
    vec4 cameraPos;
    uint emittersCount;
    float dt;
} pushConstant;


layout(binding = 0) readonly buffer EmittersOrderBuffer
{
    uint emittersOrder[];
};
layout(binding = 1) readonly buffer ParticlesBuffersBuffer
{
    Particle v[256];
} particlesBuffers[32];
layout(binding = 2) readonly buffer ParticlesOrderBuffersBuffer
{
    uint v[256];
} particlesOrderBuffers[32];



const vec2[] ds = {
  vec2(-1.0f, -1.0f),
  vec2(-1.0f, +1.0f),
  vec2(+1.0f, -1.0f),
  vec2(+1.0f, +1.0f)
};



void main()
{
    uint emitterIndex = emittersOrder[gl_DrawID];
    uint particleIndex = particlesOrderBuffers[emitterIndex].v[gl_InstanceIndex];

    vec3 particlePosition = particlesBuffers[emitterIndex].v[particleIndex].position.xyz;
    float dist = length(pushConstant.cameraPos.xyz - particlePosition);


    vec4 particleScreenPosition = vec4(particlePosition, 1.0f);
    particleScreenPosition = pushConstant.projView * particleScreenPosition;
    particleScreenPosition /= particleScreenPosition.w;

    vec2 delta;
    if (gl_VertexIndex == 0)
        delta = ds[0];
    else if (gl_VertexIndex == 5)
        delta = ds[3];
    else if (gl_VertexIndex == 1 || gl_VertexIndex == 4)
        delta = ds[1];
    else if (gl_VertexIndex == 2 || gl_VertexIndex == 3)
        delta = ds[2];

    vec3 vertexScreenPosition = particleScreenPosition.xyz +
        vec3(delta, 0.0f) * particlesBuffers[emitterIndex].v[particleIndex].textureScale / dist;

    gl_Position = vec4(vertexScreenPosition, 1.0);


    vOut.norm = normalize(pushConstant.cameraPos.xyz - particlesBuffers[emitterIndex].v[particleIndex].position.xyz);
    vOut.texCoord = (delta + 1.0f) / 2.0f;
    vOut.particleTextureIndex = particlesBuffers[emitterIndex].v[particleIndex].textureId;
    vOut.particleColor = particlesBuffers[emitterIndex].v[particleIndex].color;
}