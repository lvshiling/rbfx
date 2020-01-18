//
// Copyright (c) 2008-2019 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

// Embree includes must be first
#include <embree3/rtcore.h>
#include <embree3/rtcore_ray.h>
#define _SSIZE_T_DEFINED

#include "../Graphics/Model.h"
#include "../Graphics/ModelView.h"
#include "../Graphics/StaticModel.h"
#include "../Graphics/Terrain.h"
#include "../Graphics/TerrainPatch.h"
#include "../Glow/RaytracerScene.h"
#include "../Glow/Helpers.h"
#include "../IO/Log.h"

#include <future>

namespace Urho3D
{

namespace
{

/// Parameters for raytracing geometry creation from geometry view.
struct RaytracingFromGeometryViewParams
{
    /// Transform from geometry to world space.
    Matrix3x4 worldTransform_;
    /// Rotation from geometry to world space.
    Quaternion worldRotation_;
    /// Unpacked geometry data.
    const GeometryLODView* geometry_{};
    /// Lightmap UV scale.
    Vector2 lightmapUVScale_;
    /// Lightmap UV offset.
    Vector2 lightmapUVOffset_;
    /// UV channel used for lightmap UV.
    unsigned lightmapUVChannel_;
    /// Whether to store main texture UV.
    bool storeUV_{};
    /// Transform for U coordinate.
    Vector4 uOffset_;
    /// Transform for V coordinate.
    Vector4 vOffset_;
};

/// Parameters for raytracing geometry creation from terrain.
struct RaytracingFromTerrainParams
{
    /// Terrain.
    const Terrain* terrain_{};
    /// Lightmap UV scale.
    Vector2 lightmapUVScale_;
    /// Lightmap UV offset.
    Vector2 lightmapUVOffset_;
    /// UV channel used for lightmap UV.
    unsigned lightmapUVChannel_;
    /// Transform for U coordinate.
    Vector4 uOffset_;
    /// Transform for V coordinate.
    Vector4 vOffset_;
};

/// Pair of model and corresponding model view.
struct ModelModelViewPair
{
    Model* model_{};
    SharedPtr<ModelView> parsedModel_;
};

/// Parse model data.
ModelModelViewPair ParseModelForRaytracer(Model* model)
{
    auto modelView = MakeShared<ModelView>(model->GetContext());
    modelView->ImportModel(model);

    return { model, modelView };
}

/// Create Embree geometry from geometry view.
RTCGeometry CreateEmbreeGeometryForGeometryView(RTCDevice embreeDevice,
    const RaytracingFromGeometryViewParams& params, unsigned mask)
{
    const unsigned numVertices = params.geometry_->vertices_.size();
    const unsigned numIndices = params.geometry_->indices_.size();
    const ea::vector<ModelVertex>& sourceVertices = params.geometry_->vertices_;
    const unsigned numAttributes = params.storeUV_ ? 3 : 2;

    RTCGeometry embreeGeometry = rtcNewGeometry(embreeDevice, RTC_GEOMETRY_TYPE_TRIANGLE);
    rtcSetGeometryVertexAttributeCount(embreeGeometry, numAttributes);

    float* vertices = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX,
        0, RTC_FORMAT_FLOAT3, sizeof(Vector3), numVertices));

