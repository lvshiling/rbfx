// Copyright (c) 2024-2024 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#pragma once

#include "Urho3D/Scene/Serializable.h"

namespace Urho3D
{

class RenderBufferManager;
class RenderPipelineInterface;
class RenderPipelineView;
struct RenderPipelineSettings;

/// Render pass traits that are important for render pipeline configuration.
struct RenderPassTraits
{
    /// Whether it's required to read from and write to color buffer at the same time.
    bool needReadWriteColorBuffer_{};
    /// Whether it's required that color sampling is at least bilinear.
    bool needBilinearColorSampler_{};
};

/// Execution context of the render pass.
struct RenderPassContext
{
    RenderPipelineInterface* renderPipelineInterface_{};
    RenderBufferManager* renderBufferManager_{};
};

/// Render pass, component of render path.
class URHO3D_API RenderPass
    : public Serializable
{
    URHO3D_OBJECT(RenderPass, Serializable);

public:
    explicit RenderPass(Context* context);
    ~RenderPass() override;
    static void RegisterObject(Context* context);

    /// Return unique pass name.
    virtual const ea::string& GetPassName() const;
    /// Create missing parameters in the global map with default values.
    virtual void CollectParameters(StringVariantMap& params) const {}
    /// Initialize render pass before using it in view.
    virtual void InitializeView(RenderPipelineView* view) {}
    /// Update settings and parameters of the pass.
    /// This function is always called before any rendering updates or getters.
    virtual void UpdateParameters(const RenderPipelineSettings& settings, const StringVariantMap& params) {}
    /// Execute render pass.
    virtual void Execute(const RenderPassContext& ctx) = 0;

    /// Attributes.
    /// @{
    void SetEnabled(bool enabled) { isEnabledByUser_ = enabled; }
    bool IsEnabledEffectively() const { return isEnabledByUser_ && isEnabledInternally_; }
    const RenderPassTraits& GetTraits() const { return traits_; }

    bool IsEnabledByDefault() const { return attributes_.isEnabledByDefault_; }
    void SetEnabledByDefault(bool enabled) { attributes_.isEnabledByDefault_ = enabled; }
    const ea::string& GetComment() const { return attributes_.comment_; }
    void SetComment(const ea::string& comment) { attributes_.comment_ = comment; }
    /// @}

protected:
    struct Attributes
    {
        ea::string passName_;
        bool isEnabledByDefault_{true};
        ea::string comment_;
    } attributes_;

    bool isEnabledByUser_{};
    bool isEnabledInternally_{};
    RenderPassTraits traits_;
};

}
