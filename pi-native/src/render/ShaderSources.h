#pragma once
// GLSL ES 3.00 sources. Lighting reproduces three.js r160 conventions (physically based light
// falloff, BRDF_Lambert = albedo / PI, sRGB output, then exp2 fog in output space) so existing
// config values (lightIntensity 1500, ambientIntensity 0.5, ...) look the same as in index.html.

namespace it::shaders {

// ----------------------------------------------------------------------------- shared
#define IT_GLSL_COMMON                                                                          \
    "const float PI = 3.141592653589793;\n"                                                     \
    "vec3 linearToSrgb(vec3 c) {\n"                                                             \
    "  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));\n" \
    "}\n"                                                                                       \
    "float distanceFalloff(float d, float cutoff) {\n"                                          \
    "  float f = 1.0 / max(d * d, 0.01);\n"                                                     \
    "  if (cutoff > 0.0) { float r = d / cutoff; float w = clamp(1.0 - r * r * r * r, 0.0, 1.0); f *= w * w; }\n" \
    "  return f;\n"                                                                             \
    "}\n"                                                                                       \
    "vec3 applyFog(vec3 c, float density, float depth) {\n"                                     \
    "  float f = 1.0 - exp(-density * density * depth * depth);\n"                             \
    "  return mix(c, vec3(0.0), clamp(f, 0.0, 1.0));\n"                                         \
    "}\n"

// ----------------------------------------------------------------------------- card
// Image cards for floating / tunnel / grid: Lambert, double-sided, up to 8 point lights
// (caps::kMaxLightsPerDraw: 4 on the Pi, 8 on desktop).
inline const char* kCardVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uUvXform;
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUv;
out float vFogDepth;
void main() {
  vec4 w = uModel * vec4(aPos, 1.0);
  vWorld = w.xyz;
  vNormal = mat3(uModel) * aNrm;
  vUv = (uUvXform * vec3(aUv, 1.0)).xy;
  vec4 mv = uView * w;
  vFogDepth = -mv.z;
  gl_Position = uProj * mv;
}
)";

inline const char* kCardFS = "#version 300 es\nprecision highp float;\n" IT_GLSL_COMMON R"(
uniform sampler2D uTex;
uniform vec3 uAmbient;
uniform int uNumLights;
uniform vec3 uLightPos[8];
uniform vec3 uLightColor[8];
uniform float uLightRange[8];
uniform float uFogDensity;
uniform float uOpacity;
uniform float uAlphaCut;
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUv;
in float vFogDepth;
out vec4 fragColor;
void main() {
  vec4 tex = texture(uTex, vUv);
  float a = tex.a * uOpacity;
  if (a < uAlphaCut) discard;
  vec3 n = normalize(vNormal);
  if (!gl_FrontFacing) n = -n;
  vec3 irr = uAmbient;
  for (int i = 0; i < 8; ++i) {
    if (i >= uNumLights) break;
    vec3 L = uLightPos[i] - vWorld;
    float d = length(L);
    irr += uLightColor[i] * distanceFalloff(d, uLightRange[i]) * max(dot(n, L / max(d, 1e-4)), 0.0);
  }
  vec3 c = linearToSrgb(tex.rgb * irr / PI);
  fragColor = vec4(applyFog(c, uFogDensity, vFogDepth), a);
}
)";

// ----------------------------------------------------------------------------- maze
// Walls / floor / ceiling / frames / paintings: Lambert + optional normal map. Specular is either a
// faint fixed Blinn-Phong lobe or, with a roughness map (desktop), a normalised Blinn-Phong lobe whose
// exponent comes from the texel roughness (approximating three's MeshStandardMaterial).
// Up to 6 spotlights (4 on the Pi) + exit point light; any spot can own a layer of the shadow array.
inline const char* kMazeVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aTan;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec2 uUvScale;
uniform int uWorldUv;      // 1: uv from world XZ (floor/ceiling, no texture swimming)
out vec3 vWorld;
out vec3 vN;
out vec4 vT;
out vec2 vUv;
out float vFogDepth;
invariant gl_Position;  // must match kMazeDepthVS bit-for-bit (depth pre-pass)
void main() {
  vec4 w = uModel * vec4(aPos, 1.0);
  vWorld = w.xyz;
  vN = normalize(mat3(uModel) * aNrm);
  vT = vec4(normalize(mat3(uModel) * aTan.xyz), aTan.w);
  vUv = (uWorldUv == 1 ? w.xz : aUv) * uUvScale;
  vec4 mv = uView * w;
  vFogDepth = -mv.z;
  gl_Position = uProj * mv;
}
)";