    float* lightmapUVs = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
        RaytracerScene::LightmapUVAttribute, RTC_FORMAT_FLOAT2, sizeof(Vector2), numVertices));

    float* smoothNormals = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
        RaytracerScene::NormalAttribute, RTC_FORMAT_FLOAT3, sizeof(Vector3), numVertices));

    float* uvs = nullptr;
    if (params.storeUV_)
    {
        uvs = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
            RaytracerScene::UVAttribute, RTC_FORMAT_FLOAT2, sizeof(Vector2), numVertices));
    }

    for (unsigned i = 0; i < numVertices; ++i)
    {
        const Vector3 localPosition = static_cast<Vector3>(sourceVertices[i].position_);
        const Vector3 localNormal = static_cast<Vector3>(sourceVertices[i].normal_);
        const Vector2 lightmapUV = static_cast<Vector2>(sourceVertices[i].uv_[params.lightmapUVChannel_]);
        const Vector2 lightmapUVScaled = lightmapUV * params.lightmapUVScale_ + params.lightmapUVOffset_;
        const Vector3 worldPosition = params.worldTransform_ * localPosition;
        const Vector3 worldNormal = params.worldRotation_ * localNormal;

        vertices[i * 3 + 0] = worldPosition.x_;
        vertices[i * 3 + 1] = worldPosition.y_;
        vertices[i * 3 + 2] = worldPosition.z_;
        lightmapUVs[i * 2 + 0] = lightmapUVScaled.x_;
        lightmapUVs[i * 2 + 1] = lightmapUVScaled.y_;
        smoothNormals[i * 3 + 0] = worldNormal.x_;
        smoothNormals[i * 3 + 1] = worldNormal.y_;
        smoothNormals[i * 3 + 2] = worldNormal.z_;

        if (uvs)
        {
            const Vector2 uv = static_cast<Vector2>(sourceVertices[i].uv_[0]);
            uvs[i * 2 + 0] = uv.DotProduct(static_cast<Vector2>(params.uOffset_)) + params.uOffset_.w_;
            uvs[i * 2 + 1] = uv.DotProduct(static_cast<Vector2>(params.vOffset_)) + params.vOffset_.w_;
        }
    }

    unsigned* indices = reinterpret_cast<unsigned*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_INDEX,
        0, RTC_FORMAT_UINT3, sizeof(unsigned) * 3, numIndices / 3));

    for (unsigned i = 0; i < numIndices; ++i)
        indices[i] = params.geometry_->indices_[i];

    rtcSetGeometryMask(embreeGeometry, mask);
    rtcCommitGeometry(embreeGeometry);
    return embreeGeometry;
}

/// Create Embree geometry from geometry view.
RTCGeometry CreateEmbreeGeometryForTerrain(RTCDevice embreeDevice,
    const RaytracingFromTerrainParams& params, unsigned mask)
{
    const Terrain* terrain = params.terrain_;
    const IntVector2 terrainSize = terrain->GetNumVertices();
    const IntVector2 numPatches = terrain->GetNumPatches();
    const int patchSize = terrain->GetPatchSize();

    const unsigned numVertices = static_cast<unsigned>(terrainSize.x_ * terrainSize.y_);
    const unsigned numQuads = static_cast<unsigned>(numPatches.x_ * numPatches.y_ * patchSize * patchSize);

    RTCGeometry embreeGeometry = rtcNewGeometry(embreeDevice, RTC_GEOMETRY_TYPE_TRIANGLE);
    rtcSetGeometryVertexAttributeCount(embreeGeometry, 3);

    float* vertices = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX,
        0, RTC_FORMAT_FLOAT3, sizeof(Vector3), numVertices));

    float* lightmapUVs = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
        RaytracerScene::LightmapUVAttribute, RTC_FORMAT_FLOAT2, sizeof(Vector2), numVertices));

    float* smoothNormals = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
        RaytracerScene::NormalAttribute, RTC_FORMAT_FLOAT3, sizeof(Vector3), numVertices));

    float* uvs = reinterpret_cast<float*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_VERTEX_ATTRIBUTE,
        RaytracerScene::UVAttribute, RTC_FORMAT_FLOAT2, sizeof(Vector2), numVertices));

    for (unsigned i = 0; i < numVertices; ++i)
    {
        const int x = static_cast<int>(i) % terrainSize.x_;
        const int y = terrainSize.y_ - static_cast<int>(i) / terrainSize.x_ - 1;
        const Vector3 worldPosition = terrain->HeightMapToWorld({ x, y });
        const Vector3 worldNormal = terrain->GetNormal(worldPosition);
        const Vector2 uv = terrain->HeightMapToUV({ x, y });
        const Vector2 lightmapUVScaled = uv * params.lightmapUVScale_ + params.lightmapUVOffset_;

        vertices[i * 3 + 0] = worldPosition.x_;
        vertices[i * 3 + 1] = worldPosition.y_;
        vertices[i * 3 + 2] = worldPosition.z_;
        lightmapUVs[i * 2 + 0] = lightmapUVScaled.x_;
        lightmapUVs[i * 2 + 1] = lightmapUVScaled.y_;
        smoothNormals[i * 3 + 0] = worldNormal.x_;
        smoothNormals[i * 3 + 1] = worldNormal.y_;
        smoothNormals[i * 3 + 2] = worldNormal.z_;
        uvs[i * 2 + 0] = uv.DotProduct(static_cast<Vector2>(params.uOffset_)) + params.uOffset_.w_;
        uvs[i * 2 + 1] = uv.DotProduct(static_cast<Vector2>(params.vOffset_)) + params.vOffset_.w_;
    }

    unsigned* indices = reinterpret_cast<unsigned*>(rtcSetNewGeometryBuffer(embreeGeometry, RTC_BUFFER_TYPE_INDEX,
        0, RTC_FORMAT_UINT3, sizeof(unsigned) * 3, numQuads * 2 * 3));

    const unsigned maxZ = numPatches.y_ * patchSize;
    const unsigned maxX = numPatches.x_ * patchSize;
    const unsigned row = terrainSize.x_;
    unsigned i = 0;
    for (unsigned z = 0; z < numPatches.y_ * patchSize; ++z)
    {
        for (unsigned x = 0; x < numPatches.x_ * patchSize; ++x)
        {
            indices[i++] = (z + 1) * row + x;
            indices[i++] = z * row + x + 1;
            indices[i++] = z * row + x;
            indices[i++] = (z + 1) * row + x;
            indices[i++] = (z + 1) * row + x + 1;
            indices[i++] = z * row + x + 1;
        }
    }
    assert(i == numQuads * 2 * 3);

    rtcSetGeometryMask(embreeGeometry, mask);
    rtcCommitGeometry(embreeGeometry);
    return embreeGeometry;
}

