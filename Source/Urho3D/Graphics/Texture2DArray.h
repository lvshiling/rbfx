//
// Copyright (c) 2008-2022 the Urho3D project.
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

#include "../Container/Ptr.h"
#include "../Graphics/RenderSurface.h"
#include "../Graphics/Texture.h"

namespace Urho3D
{

class Deserializer;
class Image;

/// 2D texture array resource.
class URHO3D_API Texture2DArray : public Texture
{
    URHO3D_OBJECT(Texture2DArray, Texture);

public:
    /// Construct.
    explicit Texture2DArray(Context* context);
    /// Destruct.
    ~Texture2DArray() override;
    /// Register object factory.
    /// @nobind
    static void RegisterObject(Context* context);

    /// Load resource from stream. May be called from a worker thread. Return true if successful.
    bool BeginLoad(Deserializer& source) override;
    /// Finish resource loading. Always called from the main thread. Return true if successful.
    bool EndLoad() override;

    /// Set the number of layers in the texture. To be used before SetData.
    /// @property
    void SetLayers(unsigned layers);
    /// Set layers, size, format and usage. Set layers to zero to leave them unchanged. Return true if successful.
    bool SetSize(unsigned layers, int width, int height, TextureFormat format, TextureFlags flags = TextureFlag::None);
    /// Set data either partially or fully on a layer's mip level. Return true if successful.
    bool SetData(unsigned layer, unsigned level, int x, int y, int width, int height, const void* data);
    /// Set data of one layer from a stream. Return true if successful.
    bool SetData(unsigned layer, Deserializer& source);
    /// Set data of one layer from an image. Return true if successful. Optionally make a single channel image alpha-only.
    bool SetData(unsigned layer, Image* image);

    /// Return number of layers in the texture.
    /// @property
    unsigned GetLayers() const { return layers_; }
    /// Get data from a mip level. The destination buffer must be big enough. Return true if successful.
    bool GetData(unsigned layer, unsigned level, void* dest);

private:
    /// Texture array layers number.
    unsigned layers_{};
    /// Layer image files acquired during BeginLoad.
    ea::vector<SharedPtr<Image> > loadImages_;
    /// Parameter file acquired during BeginLoad.
    SharedPtr<XMLFile> loadParameters_;
};

}