inline const char* kMazeFS = "#version 300 es\nprecision highp float;\nprecision highp sampler2DArrayShadow;\n" IT_GLSL_COMMON R"(
uniform sampler2D uAlbedo;
uniform sampler2D uNormalMap;
uniform sampler2D uRoughMap;
uniform sampler2DArrayShadow uShadowMaps;
uniform int uUseNormal;
uniform int uUseRough;
uniform vec3 uTint;            // linear
uniform float uOpacity;
uniform vec3 uAmbient;         // linear irradiance
uniform vec3 uCamPos;
uniform int uNumSpots;
uniform vec3 uSpotPos[6];
uniform vec3 uSpotDir[6];
uniform vec3 uSpotColor[6];    // linear colour * intensity
uniform float uSpotCos[6];
uniform float uSpotPenCos[6];
uniform float uSpotRange[6];
uniform int uSpotShadow[6];    // layer in uShadowMaps, -1 = unshadowed
uniform mat4 uShadowMats[6];   // world -> shadow-map [0,1]^3 per spot
uniform int uHasPoint;
uniform vec3 uPointPos;
uniform vec3 uPointColor;
uniform float uPointRange;
uniform float uSpecular;
uniform float uFogDensity;
in vec3 vWorld;
in vec3 vN;
in vec4 vT;
in vec2 vUv;
in float vFogDepth;
out vec4 fragColor;

float gShininess = 32.0;
float gSpecK = 0.0;

float shadowFactor(int spot) {
  vec4 s = uShadowMats[spot] * vec4(vWorld, 1.0);
  vec3 p = s.xyz / s.w;
  if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
  return texture(uShadowMaps, vec4(p.xy, float(uSpotShadow[spot]), p.z - 0.0015));
}

void addLight(vec3 n, vec3 v, vec3 L, vec3 radiance, inout vec3 diff, inout vec3 spec) {
  float ndl = max(dot(n, L), 0.0);
  diff += radiance * ndl;
  if (gSpecK > 0.0) {  // paintings skip the pow entirely
    vec3 h = normalize(L + v);
    spec += radiance * ndl * pow(max(dot(n, h), 0.0), gShininess);
  }
}

void main() {
  vec4 albedo = texture(uAlbedo, vUv);
  albedo.rgb *= uTint;
  vec3 n = normalize(vN);
  if (uUseNormal == 1) {
    vec3 t = normalize(vT.xyz - n * dot(n, vT.xyz));
    vec3 b = cross(n, t) * vT.w;
    vec3 m = texture(uNormalMap, vUv).xyz * 2.0 - 1.0;
    n = normalize(mat3(t, b, n) * m);
  }
  gSpecK = uSpecular;
  if (uUseRough == 1) {
    // roughness -> Blinn-Phong exponent (2/r^4 - 2), normalised, dielectric F0 = 0.04
    float r = clamp(texture(uRoughMap, vUv).g, 0.05, 1.0);
    float r4 = r * r * r * r;
    gShininess = clamp(2.0 / r4 - 2.0, 2.0, 512.0);
    gSpecK = 0.04 * (gShininess + 2.0) / 8.0;
  }
  vec3 v = normalize(uCamPos - vWorld);
  vec3 diff = uAmbient;
  vec3 spec = vec3(0.0);
  for (int i = 0; i < 6; ++i) {
    if (i >= uNumSpots) break;
    vec3 Lv = uSpotPos[i] - vWorld;
    float d = length(Lv);
    if (d >= uSpotRange[i]) continue;  // falloff is exactly zero beyond the light's range
    vec3 L = Lv / max(d, 1e-4);
    float cone = smoothstep(uSpotCos[i], uSpotPenCos[i], dot(-L, uSpotDir[i]));
    if (cone <= 0.0) continue;
    float sh = uSpotShadow[i] >= 0 ? shadowFactor(i) : 1.0;
    addLight(n, v, L, uSpotColor[i] * distanceFalloff(d, uSpotRange[i]) * cone * sh, diff, spec);
  }
  if (uHasPoint == 1) {
    vec3 Lv = uPointPos - vWorld;
    float d = length(Lv);
    if (d < uPointRange)
      addLight(n, v, Lv / max(d, 1e-4), uPointColor * distanceFalloff(d, uPointRange), diff, spec);
  }
  vec3 c = albedo.rgb * diff / PI + spec * gSpecK / PI;
  fragColor = vec4(applyFog(linearToSrgb(c), uFogDensity, vFogDepth), albedo.a * uOpacity);
}
)";