/// Create raytracer geometries for static model.
ea::vector<RaytracerGeometry> CreateRaytracerGeometriesForStaticModel(RTCDevice embreeDevice,
    ModelView* modelView, StaticModel* staticModel, unsigned objectIndex, unsigned lightmapUVChannel)
{
    auto renderer = staticModel->GetContext()->GetRenderer();

    Node* node = staticModel->GetNode();
    const unsigned lightmapIndex = staticModel->GetLightmapIndex();
    const Vector4 lightmapUVScaleOffset = staticModel->GetLightmapScaleOffset();
    const Vector2 lightmapUVScale{ lightmapUVScaleOffset.x_, lightmapUVScaleOffset.y_ };
    const Vector2 lightmapUVOffset{ lightmapUVScaleOffset.z_, lightmapUVScaleOffset.w_ };

    ea::vector<RaytracerGeometry> result;

    const ea::vector<GeometryView> geometries = modelView->GetGeometries();
    for (unsigned geometryIndex = 0; geometryIndex < geometries.size(); ++geometryIndex)
    {
        const Material* material = staticModel->GetMaterial(geometryIndex);
        if (!material)
            material = renderer->GetDefaultMaterial();

        const GeometryView& geometryView = geometries[geometryIndex];
        for (unsigned lodIndex = 0; lodIndex < geometryView.lods_.size(); ++lodIndex)
        {
            const GeometryLODView& geometryLODView = geometryView.lods_[lodIndex];
            const unsigned mask = lodIndex == 0 ? RaytracerScene::PrimaryLODGeometry : RaytracerScene::SecondaryLODGeometry;

            RaytracerGeometry raytracerGeometry;
            raytracerGeometry.objectIndex_ = objectIndex;
            raytracerGeometry.geometryIndex_ = geometryIndex;
            raytracerGeometry.lodIndex_ = lodIndex;
            raytracerGeometry.numLods_ = geometryView.lods_.size();
            raytracerGeometry.lightmapIndex_ = lightmapIndex;
            raytracerGeometry.raytracerGeometryId_ = M_MAX_UNSIGNED;

            Vector4 uOffset;
            Vector4 vOffset;
            raytracerGeometry.opaque_ = !material || IsMaterialOpaque(material);
            if (!raytracerGeometry.opaque_)
            {
                const Color diffuseColor = GetMaterialDiffuseColor(material);
                raytracerGeometry.diffuseColor_ = diffuseColor.ToVector3();
                raytracerGeometry.alpha_ = diffuseColor.a_;

                Texture* diffuseTexture = GetMaterialDiffuseTexture(material, uOffset, vOffset);
                if (diffuseTexture)
                    raytracerGeometry.diffuseImageName_ = diffuseTexture->GetName();
            }

            RaytracingFromGeometryViewParams params;
            params.worldTransform_ = node->GetWorldTransform();
            params.worldRotation_ = node->GetWorldRotation();
            params.geometry_ = &geometryLODView;
            params.lightmapUVScale_ = lightmapUVScale;
            params.lightmapUVOffset_ = lightmapUVOffset;
            params.lightmapUVChannel_ = lightmapUVChannel;
            if (!raytracerGeometry.diffuseImageName_.empty())
            {
                params.storeUV_ = true;
                params.uOffset_ = uOffset;
                params.vOffset_ = vOffset;
            }

            raytracerGeometry.embreeGeometry_ = CreateEmbreeGeometryForGeometryView(embreeDevice, params, mask);
            result.push_back(raytracerGeometry);
        }
    }
    return result;
}

