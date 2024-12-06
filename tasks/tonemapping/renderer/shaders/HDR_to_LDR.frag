#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable



layout(binding = 0) uniform sampler2D colorTex;

layout(binding = 1) readonly buffer MaxLuminanceBuffer
{
  float maxLuminanceBuf[];
};

layout(binding = 2) readonly buffer LuminanceHistBuffer
{
  float luminanceHistBuf[];
};

layout (location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

layout(location = 0) out vec4 out_fragColor;



float log10(float x)
{
    return log(x) / log(10.f);
}

float minLum = 1.e-4f;
float minLogLum = log10(maxLuminanceBuf[0]);
float maxLogLum = log10(maxLuminanceBuf[1]);



void main() {
    vec4 HDRColor = textureLod(colorTex, surf.texCoord, 0);
    float logLum = log10(max( minLum, (0.3f * HDRColor.r + 0.59f * HDRColor.g + 0.11f * HDRColor.b) ));
    if (logLum > log10(minLum))
    {
      float normLogLum = (logLum - minLogLum) / (maxLogLum - minLogLum);
      int logLumLevel = int(floor(normLogLum * 256.0f));
      float histValue = luminanceHistBuf[logLumLevel];
      
      out_fragColor = vec4(maxLuminanceBuf[0] + (maxLuminanceBuf[1] - maxLuminanceBuf[0]) * histValue);
    }
    else
    {
      out_fragColor = HDRColor;
    }
}