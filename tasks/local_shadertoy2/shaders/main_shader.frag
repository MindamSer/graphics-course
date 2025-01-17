#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform sampler2D colorTex;
layout(binding = 1) uniform sampler2D fileTex;

layout(push_constant) uniform params
{
  uvec2 iResolution;
  uvec2 iMouse;
  float iTime;
};



// consts
const float PI = 3.141592;

// render consts
const int MAX_ITER = 200;
const float MAX_DIST = 1e2;
const float STEP_EPS = 1e-3;
const float H = 1e-2;
const float BLICK_POW = 5e1;
const float SHADOW_K = 16.;
const float CORE_SMOOTH = 0.05;

// scene consts
const float NUCLEON_SIZE = 0.3;
const float NUCLEON_OFFSET = 0.4;
const float CORE_OMEGA = 0.;
const float ELECTRON_SIZE = 0.2;
const float ELECTRON_OFFSET = 2.;
const float ELECTRON_OMEGA = 0.5;



// render structures
struct camera {
    vec3 pos;
    vec3 dir;
    vec3 up;
    float fov;
};

struct light {
    vec3 pos;
    vec4 col;
};



// rotation matrices
mat3 rotateAxis(vec3 axis, float theta) {
    float c = cos(theta);
    float s = sin(theta);
    float x = axis.x;
    float y = axis.y;
    float z = axis.z;
    return mat3(
        vec3(c + (1. - c) * x * x, (1. - c) * x * y - s * z, (1. - c) * x * z + s * y),
        vec3((1. - c) * y * x + s * z, c + (1. - c) * y * y, (1. - c) * y * z - s * x),
        vec3((1. - c) * z * x - s * y, (1. - c) * z * y + s * x, c + (1. - c) * z * z)
    );
}

mat3 rotateX(float theta) {
    return rotateAxis(vec3(1., 0., 0.), theta);
}

mat3 rotateY(float theta) {
    return rotateAxis(vec3(0., 1., 0.), theta);
}

mat3 rotateZ(float theta) {
    return rotateAxis(vec3(0., 0., 1.), theta);
}



// sdf functions
float sdSphere(vec3 p, float s)
{
      return length(p) - s;
}

float opSmoothUnion( float d1, float d2, float k )
{
    float h = clamp( 0.5 + 0.5*(d2-d1)/k, 0.0, 1.0 );
    return mix( d2, d1, h ) - k*h*(1.0-h);
}

float sdCore(vec3 p, vec3 cent)
{
    p = rotateAxis(normalize(vec3(1., 1., 1.)), PI * CORE_OMEGA * iTime) * p;
    int nucleonCount = 12;
    
    vec3[] nucleons = vec3[] (
    NUCLEON_OFFSET * normalize(vec3(3., 1., 1.)), NUCLEON_OFFSET * normalize(vec3(3., -1., -1.)),
    NUCLEON_OFFSET * normalize(vec3(1., 3., 1.)), NUCLEON_OFFSET * normalize(vec3(-1., 3., -1.)),
    NUCLEON_OFFSET * normalize(vec3(-1., 1., 3.)), NUCLEON_OFFSET * normalize(vec3(1., -1., 3.)),
    NUCLEON_OFFSET * normalize(vec3(-3., 1., -1.)), NUCLEON_OFFSET * normalize(vec3(-3., -1., 1.)),
    NUCLEON_OFFSET * normalize(vec3(-1., -3., -1.)), NUCLEON_OFFSET * normalize(vec3(1., -3., 1.)),
    NUCLEON_OFFSET * normalize(vec3(1., 1., -3.)), NUCLEON_OFFSET * normalize(vec3(-1., -1., -3.))
    );
    
    float res = sdSphere(p - (cent + nucleons[0]), NUCLEON_SIZE);
    for (int i = 0; i < nucleonCount; i++)
    {
        res = opSmoothUnion(res, sdSphere(p - (cent + nucleons[i]), NUCLEON_SIZE), CORE_SMOOTH);
    }
    return res;
}

float sdOrbiltals(vec3 p, vec3 cent)
{
    int electronCount = 6;
    vec3[] electrons = vec3[] (
    rotateAxis(normalize(vec3(-3., 2., 0.)), PI * ELECTRON_OMEGA * iTime * 1.05) * ELECTRON_OFFSET * normalize(vec3(2., 3., 0.)),
    rotateAxis(normalize(vec3(3., 2., 0.)), PI * ELECTRON_OMEGA * iTime * 1.07) * ELECTRON_OFFSET * normalize(vec3(-2., 3., 0.)),
    rotateAxis(normalize(vec3(0., -3., 2.)), PI * ELECTRON_OMEGA * iTime * 1.11) * ELECTRON_OFFSET * normalize(vec3(0., 2., 3.)),
    rotateAxis(normalize(vec3(0., 3., 2.)), PI * ELECTRON_OMEGA * iTime * 1.13) * ELECTRON_OFFSET * normalize(vec3(0., -2., 3.)),
    rotateAxis(normalize(vec3(-3., 0., 2.)), PI * ELECTRON_OMEGA * iTime * 1.17) * ELECTRON_OFFSET * normalize(vec3(2., 0., 3.)),
    rotateAxis(normalize(vec3(3., 0., 2.)), PI * ELECTRON_OMEGA * iTime * 1.19) * ELECTRON_OFFSET * normalize(vec3(-2., 0., 3.))
    );
    
    float res = sdSphere(p - (cent + electrons[0]), ELECTRON_SIZE);
    for (int i = 0; i < electronCount; i++)
    {
        res = min(res, sdSphere(p - (cent + electrons[i]), ELECTRON_SIZE));
    }
    return res;
}