/// Create raytracer geometry for terrain.
ea::vector<RaytracerGeometry> CreateRaytracerGeometriesForTerrain(RTCDevice embreeDevice,
    Terrain* terrain, unsigned objectIndex, unsigned lightmapUVChannel)
{
    auto renderer = terrain->GetContext()->GetRenderer();

    const Material* material = terrain->GetMaterial();
    if (!material)
        material = renderer->GetDefaultMaterial();

    const unsigned lightmapIndex = terrain->GetLightmapIndex();
    const Vector4 lightmapUVScaleOffset = terrain->GetLightmapScaleOffset();
    const Vector2 lightmapUVScale{ lightmapUVScaleOffset.x_, lightmapUVScaleOffset.y_ };
    const Vector2 lightmapUVOffset{ lightmapUVScaleOffset.z_, lightmapUVScaleOffset.w_ };

    RaytracerGeometry raytracerGeometry;
    raytracerGeometry.objectIndex_ = objectIndex;
    raytracerGeometry.geometryIndex_ = 0;
    raytracerGeometry.lodIndex_ = 0;
    raytracerGeometry.numLods_ = 1;
    raytracerGeometry.lightmapIndex_ = lightmapIndex;
    raytracerGeometry.raytracerGeometryId_ = M_MAX_UNSIGNED;

    Vector4 uOffset;
    Vector4 vOffset;
    raytracerGeometry.opaque_ = !material || IsMaterialOpaque(material);
    if (!raytracerGeometry.opaque_)
    {
        const Color diffuseColor = GetMaterialDiffuseColor(material);
        raytracerGeometry.diffuseColor_ = diffuseColor.ToVector3();
        raytracerGeometry.alpha_ = diffuseColor.a_;

        Texture* diffuseTexture = GetMaterialDiffuseTexture(material, uOffset, vOffset);
        if (diffuseTexture)
            raytracerGeometry.diffuseImageName_ = diffuseTexture->GetName();
    }

    RaytracingFromTerrainParams params;
    params.terrain_ = terrain;
    params.lightmapUVScale_ = lightmapUVScale;
    params.lightmapUVOffset_ = lightmapUVOffset;
    params.lightmapUVChannel_ = lightmapUVChannel;
    if (!raytracerGeometry.diffuseImageName_.empty())
    {
        params.uOffset_ = uOffset;
        params.vOffset_ = vOffset;
    }

    const unsigned mask = RaytracerScene::PrimaryLODGeometry;
    raytracerGeometry.embreeGeometry_ = CreateEmbreeGeometryForTerrain(embreeDevice, params, mask);
    return { raytracerGeometry };
}

}

RaytracerScene::~RaytracerScene()
{
    if (scene_)
        rtcReleaseScene(scene_);
    if (device_)
        rtcReleaseDevice(device_);
}

