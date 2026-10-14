#pragma once
#include "common.h"
#include <rlgl.h>

// Renderer: real point lighting + fog for the 3D pass, then a post-process
// blit (vignette / grain / chromatic aberration / instability warp / scare tint).
// The world renders into a supersampled RenderTexture; UI stays at screen res.
namespace FX {

static const int MAX_LIGHTS = 10;

struct Light {
    Vector3 pos{};
    Color color{255, 255, 255, 255};
    float radius = 6.f;
    float intensity = 1.f;
    std::string room = "office";   // "waiting" lights die when the anomaly hits
    bool flicker = false;          // dims with the global flicker level
};

// ---------- GLSL ----------

static const char* LIGHT_VS = R"GLSL(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
uniform mat4 mvp;
void main() {
    // rlgl applies the matrix stack on the CPU: vertexPosition/vertexNormal
    // arrive WORLD-space. matModel/matNormal are re-uploaded per flush from
    // whatever transform is on the stack AT FLUSH TIME - an overflow flush
    // mid-DrawSphereEx (its rlPushMatrix is still active) would double-
    // transform every accumulated vertex: positions stay correct via mvp but
    // fragPosition/fragNormal scramble -> light pools sweep + room dims.
    fragPosition = vertexPosition;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = (dot(vertexNormal, vertexNormal) > 1e-6) ? normalize(vertexNormal) : vec3(0.0, 1.0, 0.0);
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)GLSL";

static const char* LIGHT_FS = R"GLSL(#version 330
#define MAXL 10
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec4 ambient;
uniform int lightCount;
uniform vec3 lightPos[MAXL];
uniform vec4 lightCol[MAXL];   // rgb premultiplied by intensity, a = radius
uniform vec3 fogColor;
uniform float fogDensity;
uniform float noiseAmt;
uniform int dbgMode;   // 0 normal, 1=show normals, 2=show acc, 3=show light0 color
out vec4 finalColor;
float hash(vec3 p) {
    p = fract(p*0.3183099 + vec3(0.11, 0.17, 0.13));
    p *= 17.0;
    return fract(p.x*p.y*p.z*(p.x + p.y + p.z));
}
void main() {
    vec4 texel = texture(texture0, fragTexCoord);
    vec3 base = texel.rgb * colDiffuse.rgb * fragColor.rgb;
    // world-space grain so flat surfaces aren't perfectly flat; coarse cells +
    // distance fade keep it from aliasing into moire bands on big surfaces
    float dist0 = length(viewPos - fragPosition);
    float g = hash(floor(fragPosition*9.0));
    base *= 1.0 + (g - 0.5) * noiseAmt * exp(-dist0*0.10);
    vec3 n = normalize(fragNormal);
    if (!gl_FrontFacing) n = -n;
    vec3 acc = ambient.rgb;
    for (int i = 0; i < MAXL; i++) {
        if (i >= lightCount) break;
        vec3 L = lightPos[i] - fragPosition;
        float d = length(L);
        float att = clamp(1.0 - d/lightCol[i].a, 0.0, 1.0);
        att *= att;
        float ndl = max(dot(n, L/max(d, 0.001)), 0.0);
        acc += lightCol[i].rgb * (ndl * att);
    }
    vec3 col = base * acc;
    float fog = 1.0 - exp(-fogDensity*fogDensity*dist0*dist0);
    col = mix(col, fogColor, clamp(fog, 0.0, 1.0));
    if (dbgMode == 1) col = normalize(fragNormal) * 0.5 + 0.5;
    else if (dbgMode == 2) col = acc;
    else if (dbgMode == 3) col = vec3(lightPos[0].x * 0.1, lightCol[0].r, lightCol[0].a * 0.1);
    else if (dbgMode == 4) col = vec3(float(lightCount) * 0.1);
    else if (dbgMode == 5) { finalColor = vec4(1.0, 0.0, 0.0, 1.0); return; }
    else if (dbgMode >= 6) {
        vec3 Ld = lightPos[1] - fragPosition;
        float dd = length(Ld);
        float attd = clamp(1.0 - dd/lightCol[1].a, 0.0, 1.0); attd *= attd;
        float ndld = max(dot(n, Ld/max(dd, 0.001)), 0.0);
        vec3 dv = dbgMode == 6 ? vec3(attd) : dbgMode == 7 ? vec3(ndld) :
                  dbgMode == 8 ? vec3(attd*ndld) :
                  dbgMode == 9 ? vec3(clamp(dd * 0.1, 0.0, 1.0)) :   // distance to L1
                  dbgMode == 10 ? vec3(clamp(fragPosition * 0.1 + 0.5, 0.0, 1.0)) :
                  dbgMode == 11 ? vec3(lightPos[1].x * 0.1 + 0.5, lightPos[1].y * 0.1, lightPos[1].z * 0.1 + 0.5) :
                  dbgMode == 12 ? vec3(clamp(lightCol[1].rgb * 0.4, 0.0, 1.0)) :
                  dbgMode == 13 ? vec3(lightCol[1].a * 0.1) :         // radius viz
                  vec3(ambient.rgb);                                 // 14: ambient check
        finalColor = vec4(dv, 1.0); return;
    }
    finalColor = vec4(col, texel.a * colDiffuse.a * fragColor.a);
}
)GLSL";

