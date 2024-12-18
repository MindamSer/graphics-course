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
float Ldmin = 1.e-4f;
float Ldmax = 1.f;

float Lwmin = maxLuminanceBuf[0];
float Lwmax = maxLuminanceBuf[1];
float Bwmin = log10(Lwmin);
float Bwmax = log10(Lwmax);



void main() {
    vec4 HDRColor = textureLod(colorTex, surf.texCoord, 0);

    float Lw = 0.3f * HDRColor.r + 0.59f * HDRColor.g + 0.11f * HDRColor.b;
    if (Lw > minLum)
    {
      float Bw = log10(Lw);
      float normBw = (Bw - Bwmin) / (Bwmax - Bwmin);
      int BwLevel = min(int(floor(normBw * 256.0f)), 255);
      float histValue = luminanceHistBuf[BwLevel];
      
      float Bde = log10(Ldmin) + (log10(Ldmax) - log10(Ldmin)) * histValue;
      float Ld = pow(10, Bde);

      out_fragColor = vec4(vec3(Ld), 1.0);
    }
    else
    {
      out_fragColor = HDRColor;
    }

    out_fragColor = HDRColor;
}