SharedPtr<RaytracerScene> CreateRaytracingScene(Context* context, const ea::vector<Component*>& geometries, unsigned lightmapUVChannel)
{
    // Queue models for parsing
    ea::hash_set<Model*> modelsToParse;
    for (Component* geometry : geometries)
    {
        if (auto staticModel = dynamic_cast<StaticModel*>(geometry))
            modelsToParse.insert(staticModel->GetModel());
    }

    // Start model parsing
    ea::vector<std::future<ModelModelViewPair>> modelParseTasks;
    for (Model* model : modelsToParse)
        modelParseTasks.push_back(std::async(ParseModelForRaytracer, model));

    // Finish model parsing
    ea::unordered_map<Model*, SharedPtr<ModelView>> parsedModelCache;
    for (auto& task : modelParseTasks)
    {
        const ModelModelViewPair& parsedModel = task.get();
        parsedModelCache.emplace(parsedModel.model_, parsedModel.parsedModel_);
    }

    // Prepare Embree scene
    const RTCDevice device = rtcNewDevice("");
    const RTCScene scene = rtcNewScene(device);
    rtcSetSceneFlags(scene, RTC_SCENE_FLAG_CONTEXT_FILTER_FUNCTION);

    ea::vector<std::future<ea::vector<RaytracerGeometry>>> createRaytracerGeometriesTasks;
    for (unsigned objectIndex = 0; objectIndex < geometries.size(); ++objectIndex)
    {
        Component* geometry = geometries[objectIndex];
        if (auto staticModel = dynamic_cast<StaticModel*>(geometry))
        {
            ModelView* parsedModel = parsedModelCache[staticModel->GetModel()];
            createRaytracerGeometriesTasks.push_back(std::async(CreateRaytracerGeometriesForStaticModel,
                device, parsedModel, staticModel, objectIndex, lightmapUVChannel));
        }
        else if (auto terrain = dynamic_cast<Terrain*>(geometry))
        {
            createRaytracerGeometriesTasks.push_back(std::async(CreateRaytracerGeometriesForTerrain,
                device, terrain, objectIndex, lightmapUVChannel));
        }
    }

    // Collect and attach Embree geometries
    ea::hash_map<ea::string, SharedPtr<Image>> diffuseImages;
    ea::vector<RaytracerGeometry> geometryIndex;
    for (auto& task : createRaytracerGeometriesTasks)
    {
        const ea::vector<RaytracerGeometry> raytracerGeometryArray = task.get();
        for (const RaytracerGeometry& raytracerGeometry : raytracerGeometryArray)
        {
            const unsigned geomID = rtcAttachGeometry(scene, raytracerGeometry.embreeGeometry_);
            rtcReleaseGeometry(raytracerGeometry.embreeGeometry_);

            geometryIndex.resize(geomID + 1);
            geometryIndex[geomID] = raytracerGeometry;
            geometryIndex[geomID].raytracerGeometryId_ = geomID;
            diffuseImages[raytracerGeometry.diffuseImageName_] = nullptr;
        }
    }

    // Finalize scene
    rtcCommitScene(scene);

    // Load images
    auto cache = context->GetCache();
    for (auto& nameAndImage : diffuseImages)
    {
        if (!nameAndImage.first.empty())
        {
            auto image = cache->GetResource<Image>(nameAndImage.first);
            nameAndImage.second = image->GetDecompressedImage();
        }
    }

    for (RaytracerGeometry& raytracerGeometry : geometryIndex)
    {
        raytracerGeometry.diffuseImage_ = diffuseImages[raytracerGeometry.diffuseImageName_];
        if (raytracerGeometry.diffuseImage_)
        {
            raytracerGeometry.diffuseImageWidth_ = raytracerGeometry.diffuseImage_->GetWidth();
            raytracerGeometry.diffuseImageHeight_ = raytracerGeometry.diffuseImage_->GetHeight();
        }
    }

    // Calculate max distance between objects
    BoundingBox boundingBox;
    for (Component* geometry : geometries)
    {
        if (auto staticModel = dynamic_cast<StaticModel*>(geometry))
            boundingBox.Merge(staticModel->GetWorldBoundingBox());
        else if (auto terrain = dynamic_cast<Terrain*>(geometry))
            boundingBox.Merge(terrain->CalculateWorldBoundingBox());
    }

    const Vector3 sceneSize = boundingBox.Size();
    const float maxDistance = ea::max({ sceneSize.x_, sceneSize.y_, sceneSize.z_ });

    return MakeShared<RaytracerScene>(context, device, scene, ea::move(geometryIndex), maxDistance);
}

}