static const char* POST_VS = R"GLSL(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
out vec2 fragTexCoord;
out vec4 fragColor;
uniform mat4 mvp;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp*vec4(vertexPosition, 1.0);
}
)GLSL";

static const char* POST_FS = R"GLSL(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec2 res;
uniform float time;
uniform float instab;    // 0..1
uniform float scare;     // 0..1 jumpscare
uniform float blackout;  // 0..1 light failure
uniform float grade;     // 0..1 colour-grade strength
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7)))*43758.5453); }
// compact FXAA (Lottes reduced): luma-edge directed blur, 9 taps on texture0
vec3 Fxaa(vec2 uv, vec2 px) {
    const vec3 luma = vec3(0.299, 0.587, 0.114);
    vec3 nw = texture(texture0, uv + vec2(-1.0, -1.0)*px).rgb;
    vec3 ne = texture(texture0, uv + vec2( 1.0, -1.0)*px).rgb;
    vec3 sw = texture(texture0, uv + vec2(-1.0,  1.0)*px).rgb;
    vec3 se = texture(texture0, uv + vec2( 1.0,  1.0)*px).rgb;
    vec3 m  = texture(texture0, uv).rgb;
    float lNW = dot(nw, luma), lNE = dot(ne, luma);
    float lSW = dot(sw, luma), lSE = dot(se, luma);
    float lM = dot(m, luma);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float dirReduce = max((lNW + lNE + lSW + lSE)*0.03125, 0.0078125);
    float rcpMin = 1.0/(min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir*rcpMin, -8.0, 8.0)*px;
    vec3 a = 0.5*(texture(texture0, uv + dir*(1.0/3.0 - 0.5)).rgb +
                  texture(texture0, uv + dir*(2.0/3.0 - 0.5)).rgb);
    vec3 b = a*0.5 + 0.25*(texture(texture0, uv + dir*(-0.5)).rgb +
                           texture(texture0, uv + dir*( 0.5)).rgb);
    float lB = dot(b, luma);
    return (lB < lMin || lB > lMax) ? a : b;
}
void main() {
    vec2 uv = fragTexCoord;
    // breathing warp that grows with instability / scare
    float w = instab*0.0035 + scare*0.02;
    uv.x += sin(uv.y*38.0 + time*2.7)*w;
    uv.y += sin(uv.x*31.0 - time*2.1)*w*0.6;
    vec4 t = texture(texture0, uv);
    vec3 col = Fxaa(uv, 1.0/res) * colDiffuse.rgb * fragColor.rgb;
    // chromatic aberration
    float ca = 0.0010 + instab*0.0025 + scare*0.008;
    vec2 dir = uv - 0.5;
    col.r = texture(texture0, uv + dir*ca).r;
    col.b = texture(texture0, uv - dir*ca).b;
    // subtle grade: filmic S-curve on the legal range, cool shadows + warm
    // highlights by luma - both multiplicative so blacks stay black
    vec3 cc = clamp(col, 0.0, 1.0);
    col = mix(col, cc*cc*(3.0 - 2.0*cc), grade*0.9);
    float lu = clamp(dot(col, vec3(0.299, 0.587, 0.114)), 0.0, 1.0);
    col *= mix(vec3(1.0), mix(vec3(0.93, 0.97, 1.08), vec3(1.05, 1.00, 0.93),
               smoothstep(0.10, 0.70, lu)), grade);
    // film grain
    float g = hash(uv*res*0.5 + vec2(fract(time*13.7)*91.0));
    col += (g - 0.5) * (0.045 + instab*0.085 + scare*0.10);
    // vignette
    float v = smoothstep(1.05, 0.30, length(dir));
    col *= mix(0.55, 1.0, v);
    // blackout collapse
    col *= (1.0 - blackout*0.82);
    // scare: crush toward red-black
    col = mix(col, vec3(dot(col, vec3(0.299, 0.587, 0.114)))*vec3(0.55, 0.06, 0.05), scare*0.6);
    finalColor = vec4(col, t.a * colDiffuse.a * fragColor.a);
}
)GLSL";

