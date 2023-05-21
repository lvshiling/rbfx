//
// Copyright (c) 2017-2020 the Urho3D project.
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

#pragma once

#include "../Core/Object.h"
#include "../Graphics/GraphicsDefs.h"
#include "../Graphics/ComputeBuffer.h"

#include <EASTL/map.h>
#include <EASTL/vector.h>

#if defined(URHO3D_D3D11)
    struct ID3D11Buffer;
    struct ID3D11SamplerState;
    struct ID3D11ShaderResourceView;
    struct ID3D11UnorderedAccessView;
#elif defined(URHO3D_DILIGENT)
    #include <Diligent/Graphics/GraphicsEngine/interface/ResourceMapping.h>
    #include <Diligent/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h>
    #include "../Graphics/PipelineState.h"
    #include "../Engine/EngineEvents.h"
#elif defined(URHO3D_OPENGL)
#endif

#if defined(URHO3D_COMPUTE)

namespace Urho3D
{

class ConstantBuffer;
class IndexBuffer;
class Graphics;
class ShaderVariation;
class Texture;
class VertexBuffer;

// CD_UNIT == Comput Device - Binding Unit
#ifdef URHO3D_DILIGENT
#define CD_UNIT_TYPE ea::string
#define CD_UNIT const CD_UNIT_TYPE&
#else
#define CD_UNIT_TYPE unsigned
#define CD_UNIT CD_UNIT_TYPE 
#endif

//  On devices created at a Feature Level D3D_FEATURE_LEVEL_11_0, the maximum Compute Shader Unordered Access View slot is 7.
#define MAX_COMPUTE_WRITE_TARGETS 6

/// Common interface for GP-GPU that is responsible for dispatch and keeping track of the compute-specific state of the DX and GL APIs. Usage has no explicit rules but is most likely appropriate in
/// event handlers for E_BEGINRENDERING, E_ENDRENDERING, E_BEGINVIEWUPDATE, E_BEGINVIEWRENDER, and other events that are clean segues.
class URHO3D_API ComputeDevice : public Object
{
    URHO3D_OBJECT(ComputeDevice, Object);
public:
    /// Construct.
    ComputeDevice(Context*, Graphics*);
    /// Destruct.
    virtual ~ComputeDevice();

    /// Returns true if this compute device can actually execute, ie. a D3D9 level target on D3D11.
    bool IsSupported() const;

    /// Set a texture for reading as a traditional texture.
    bool SetReadTexture(Texture* texture, CD_UNIT unit);
    /// Set a constant buffer for standard usage.
    bool SetConstantBuffer(ConstantBuffer* buffer, CD_UNIT unit);

    /// Sets a texture for image write usage. Use UINT_MAX for faceIndex to bind all layers/faces.
    bool SetWriteTexture(Texture* texture, CD_UNIT unit, unsigned faceIndex, unsigned mipLevel);
    /// Sets a constant buffer for write usage. Compute write-capable buffers must NOT be dynamic.
    bool SetWriteBuffer(ConstantBuffer* buffer, CD_UNIT unit);
    /// Sets a vertex buffer for write usage, must be float4 compliant. Compute write-capable buffers must NOT be dynamic.
    bool SetWriteBuffer(VertexBuffer* buffer, CD_UNIT unit);
    /// Sets an index buffer for write usage. Compute write-capable buffers must NOT be dynamic.
    bool SetWriteBuffer(IndexBuffer* buffer, CD_UNIT unit);
    /// Sets a structured-buffer/SSBO for read/write usage.
    bool SetWriteBuffer(ComputeBuffer* buffer, CD_UNIT unit);
    /// Sets or clears the compute shader to use.
    bool SetProgram(ShaderVariation* computeShader);
    /// Dispatches the compute call, will queue a barrier as needed.
    void Dispatch(unsigned xDim, unsigned yDim, unsigned zDim);
    /// Apply all dirty GPU object bindings.
    /// TODO(compute): Handle it more nicely
    void ApplyBindings();

private:
    /// Setup necessary initial member state.
    void Init();
    /// Removes any constructed resources in response to a GPUObject::Release of a resource
    void HandleGPUResourceRelease(StringHash eventID, VariantMap& eventData);
    /// Frees any locally created GPU objects.
    void ReleaseLocalState();
    /// Internal implementation of buffer object setting.
    bool SetWritableBuffer(Object*, CD_UNIT slot);

#if defined(URHO3D_D3D11)
    /// Record for a mip+face UAV combination.
    struct UAVBinding {
        ID3D11UnorderedAccessView* uav_;
        unsigned face_;
        unsigned mipLevel_;
        bool isBuffer_;
    };

