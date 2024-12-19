#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable



layout(quads, equal_spacing, ccw) in;

layout(push_constant) uniform params
{
  mat4 mProjView;
  vec3 cameraPos;
} pushConstant;

layout(binding = 0) uniform sampler2D colorTex;

layout (location = 0 ) out VS_OUT
{
  vec3 pos;
  vec3 norm;
} vOut;



float H(vec2 coord)
{
	return textureLod(colorTex, coord, 0).r * 100.0f;
}

vec3 getNorm(vec2 coord, float scale)
{
	float h = scale / 2048.;

	float du_x = (H(coord + vec2(h, 0.)) - H(coord - vec2(h, 0.))) / (2 * 0.5);
	float du_y = (H(coord + vec2(0., h)) - H(coord - vec2(0., h))) / (2 * 0.5);

	return normalize(vec3(du_x, du_y, 1));
}



void main()
{
  int tileCount = 64;
  float tileSize = 16.;
  float hmScale = 8.;


  float u = gl_TessCoord.x;
  float v = gl_TessCoord.y;
  
  vec4 pos0 = gl_in[0].gl_Position;
  vec4 pos1 = gl_in[1].gl_Position;
  vec4 pos2 = gl_in[2].gl_Position;
  vec4 pos3 = gl_in[3].gl_Position;
  
  vec4 leftPos = pos0 + v * (pos3 - pos0);
  vec4 rightPos = pos1 + v * (pos2 - pos1);
  
  vec4 pos = leftPos + u * (rightPos - leftPos);


  pos.xz *= tileSize;
  pos.xz += tileSize * (ivec2(floor(pushConstant.cameraPos.xz / tileSize + 0.5)));


  vec2 hmCoord = hmScale * (pos.xz) / (tileCount * tileSize) + 0.5;
  pos.y = H(hmCoord);


  vOut.pos = pos.xyz;
  vOut.norm = getNorm(hmCoord, hmScale).xzy;
  
  gl_Position = pushConstant.mProjView * pos;
}