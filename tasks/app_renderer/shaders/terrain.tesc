#version 460
#extension GL_KHR_vulkan_glsl : enable
#extension GL_ARB_separate_shader_objects : enable


layout(vertices = 4) out;



const int maxDivisions = 64;
const int lowerDivEvery = 3;



void main()
{
  if(gl_InvocationID == 0)
  {
  	vec2 maxPoint = max(
  	  max(gl_in[0].gl_Position.xz, gl_in[1].gl_Position.xz),
  	  max(gl_in[2].gl_Position.xz, gl_in[3].gl_Position.xz)
    );
  	vec2 minPoint = min(
  	  min(gl_in[0].gl_Position.xz, gl_in[1].gl_Position.xz),
  	  min(gl_in[2].gl_Position.xz, gl_in[3].gl_Position.xz)
    );
    vec2 centPoint = (maxPoint + minPoint) / 2;

  	vec2 farPoint = max(abs(maxPoint), abs(minPoint));
  	float maxDist = max(farPoint.x, farPoint.y);


  	int k = 1;
  	while(maxDist > lowerDivEvery && k < maxDivisions)
  	{
  	  maxDist -= lowerDivEvery;
  	  k *= 2;
  	}

  	gl_TessLevelOuter[0] = maxDivisions / k;
  	gl_TessLevelOuter[1] = maxDivisions / k;
  	gl_TessLevelOuter[2] = maxDivisions / k;
  	gl_TessLevelOuter[3] = maxDivisions / k;
  	gl_TessLevelInner[0] = maxDivisions / k;
  	gl_TessLevelInner[1] = maxDivisions / k;


    if (
    maxPoint.x != maxPoint.y &&
    maxPoint.x != -minPoint.y &&
    maxDist == 1.)
    {
      if (abs(centPoint.y / centPoint.x) < 1.)
      {
        if (maxPoint.x > 0)
          gl_TessLevelOuter[0] *= 2;
        else
          gl_TessLevelOuter[2] *= 2;
      }
      else
      {
        if (maxPoint.y > 0)
          gl_TessLevelOuter[1] *= 2;
        else
          gl_TessLevelOuter[3] *= 2;
      }
    }
  }

  gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
}