// scene sdf function
float sceneSDF(vec3 p )
{ 
    return min(sdCore(p, vec3(0.)), sdOrbiltals(p, vec3(0.)));
}



vec3 getNormal(vec3 p)
{
    float dx1 = sceneSDF(p + vec3(H, 0, 0));
    float dx2 = sceneSDF(p - vec3(H, 0, 0));
    float dy1 = sceneSDF(p + vec3(0, H, 0));
    float dy2 = sceneSDF(p - vec3(0, H, 0));
    float dz1 = sceneSDF(p + vec3(0, 0, H));
    float dz2 = sceneSDF(p - vec3(0, 0, H));
    
    return normalize(vec3(dx1 - dx2, dy1 - dy2, dz1 - dz2));
}



vec3 trace(in vec3 start, in vec3 dir, out bool hit)
{
	hit = false;
    vec3 p = vec3(0.);
    float t = 0.;
    float d = 0.;
    
    for (int i = 0; i < MAX_ITER; ++i)
    {
        p = start + dir * t;
        d = sceneSDF(p);
        t += d;
        
        if (d < STEP_EPS || t > MAX_DIST) break;
    }
    
    if (d < STEP_EPS) hit = true;
    
    return p;
}



float softshadow(in vec3 ro, in vec3 rd, float k)
{
    float res = 1.0;
    float t = STEP_EPS * 10.;
    for( int i=0; i<MAX_ITER && t<MAX_DIST; i++ )
    {
        float h = sceneSDF(ro + rd*t);
        if( h < STEP_EPS )
            return 0.0;
        res = min( res, k*h/t );
        t += h;
    }
    return res;
}



camera mainCam = camera(vec3(0., 0., 5.), vec3(0., 0., -1.), vec3(0., 1., 0.), 100.);

int lightCount = 6;
light[] ligths = light[] (
light(vec3(0., 4., 3.), vec4(1., 0., 0., 1.)),
light(vec3(-2., -3., 3.), vec4(0., 1., 0., 1.)),
light(vec3(2., -3., 3.), vec4(0., 0., 1., 1.)),
light(vec3(-2., 3., -3.), vec4(1., 1., 0., 1.)),
light(vec3(2., 3., -3.), vec4(1., 0., 1., 1.)),
light(vec3(0., -4., -3.), vec4(0., 1., 1., 1.))
);



void main()
{
    // angle of camera
    vec2 mouseTheta = PI * (vec2(iMouse).xy * 2. - vec2(iResolution).xy) / iResolution.x;
    float phi = mouseTheta.x;
    float theta = mouseTheta.y;
    
    // moving camera
    vec3 frameCameraPos = rotateY(phi) * mainCam.pos;
    vec3 frameCameraDir = rotateY(phi) * mainCam.dir;
    vec3 frameCameraUp = mainCam.up;
    vec3 frameCameraRight = cross(frameCameraDir, frameCameraUp);
    frameCameraPos = rotateAxis(frameCameraRight, -theta) * frameCameraPos;
    frameCameraDir = rotateAxis(frameCameraRight, -theta) * frameCameraDir;
    frameCameraUp = rotateAxis(frameCameraRight, -theta) * frameCameraUp;
    float cameraDepth = 1. / tan(radians(mainCam.fov / 2.));
    
    // computing ray direction
    vec2 uv = (vec2(gl_FragCoord).xy * 2. - vec2(iResolution).xy) / iResolution.x;
    vec3 rayDir = normalize(frameCameraDir * cameraDepth + frameCameraRight * uv.x + frameCameraUp * uv.y);
    
	// vec4 color = textureLod(colorTex, vec2(0.5) + 4.0 * uv, 0).rgba;
	vec4 color = vec4(vec3(0.), 1.);
	
    //tracing ray
    bool hit = false;
    vec3 p = trace(frameCameraPos, rayDir, hit);
	
    // process hit case
    if (hit)
    {
        color = vec4(vec3(0.), 1.);
        vec3 n = getNormal(p);
        vec3 w = abs(n);
        
        vec3 texel1 =
            w.x * textureLod(colorTex, vec2(0.5) + 1.5 * p.yz, 0).rgb +
            w.y * textureLod(colorTex, vec2(0.5) + 1.5 * p.xz, 0).rgb +
            w.z * textureLod(colorTex, vec2(0.5) + 1.5 * p.xy, 0).rgb;
		vec3 texel2 =
            w.x * textureLod(fileTex, vec2(0.5) + 0.35 * p.yz, 0).rgb +
            w.y * textureLod(fileTex, vec2(0.5) + 0.35 * p.xz, 0).rgb +
            w.z * textureLod(fileTex, vec2(0.5) + 0.35 * p.xy, 0).rgb;
        
        vec3 hc = normalize(frameCameraPos - p);
        for (int i = 0; i < lightCount; i++)
        {
            vec3 hl = normalize(ligths[i].pos - p);
            float brightness = softshadow(p, hl, SHADOW_K) * max(0., dot(n, hl));
            float blick = pow(brightness, BLICK_POW);
            color = color + (0.1 + 0.45 * brightness + 0.45 * blick) * ligths[i].col;
        }
        
        color =  color * vec4(texel1, 1.0) * vec4(texel2, 1.0);
    }

    fragColor = color;
}