// ---------- host ----------

class Renderer {
public:
    RenderTexture2D scene{};
    Shader light{}, post{};
    float rtScale = 1.25f;
    int rtW = 0, rtH = 0;
    bool ok = false;

    // per-frame params
    float time = 0, instab = 0, scare = 0, blackout = 0;
    float grade = 0.22f;            // filmic grade strength (EI_GRADE override)
    float flicker = 1.f;              // multiplies flicker-flagged lights
    float fogDensity = 0.065f;
    Color fogColor{9, 10, 14, 255};
    Color ambient{78, 72, 82, 255};
    float noiseAmt = 0.12f;

    std::vector<Light> lights;
    bool waitingDark = false;         // office.waitingLightsOut mirror

    bool noLight = false, noPost = false;   // EI_NOLIGHT / EI_RAW debug toggles

    bool Init(float scale = 1.25f) {
        rtScale = scale;
        light = LoadShaderFromMemory(LIGHT_VS, LIGHT_FS);
        post = LoadShaderFromMemory(POST_VS, POST_FS);
        if (std::getenv("EI_NOLIGHT")) { light.id = 0; }
        if (std::getenv("EI_RAW")) { post.id = 0; }
        noLight = light.id == 0; noPost = post.id == 0;
        // ok means "render-to-texture pipeline usable"; shader failure degrades gracefully
        ok = true;
        locLightCount = GetShaderLocation(light, "lightCount");
        locAmbient = GetShaderLocation(light, "ambient");
        locViewPos = GetShaderLocation(light, "viewPos");
        locFogColor = GetShaderLocation(light, "fogColor");
        locFogDensity = GetShaderLocation(light, "fogDensity");
        locNoiseAmt = GetShaderLocation(light, "noiseAmt");
        locDbg = GetShaderLocation(light, "dbgMode");
        for (int i = 0; i < MAX_LIGHTS; i++) {
            char n[32];
            snprintf(n, 32, "lightPos[%d]", i); locLightPos[i] = GetShaderLocation(light, n);
            snprintf(n, 32, "lightCol[%d]", i); locLightCol[i] = GetShaderLocation(light, n);
        }
        if (std::getenv("EI_DIAG")) {
            TraceLog(LOG_WARNING, "[locs] cnt=%d amb=%d view=%d fog=%d fogd=%d nse=%d dbg=%d pos0=%d col0=%d",
                     locLightCount, locAmbient, locViewPos, locFogColor, locFogDensity,
                     locNoiseAmt, locDbg, locLightPos[0], locLightCol[0]);
            for (int i = 1; i < 10; i++)
                TraceLog(LOG_WARNING, "[locs] pos%d=%d col%d=%d", i, locLightPos[i], i, locLightCol[i]);
        }
        locRes = GetShaderLocation(post, "res");
        locTime = GetShaderLocation(post, "time");
        locInstab = GetShaderLocation(post, "instab");
        locScare = GetShaderLocation(post, "scare");
        locBlackout = GetShaderLocation(post, "blackout");
        locGrade = GetShaderLocation(post, "grade");
        if (const char* e = std::getenv("EI_GRADE")) grade = (float)std::atof(e);
        EnsureSize();
        return true;
    }

    void EnsureSize() {
        int w = (int)(GetScreenWidth() * rtScale), h = (int)(GetScreenHeight() * rtScale);
        if (w == rtW && h == rtH) return;
        if (scene.id != 0) UnloadRenderTexture(scene);
        rtW = w; rtH = h;
        scene = LoadRenderTexture(w, h);
        SetTextureFilter(scene.texture, TEXTURE_FILTER_BILINEAR);
        if (scene.id == 0) ok = false;
    }

