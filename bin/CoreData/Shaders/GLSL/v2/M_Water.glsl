#define URHO3D_PIXEL_NEED_TEXCOORD
#define URHO3D_PIXEL_NEED_EYE_VECTOR
#define URHO3D_SURFACE_NEED_BACKGROUND_COLOR
#ifdef URHO3D_HAS_READABLE_DEPTH
    #define URHO3D_SURFACE_NEED_BACKGROUND_DEPTH
#endif
#define URHO3D_SURFACE_NEED_NORMAL
#define URHO3D_CUSTOM_MATERIAL_UNIFORMS

#ifndef NORMALMAP
    #define NORMALMAP
#endif

#ifndef ENVCUBEMAP
    #define ENVCUBEMAP
#endif

#include "_Config.glsl"
#include "_Uniforms.glsl"

UNIFORM_BUFFER_BEGIN(4, Material)
    DEFAULT_MATERIAL_UNIFORMS
    UNIFORM(half2 cNoiseSpeed)
    UNIFORM(half cNoiseStrength)
UNIFORM_BUFFER_END(4, Material)

#include "_Material.glsl"

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    FillVertexOutputs(vertexTransform);
    vTexCoord += cElapsedTime * cNoiseSpeed;
}
#endif

#ifdef URHO3D_PIXEL_SHADER
void main()
{
    SurfaceData surfaceData;

    FillSurfaceCommon(surfaceData);
    FillSurfaceAmbient(surfaceData, sEmissiveMap, vTexCoord2);
    FillSurfaceNormal(surfaceData, vNormal, sNormalMap, vTexCoord, vTangent, vBitangentXY);
    FillSurfaceMetallicRoughnessOcclusion(surfaceData);
    FillSurfaceReflectionColor(surfaceData);

    // Apply noise to screen position used for background sampling
    surfaceData.screenPos += surfaceData.normalInTangentSpace.xy * cNoiseStrength;

#ifndef URHO3D_ADDITIVE_BLENDING
    FillSurfaceBackground(surfaceData);
#endif

    // Water doesn't accept diffuse lighting, set albedo to zero
    surfaceData.albedo = vec4(0.0);
    surfaceData.specular = GammaToLightSpace(cMatSpecColor.rgb) * (1.0 - surfaceData.oneMinusReflectivity);
#ifdef URHO3D_SURFACE_NEED_AMBIENT
    surfaceData.emission = GammaToLightSpace(cMatEmissiveColor);
#endif

#ifdef URHO3D_AMBIENT_PASS
    half NoV = clamp(dot(surfaceData.normal, surfaceData.eyeVec), 0.0, 1.0);
    #ifdef URHO3D_PHYSICAL_MATERIAL
        half4 reflectedColor = Indirect_PBRWater(surfaceData.reflectionColor[0].rgb, surfaceData.specular, NoV);
    #else
        half4 reflectedColor = Indirect_SimpleWater(surfaceData.reflectionColor[0].rgb, cMatEnvMapColor, NoV);
    #endif
#else
    half4 reflectedColor = vec4(0.0);
#endif

#ifdef URHO3D_LIGHT_PASS
    reflectedColor.rgb += CalculateDirectLighting(surfaceData);
#endif

    // Water uses background color and so doesn't need alpha blending
    const fixed finalAlpha = 1.0;

#ifdef URHO3D_ADDITIVE_BLENDING
    gl_FragColor = GetFragmentColorAlpha(reflectedColor.rgb, finalAlpha, surfaceData.fogFactor);
#else
    half3 waterDepthColor = GetFragmentColor(GammaToLightSpace(cMatDiffColor.rgb), finalAlpha, surfaceData.fogFactor);

    #ifdef URHO3D_HAS_READABLE_DEPTH
        half depthDelta = surfaceData.backgroundDepth - vWorldDepth - cFadeOffsetScale.x;
        half opacity = clamp(depthDelta * cFadeOffsetScale.y, 0.0, 1.0);
        surfaceData.backgroundColor = mix(surfaceData.backgroundColor, waterDepthColor, opacity);
    #else
        surfaceData.backgroundColor = mix(surfaceData.backgroundColor, waterDepthColor, cMatDiffColor.a);
    #endif

    half3 waterColor = GetFragmentColorAlpha(reflectedColor.rgb, 1.0, surfaceData.fogFactor).rgb;
    gl_FragColor = vec4(mix(surfaceData.backgroundColor, waterColor, reflectedColor.a), 1.0);
#endif

}
#endif
