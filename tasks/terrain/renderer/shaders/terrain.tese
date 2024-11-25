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
	float h = 0.003;
	float ux = (H(coord + vec2(h, 0)) - H(coord - vec2(h, 0)) / (2 * h));
	float uy = (H(coord + vec2(0, h)) - H(coord - vec2(0, h)) / (2 * h));
	vec3 dx = vec3(1., ux, 0.);
	vec3 dy = vec3(0., uy, 1.);
	return normalize(cross(dx, dy));
}

void main()
{
	float u = gl_TessCoord.x;
	float v = gl_TessCoord.y;

	vec4 pos0 = gl_in[0].gl_Position;
	vec4 pos1 = gl_in[1].gl_Position;
	vec4 pos2 = gl_in[2].gl_Position;
	vec4 pos3 = gl_in[3].gl_Position;

	vec4 leftPos = pos0 + v * (pos3 - pos0);
	vec4 rightPos = pos1 + v * (pos2 - pos1);

	vec4 pos = leftPos + u * (rightPos - leftPos);


	pos.xz += pushConstant.cameraPos.xz;

	vec2 hmCoord = pos.xz / 512.0 + 0.5;
	pos.y = H(hmCoord);

	vOut.pos = pos.xyz;
	vOut.norm = getNorm(hmCoord);

	gl_Position = pushConstant.mProjView * pos;
}