    // Fill shader light arrays from `lights`, honoring waitingDark + flicker.
    void PushLights() {
        int n = 0;
        float pos[MAX_LIGHTS * 3]{};
        float col[MAX_LIGHTS * 4]{};
        for (auto& L : lights) {
            if (n >= MAX_LIGHTS) break;
            if (waitingDark && L.room == "waiting") continue;
            float k = L.intensity * (L.flicker ? flicker : 1.f);
            if (k <= 0.02f) continue;
            pos[n * 3 + 0] = L.pos.x; pos[n * 3 + 1] = L.pos.y; pos[n * 3 + 2] = L.pos.z;
            col[n * 4 + 0] = L.color.r / 255.f * k;
            col[n * 4 + 1] = L.color.g / 255.f * k;
            col[n * 4 + 2] = L.color.b / 255.f * k;
            col[n * 4 + 3] = L.radius;
            n++;
        }
        SetShaderValue(light, locLightCount, &n, SHADER_UNIFORM_INT);
        if (n > 0) {
            SetShaderValueV(light, locLightPos[0], pos, SHADER_UNIFORM_VEC3, n);
            SetShaderValueV(light, locLightCol[0], col, SHADER_UNIFORM_VEC4, n);
        }
        if (std::getenv("EI_DIAG")) {
            static float acc = 0; acc += GetFrameTime();
            if (acc >= 5.f) {
                acc = 0;
                TraceLog(LOG_WARNING, "[lights] n=%d flick=%.2f wd=%d", n, flicker, (int)waitingDark);
                for (int i = 0; i < n; i++)
                    TraceLog(LOG_WARNING, "  L%d pos=(%.1f %.1f %.1f) rgb=(%.2f %.2f %.2f) r=%.1f",
                             i, pos[i*3], pos[i*3+1], pos[i*3+2],
                             col[i*4], col[i*4+1], col[i*4+2], col[i*4+3]);
            }
        }
        {
            static int dbg = -1;
            if (dbg < 0) { const char* e = std::getenv("EI_DBG"); dbg = e ? std::atoi(e) : 0; }
            SetShaderValue(light, locDbg, &dbg, SHADER_UNIFORM_INT);
        }
        float amb[4] = {ambient.r / 255.f, ambient.g / 255.f, ambient.b / 255.f, 1.f};
        SetShaderValue(light, locAmbient, amb, SHADER_UNIFORM_VEC4);
        float fog[3] = {fogColor.r / 255.f, fogColor.g / 255.f, fogColor.b / 255.f};
        SetShaderValue(light, locFogColor, fog, SHADER_UNIFORM_VEC3);
        SetShaderValue(light, locFogDensity, &fogDensity, SHADER_UNIFORM_FLOAT);
        SetShaderValue(light, locNoiseAmt, &noiseAmt, SHADER_UNIFORM_FLOAT);
    }

    void SetViewPos(Vector3 p) {
        float v[3] = {p.x, p.y, p.z};
        SetShaderValue(light, locViewPos, v, SHADER_UNIFORM_VEC3);
    }

    // call between BeginTextureMode and BeginMode3D
    void BeginWorld() {
        if (!noLight) BeginShaderMode(light);
    }
    // call before EndMode3D for unlit/emissive geometry (shadow figures)
    void LitOff() { if (!noLight) EndShaderMode(); }

    // full-screen post blit, call after BeginDrawing
    void Blit() {
        if (!noPost) {
            SetShaderValue(post, locTime, &time, SHADER_UNIFORM_FLOAT);
            SetShaderValue(post, locInstab, &instab, SHADER_UNIFORM_FLOAT);
            SetShaderValue(post, locScare, &scare, SHADER_UNIFORM_FLOAT);
            SetShaderValue(post, locBlackout, &blackout, SHADER_UNIFORM_FLOAT);
            SetShaderValue(post, locGrade, &grade, SHADER_UNIFORM_FLOAT);
            float r[2] = {(float)rtW, (float)rtH};
            SetShaderValue(post, locRes, r, SHADER_UNIFORM_VEC2);
            BeginShaderMode(post);
        }
        DrawTexturePro(scene.texture,
                       Rectangle{0, 0, (float)rtW, -(float)rtH},
                       Rectangle{0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()},
                       Vector2{0, 0}, 0.f, WHITE);
        if (!noPost) EndShaderMode();
    }

    void Shutdown() {
        if (scene.id) UnloadRenderTexture(scene);
        if (light.id) UnloadShader(light);
        if (post.id) UnloadShader(post);
    }

private:
    int locLightCount = -1, locAmbient = -1, locViewPos = -1, locFogColor = -1,
        locFogDensity = -1, locNoiseAmt = -1, locDbg = -1;
    int locLightPos[MAX_LIGHTS]{}, locLightCol[MAX_LIGHTS]{};
    int locRes = -1, locTime = -1, locInstab = -1, locScare = -1, locBlackout = -1,
        locGrade = -1;
};

} // namespace FX
