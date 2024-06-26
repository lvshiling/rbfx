#ifndef UNLIT
    #define UNLIT
#endif
#define URHO3D_PIXEL_NEED_TEXCOORD
#define URHO3D_CUSTOM_MATERIAL_UNIFORMS

#include "_Config.glsl"
#include "_Uniforms.glsl"
#include "_DefaultSamplers.glsl"

UNIFORM_BUFFER_BEGIN(4, Material)
    DEFAULT_MATERIAL_UNIFORMS
    UNIFORM(half4 cShadowColor)
    UNIFORM(half4 cStrokeColor)
    UNIFORM(half2 cShadowOffset)
    UNIFORM(half cStrokeThickness)
UNIFORM_BUFFER_END(4, Material)

#include "_Material.glsl"

#ifdef URHO3D_VERTEX_SHADER
void main()
{
    VertexTransform vertexTransform = GetVertexTransform();
    Vertex_SetAll(vertexTransform, cNormalScale, cUOffset, cVOffset, cLMOffset);
}
#endif

#ifdef URHO3D_PIXEL_SHADER

/// Whether to supersample SDF font.
#if defined(SIGNED_DISTANCE_FIELD)
    #define URHO3D_FONT_SUPERSAMPLE
#endif

/// Return width of SDF font border.
half GetBorderWidth(half distance)
{
    return fwidth(distance);
}

/// Return letter opacity.
half GetOpacity(half distance, half width)
{
    return smoothstep(0.5 - width, 0.5 + width, distance);
}

void main()
{
    half4 finalColor = vColor;

#ifdef SIGNED_DISTANCE_FIELD
    half distance = texture(sAlbedo, vTexCoord).a;
    half width = GetBorderWidth(distance);

    half mainWeight = GetOpacity(distance, width);
    #ifdef URHO3D_FONT_SUPERSAMPLE
        const float inv2v2 = 0.354; // 1 / (2 * sqrt(2))
        vec2 deltaUV = inv2v2 * fwidth(vTexCoord);
        vec4 square = vec4(vTexCoord - deltaUV, vTexCoord + deltaUV);
        mainWeight += GetOpacity(texture(sAlbedo, square.xy).a, width);
        mainWeight += GetOpacity(texture(sAlbedo, square.zw).a, width);
        mainWeight += GetOpacity(texture(sAlbedo, square.xw).a, width);
        mainWeight += GetOpacity(texture(sAlbedo, square.zy).a, width);
        // Divide by 4 instead of 5 to make font sharper
        mainWeight = min(0.25 * mainWeight, 1.0);
    #endif

    #ifdef TEXT_EFFECT_STROKE
        half strokeWeight = smoothstep(0.5, 0.5 + max(0.001, cStrokeThickness), distance);
        finalColor.rgb = mix(cStrokeColor.rgb, finalColor.rgb, strokeWeight);
    #endif

    #ifdef TEXT_EFFECT_SHADOW
        half shadowDistance = texture(sAlbedo, vTexCoord + cShadowOffset).a;
        half shadowWeight = step(0.5, shadowDistance);
        finalColor.rgb = mix(cShadowColor.rgb, finalColor.rgb, shadowWeight);
    #endif

    finalColor.a *= mainWeight;
#else
    #ifdef ALPHAMAP
        finalColor.a *= texture(sAlbedo, vTexCoord).r;
    #else
        finalColor *= texture(sAlbedo, vTexCoord);
    #endif
#endif

    gl_FragColor = GammaToLightSpaceAlpha(finalColor);
}
#endif