// ----------------------------------------------------------------------------- unlit
// Light orbs, spotlight housings, exit crystal, minimap, fades. uColor is sRGB (MeshBasicMaterial).
inline const char* kUnlitVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 2) in vec2 aUv;
uniform mat4 uMVP;
uniform mat4 uModelView;
out vec2 vUv;
out float vFogDepth;
void main() {
  vUv = aUv;
  vFogDepth = -(uModelView * vec4(aPos, 1.0)).z;
  gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

inline const char* kUnlitFS = "#version 300 es\nprecision highp float;\n" IT_GLSL_COMMON R"(
uniform vec4 uColor;
uniform float uFogDensity;
in vec2 vUv;
in float vFogDepth;
out vec4 fragColor;
void main() {
  fragColor = vec4(applyFog(uColor.rgb, uFogDensity, vFogDepth), uColor.a);
}
)";

// ----------------------------------------------------------------------------- rotated present
// Draws the logical-size frame onto the window for a monitor rotated by uRotation (degrees CW).
// aPos is the [0,1]^2 screen quad; window GL coords (fx, fy) map to texture coords as derived for a
// panel turned clockwise: 90 -> (fy, 1-fx), 180 -> (1-fx, 1-fy), 270 -> (1-fy, fx).
inline const char* kPresentVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
uniform int uRotation;
uniform vec2 uScale;    // < 1 squeezes the frame towards the centre ("flip" maze transition)
out vec2 vUv;
void main() {
  vec2 f = aPos.xy;
  if (uRotation == 90) vUv = vec2(f.y, 1.0 - f.x);
  else if (uRotation == 180) vUv = vec2(1.0 - f.x, 1.0 - f.y);
  else if (uRotation == 270) vUv = vec2(1.0 - f.y, f.x);
  else vUv = f;
  gl_Position = vec4((f * 2.0 - 1.0) * uScale, 0.0, 1.0);
}
)";

inline const char* kPresentFS = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
in vec2 vUv;
out vec4 fragColor;
void main() { fragColor = vec4(texture(uTex, vUv).rgb, 1.0); }
)";

// ----------------------------------------------------------------------------- maze depth pre-pass
// Same position expression as kMazeVS (both invariant) so the shading pass can test LEQUAL against
// it exactly. Maze walls are one mesh in generation order; without this, walls hidden behind nearer
// walls get fully lit first (view-dependent overdraw that pushed some frames past 33 ms on the Pi).
inline const char* kMazeDepthVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
invariant gl_Position;
void main() {
  vec4 w = uModel * vec4(aPos, 1.0);
  vec4 mv = uView * w;
  gl_Position = uProj * mv;
}
)";

// ----------------------------------------------------------------------------- depth only
inline const char* kDepthVS = R"(#version 300 es
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)";

inline const char* kDepthFS = R"(#version 300 es
precision mediump float;
void main() {}
)";

#undef IT_GLSL_COMMON

}  // namespace it::shaders
