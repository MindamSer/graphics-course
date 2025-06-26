#version 460
#extension GL_KHR_vulkan_glsl : enable
#extension GL_ARB_separate_shader_objects : enable


layout(push_constant) uniform params
{
  mat4 mProjView;
  vec3 cameraPos;
} pushConstant;


layout(binding = 0) uniform sampler2D colorTex;


layout(quads, equal_spacing, ccw) in;

layout (location = 0 ) out VS_OUT
{
  vec3 pos;
  vec3 norm;
} vOut;



const int tilePerSide = 64;
const int tilePerTextureSide = 1;
const float tileSize = 32.0f;
const float terrainHeight = 50.0f;
const float textureSize = 4096.0f;

float H(vec2 coord)
{
	return texture(colorTex, coord).r * terrainHeight;
}

vec3 getNorm(vec2 coord)
{
	float h = 1.0f / textureSize;

  float dhdx = (H(coord + vec2(h, 0.0f)) - H(coord - vec2(h, 0.0f))) / (2.0f * h);
	float dhdy = (H(coord + vec2(0.0f, h)) - H(coord - vec2(0.0f, h))) / (2.0f * h);

	return normalize(vec3(-dhdx / tileSize, 1.0f, -dhdy / tileSize));
}



void main()
{
  float u = gl_TessCoord.x;
  float v = gl_TessCoord.y;

  vec2 pos0 = gl_in[0].gl_Position.xz;
  vec2 pos1 = gl_in[1].gl_Position.xz;
  vec2 pos2 = gl_in[2].gl_Position.xz;
  vec2 pos3 = gl_in[3].gl_Position.xz;

  vec2 leftPos = pos0 + v * (pos3 - pos0);
  vec2 rightPos = pos1 + v * (pos2 - pos1);

  vec2 gridTerrainCenter = floor(pushConstant.cameraPos.xz / tileSize + 0.5);
  vec2 gridPosition = leftPos + u * (rightPos - leftPos);


  vec4 worldPosition = vec4(1.0f);
  worldPosition.xz = (gridTerrainCenter + gridPosition) * tileSize;

  vec2 hmCoord = worldPosition.xz / (tilePerTextureSide * tileSize);
  worldPosition.y = H(hmCoord);


  vOut.pos = worldPosition.xyz;
  vOut.norm = getNorm(hmCoord);

  gl_Position = pushConstant.mProjView * worldPosition;
}