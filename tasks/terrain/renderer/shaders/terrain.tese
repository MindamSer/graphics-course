#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable

layout(quads, equal_spacing, ccw) in;

layout (location = 0 ) out VS_OUT
{
  vec3 pos;
  vec3 norm;
} vOut;

layout(binding = 0) uniform sampler2D colorTex;

layout(push_constant) uniform params
{
  mat4 mProjView;
  vec3 cameraPos;
  uint instanceCount;
  uint relemCount;
} pushConstant;

float H(vec2 coord)
{
	return textureLod(colorTex, coord, 0).r * 5.0;
}

vec3 getNorm(vec2 coord)
{
	float h = 1. / 4096.;

	float du_x = (H(coord + vec2(h, 0.)) - H(coord));
	float du_y = (H(coord + vec2(0., h)) - H(coord));

	vec3 dx = vec3(h, 0., du_x);
	vec3 dy = vec3(0., h, du_y);

	return normalize(cross(dx,dy));
}

void main()
{
  int tileCount = 64;
  float tileSize = 16.;
  
  float u = gl_TessCoord.x;
  float v = gl_TessCoord.y;
  
  vec4 pos0 = gl_in[0].gl_Position;
  vec4 pos1 = gl_in[1].gl_Position;
  vec4 pos2 = gl_in[2].gl_Position;
  vec4 pos3 = gl_in[3].gl_Position;
  
  vec4 leftPos = pos0 + v * (pos3 - pos0);
  vec4 rightPos = pos1 + v * (pos2 - pos1);
  
  vec4 pos = leftPos + u * (rightPos - leftPos);
  
  pos.xz += tileSize * (ivec2(pushConstant.cameraPos.xz + tileSize / 2) / int(tileSize));
  vec2 hmCoord = pos.xz / (tileCount * tileSize) + 0.5;
  pos.y = H(hmCoord);
  
  vOut.pos = pos.xyz;
  vOut.norm = getNorm(hmCoord).xyz;
  
  gl_Position = pushConstant.mProjView * pos;
}