    /// As needed UAVs are constructed for textures and those UAVs are saved here.
    eastl::map<WeakPtr<Object>, eastl::vector<UAVBinding> > constructedUAVs_;
    /// As needed SRVs are constructed for buffers and those are saved here.
    eastl::map<WeakPtr<Object>, ID3D11UnorderedAccessView*> constructedBufferUAVs_;

    /// List of sampler bindings.
    ID3D11SamplerState* samplerBindings_[MAX_TEXTURE_UNITS];
    /// List of SRV bindings (textures or buffers).
    ID3D11ShaderResourceView* shaderResourceViews_[MAX_TEXTURE_UNITS];
    /// Constant buffer bindings.
    ID3D11Buffer* constantBuffers_[MAX_SHADER_PARAMETER_GROUPS]{};
    /// UAV targets for writing.
    ID3D11UnorderedAccessView* uavs_[MAX_COMPUTE_WRITE_TARGETS];
#elif defined(URHO3D_DILIGENT)
    /// Record for a mip+face UAV combination.
    struct UAVBinding {
        Diligent::RefCntAutoPtr<Diligent::IDeviceObject> uav_;
        unsigned face_;
        unsigned mipLevel_;
        bool isBuffer_;
    };

    /// As needed UAVs are constructed for textures and those UAVs are saved here.
    eastl::map<WeakPtr<Object>, eastl::vector<UAVBinding>> constructedUAVs_;
    /// As needed SRVs are constructed for buffers and those are saved here.
    eastl::map<WeakPtr<Object>, Diligent::RefCntAutoPtr<Diligent::IDeviceObject>> constructedBufferUAVs_;

    /// Table of bounded resources
    ea::unordered_map<ea::string, Diligent::RefCntAutoPtr<Diligent::IDeviceObject>> resources_{};

    /// Current Compute Pipeline
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_{};
    /// Current Pipeline Shader Resource Binding
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb_{};

    /// Record for pipeline+shader resource binding combination.
    struct CacheEntry {
        Diligent::RefCntAutoPtr<Diligent::IPipelineState> pipeline_;
        Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb_;
    };
    /// Compute Pipeline Cache Table. All compute pipelines will be saved here
    eastl::unordered_map<unsigned, CacheEntry> cachedPipelines_;

    /// Pipeline State Cache
    /// This object will reduce overhead at pipeline creation.
    WeakPtr<PipelineStateCache> psoCache_;

    bool resourcesDirty_{};
   
#elif defined(URHO3D_OPENGL)
    /// OpenGL requires some additional information in order to make the bind since a UAV-object isn't a thing.
    struct WriteTexBinding
    {
        SharedPtr<Texture> object_;
        int mipLevel_;
        int layer_;
        int layerCount_;
    };

    /// Structure for SSBO record list.
    struct WriteBufferBinding
    {
        unsigned object_ = { 0 };
        bool dirty_ = { false };
    };

    /// Table of bound constant buffers, uses the lower range of the parameter groups.
    ConstantBufferRange constantBuffers_[MAX_SHADER_PARAMETER_GROUPS]{};
    /// Table of write-texture targets.
    WriteTexBinding uavs_[MAX_COMPUTE_WRITE_TARGETS];
    /// Table of write-buffer targets (SSBO).
    WriteBufferBinding ssbos_[MAX_COMPUTE_WRITE_TARGETS];

#endif
    void HandleEngineInitialization(StringHash eventType, VariantMap& eventData);
    bool BuildPipeline();
    /// Handle to the graphics object for device specific access.
    Graphics* graphics_;
    /// Active compute shader that will be invoked with dispatch.
    WeakPtr<ShaderVariation> computeShader_;
#ifndef URHO3D_DILIGENT
    /// Tags samplers as dirty.
    bool samplersDirty_;
    /// Tags constant buffers as dirty.
    bool constantBuffersDirty_;
    /// Tags textures as dirty.
    bool texturesDirty_;
    /// Tags UAVs as dirty.
    bool uavsDirty_;
    /// Tag for the availability of compute, determined at startup.
    bool isComputeSupported_;
#endif
    /// Tag the shader program as dirty.
    bool programDirty_;
};

}

#endif

