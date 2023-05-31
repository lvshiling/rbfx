// Copyright (c) 2023-2023 the rbfx project.
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT> or the accompanying LICENSE file.

#include "Urho3D/Precompiled.h"

#include "Urho3D/RenderAPI/RenderDevice.h"

#include "Urho3D/Core/ProcessUtils.h"
#include "Urho3D/IO/Log.h"
#include "Urho3D/RenderAPI/GAPIIncludes.h"
#include "Urho3D/RenderAPI/RenderAPIUtils.h"

#include <Diligent/Common/interface/DefaultRawMemoryAllocator.hpp>
#include <Diligent/Graphics/GraphicsAccessories/interface/GraphicsAccessories.hpp>
#include <Diligent/Graphics/GraphicsEngine/include/SwapChainBase.hpp>
#include <Diligent/Graphics/GraphicsEngine/interface/RenderDevice.h>

#include <EASTL/sort.h>

#include <SDL.h>
#include <SDL_syswm.h>

#if D3D11_SUPPORTED
    #include <Diligent/Graphics/GraphicsEngineD3D11/interface/DeviceContextD3D11.h>
    #include <Diligent/Graphics/GraphicsEngineD3D11/interface/EngineFactoryD3D11.h>
    #include <Diligent/Graphics/GraphicsEngineD3D11/interface/RenderDeviceD3D11.h>
    #include <Diligent/Graphics/GraphicsEngineD3D11/interface/SwapChainD3D11.h>
#endif

#if D3D12_SUPPORTED
    #include <Diligent/Graphics/GraphicsEngineD3D12/interface/DeviceContextD3D12.h>
    #include <Diligent/Graphics/GraphicsEngineD3D12/interface/EngineFactoryD3D12.h>
    #include <Diligent/Graphics/GraphicsEngineD3D12/interface/RenderDeviceD3D12.h>
    #include <Diligent/Graphics/GraphicsEngineD3D12/interface/SwapChainD3D12.h>
#endif

#if VULKAN_SUPPORTED
    #include <Diligent/Graphics/GraphicsEngineVulkan/interface/DeviceContextVk.h>
    #include <Diligent/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h>
    #include <Diligent/Graphics/GraphicsEngineVulkan/interface/RenderDeviceVk.h>
    #include <Diligent/Graphics/GraphicsEngineVulkan/interface/SwapChainVk.h>
#endif

// OpenGL includes
#if GL_SUPPORTED || GLES_SUPPORTED
    #include <Diligent/Graphics/GraphicsEngineOpenGL/interface/DeviceContextGL.h>
    #include <Diligent/Graphics/GraphicsEngineOpenGL/interface/EngineFactoryOpenGL.h>
    #include <Diligent/Graphics/GraphicsEngineOpenGL/interface/RenderDeviceGL.h>
    #include <Diligent/Graphics/GraphicsEngineOpenGL/interface/SwapChainGL.h>
    #if GLES_SUPPORTED && (URHO3D_PLATFORM_WEB || URHO3D_PLATFORM_ANDROID)
        #include <Diligent/Graphics/GraphicsEngineOpenGL/interface/RenderDeviceGLES.h>
    #endif
#endif

#if URHO3D_PLATFORM_MACOS || URHO3D_PLATFORM_IOS || URHO3D_PLATFORM_TVOS
    #include <SDL_metal.h>
#endif

#if URHO3D_PLATFORM_UNIVERSAL_WINDOWS
    #include <windows.ui.core.h>
#endif

namespace Urho3D
{

namespace
{

void ValidateWindowSettings(WindowSettings& settings)
{
    const PlatformId platform = GetPlatform();

    // TODO(diligent): Revisit high-DPI support

    // iOS and tvOS app always take the fullscreen (and with status bar hidden)
    // TODO(diligent): What about Android?
    if (platform == PlatformId::iOS || platform == PlatformId::tvOS)
        settings.mode_ = WindowMode::Fullscreen;

    // Emscripten cannot be truly fullscreen
    // TODO(diligent): Maybe it should be only WindowMode::Windowed?
    if (platform == PlatformId::Web)
    {
        if (settings.mode_ == WindowMode::Fullscreen)
            settings.mode_ = WindowMode::Borderless;
    }

    // UWP doesn't support borderless windows
    if (platform == PlatformId::UniversalWindowsPlatform)
    {
        if (settings.mode_ == WindowMode::Borderless)
            settings.mode_ = WindowMode::Fullscreen;
    }

    // Ensure that monitor index is valid
    const int numMonitors = SDL_GetNumVideoDisplays();
    if (settings.monitor_ >= numMonitors || settings.monitor_ < 0)
        settings.monitor_ = 0;

    // Ensure that multisample factor is valid
    settings.multiSample_ = NextPowerOfTwo(Clamp(settings.multiSample_, 1, 16));

    if (platform == PlatformId::iOS)
        settings.resizable_ = true; // iOS window needs to be resizable to handle orientation changes properly
    else if (settings.mode_ != WindowMode::Windowed)
        settings.resizable_ = false; // Only Windowed window can be resizable

    // Deduce window size and refresh rate if not specified
    static const IntVector2 defaultWindowSize{1024, 768};
    SDL_DisplayMode mode;
    if (SDL_GetDesktopDisplayMode(settings.monitor_, &mode) != 0)
    {
        URHO3D_LOGERROR("Failed to get desktop display mode: {}", SDL_GetError());
        settings.mode_ = WindowMode::Windowed;
        settings.size_ = defaultWindowSize;
        settings.refreshRate_ = 60;
    }
    else
    {
        if (settings.size_ == IntVector2::ZERO)
            settings.size_ = settings.mode_ == WindowMode::Windowed ? defaultWindowSize : IntVector2{mode.w, mode.h};

        if (settings.refreshRate_ == 0 || settings.mode_ != WindowMode::Fullscreen)
            settings.refreshRate_ = mode.refresh_rate;
    }

    // If fullscreen, snap to the closest matching mode
    if (settings.mode_ == WindowMode::Fullscreen)
    {
        const FullscreenModeVector modes = RenderDevice::GetFullscreenModes(settings.monitor_);
        if (!modes.empty())
        {
            const FullscreenMode desiredMode{settings.size_, settings.refreshRate_};
            const FullscreenMode closestMode = RenderDevice::GetClosestFullscreenMode(modes, desiredMode);
            settings.size_ = closestMode.size_;
            settings.refreshRate_ = closestMode.refreshRate_;
        }
    }
}

unsigned ToSDLFlag(WindowMode mode)
{
    switch (mode)
    {
    case WindowMode::Fullscreen: return SDL_WINDOW_FULLSCREEN;
    case WindowMode::Borderless: return SDL_WINDOW_FULLSCREEN_DESKTOP;
    default:
    case WindowMode::Windowed: return 0;
    }
}

void SetWindowFullscreen(SDL_Window* window, const WindowSettings& settings)
{
    SDL_DisplayMode* fullscreenDisplayMode = nullptr;
    if (settings.mode_ == WindowMode::Fullscreen)
    {
        static SDL_DisplayMode temp;
        const SDL_DisplayMode desiredMode{
            SDL_PIXELFORMAT_UNKNOWN, settings.size_.x_, settings.size_.y_, settings.refreshRate_, nullptr};
        fullscreenDisplayMode = SDL_GetClosestDisplayMode(settings.monitor_, &desiredMode, &temp);
    }

    SDL_SetWindowFullscreen(window, 0);
    if (fullscreenDisplayMode)
        SDL_SetWindowDisplayMode(window, fullscreenDisplayMode);
    SDL_SetWindowFullscreen(window, ToSDLFlag(settings.mode_));
}

ea::shared_ptr<SDL_Window> CreateEmptyWindow(
    RenderBackend backend, const WindowSettings& settings, void* externalWindowHandle)
{
    unsigned flags = 0;
    if (externalWindowHandle == nullptr)
    {
        if (GetPlatform() != PlatformId::Web)
            flags |= SDL_WINDOW_ALLOW_HIGHDPI;
        if (settings.resizable_)
            flags |= SDL_WINDOW_RESIZABLE; // | SDL_WINDOW_MAXIMIZED;
        if (settings.mode_ == WindowMode::Borderless)
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        if (IsMetalBackend(backend))
        {
            flags |= SDL_WINDOW_METAL;
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, "metal");
        }
    }

    const int x = SDL_WINDOWPOS_UNDEFINED_DISPLAY(settings.monitor_);
    const int y = SDL_WINDOWPOS_UNDEFINED_DISPLAY(settings.monitor_);
    const int w = settings.size_.x_;
    const int h = settings.size_.y_;

    SDL_SetHint(SDL_HINT_ORIENTATIONS, ea::string::joined(settings.orientations_, " ").c_str());
    SDL_SetHint(SDL_HINT_VIDEO_EXTERNAL_CONTEXT, "1");
    SDL_Window* window = externalWindowHandle == nullptr //
        ? SDL_CreateWindow(settings.title_.c_str(), x, y, w, h, flags)
        : SDL_CreateWindowFrom(externalWindowHandle, flags);

    if (!window)
        throw RuntimeException("Could not create window: {}", SDL_GetError());

    SetWindowFullscreen(window, settings);

    // Window size is off on UWP if it was created with the same size as on previous run.
    // Tweak it a bit to force the correct size.
    if (GetPlatform() == PlatformId::UniversalWindowsPlatform && settings.mode_ == WindowMode::Windowed)
    {
        SDL_SetWindowSize(window, settings.size_.x_ - 1, settings.size_.y_ + 1);
        SDL_SetWindowSize(window, settings.size_.x_, settings.size_.y_);
    }

    return ea::shared_ptr<SDL_Window>(window, SDL_DestroyWindow);
}

ea::shared_ptr<SDL_Window> CreateOpenGLWindow(bool es, const WindowSettings& settings, void* externalWindowHandle)
{
    unsigned flags = SDL_WINDOW_OPENGL;
    if (externalWindowHandle == nullptr)
    {
        flags |= SDL_WINDOW_SHOWN;
        if (GetPlatform() != PlatformId::Web)
            flags |= SDL_WINDOW_ALLOW_HIGHDPI;
        if (settings.resizable_)
            flags |= SDL_WINDOW_RESIZABLE; // | SDL_WINDOW_MAXIMIZED; | SDL_WINDOW_MAXIMIZED;
        if (settings.mode_ == WindowMode::Borderless)
            flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    const int x = SDL_WINDOWPOS_UNDEFINED_DISPLAY(settings.monitor_);
    const int y = SDL_WINDOWPOS_UNDEFINED_DISPLAY(settings.monitor_);
    const int w = settings.size_.x_;
    const int h = settings.size_.y_;

    SDL_SetHint(SDL_HINT_ORIENTATIONS, ea::string::joined(settings.orientations_, " ").c_str());

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (es)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    }
    else
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    }

    for (int colorBits : {8, 1})
    {
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, colorBits);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, colorBits);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, colorBits);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, externalWindowHandle ? 8 : 0);

        for (int depthBits : {24, 16})
        {
            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depthBits);

            for (int stencilBits : {8, 0})
            {
                SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, stencilBits);

                for (bool srgb : {true, false})
                {
                    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, srgb);

                    for (int multiSample = settings.multiSample_; multiSample > 0; multiSample /= 2)
                    {
                        if (multiSample > 1)
                        {
                            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
                            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multiSample);
                        }
                        else
                        {
                            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
                            SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
                        }

                        SDL_Window* window = externalWindowHandle == nullptr
                            ? SDL_CreateWindow(settings.title_.c_str(), x, y, w, h, flags)
                            : SDL_CreateWindowFrom(externalWindowHandle, flags);

                        if (window)
                        {
                            SetWindowFullscreen(window, settings);

                            return ea::shared_ptr<SDL_Window>(window, SDL_DestroyWindow);
                        }
                    }
                }
            }
        }
    }

    throw RuntimeException("Could not create window: {}", SDL_GetError());
}

ea::shared_ptr<void> CreateMetalView(SDL_Window* window)
{
    SDL_MetalView metalView = SDL_Metal_CreateView(window);
    if (!metalView)
        throw RuntimeException("Could not create Metal view: {}", SDL_GetError());

    return ea::shared_ptr<void>(metalView, SDL_Metal_DestroyView);
}

/// @note This function is never used for OpenGL backend!
Diligent::NativeWindow GetNativeWindow(SDL_Window* window, void* metalView)
{
    Diligent::NativeWindow result;

#if !URHO3D_PLATFORM_WEB && !URHO3D_PLATFORM_MACOS
    SDL_SysWMinfo sysInfo;
    SDL_VERSION(&sysInfo.version);
    SDL_GetWindowWMInfo(window, &sysInfo);
#endif

#if URHO3D_PLATFORM_WINDOWS
    result.hWnd = sysInfo.info.win.window;
#elif URHO3D_PLATFORM_UNIVERSAL_WINDOWS
    result.pCoreWindow = sysInfo.info.winrt.window;
#elif URHO3D_PLATFORM_LINUX
    result.pDisplay = sysInfo.info.x11.display;
    result.WindowId = sysInfo.info.x11.window;
#elif URHO3D_PLATFORM_MACOS
    result.pNSView = metalView;
#elif URHO3D_PLATFORM_IOS || URHO3D_PLATFORM_TVOS
    result.pCALayer = sysInfo.info.uikit.window;
#elif URHO3D_PLATFORM_ANDROID
    result.pAWindow = sysInfo.info.android.window;
#elif URHO3D_PLATFORM_WEB
    result.pCanvasId = "canvas";
#endif
    return result;
}

unsigned FindBestAdapter(
    Diligent::IEngineFactory* engineFactory, const Diligent::Version& version, ea::optional<unsigned> hintAdapterId)
{
    unsigned numAdapters = 0;
    engineFactory->EnumerateAdapters(version, numAdapters, nullptr);
    ea::vector<Diligent::GraphicsAdapterInfo> adapters(numAdapters);
    engineFactory->EnumerateAdapters(version, numAdapters, adapters.data());

    if (hintAdapterId && *hintAdapterId < numAdapters)
        return *hintAdapterId;

    // Find best quality device
    unsigned result = Diligent::DEFAULT_ADAPTER_ID;
    for (unsigned i = 0; i < numAdapters; ++i)
    {
        const Diligent::GraphicsAdapterInfo& adapter = adapters[i];
        if (adapter.Type == Diligent::ADAPTER_TYPE_INTEGRATED || adapter.Type == Diligent::ADAPTER_TYPE_DISCRETE)
        {
            result = i;
            // Always prefer discrete gpu
            if (adapter.Type == Diligent::ADAPTER_TYPE_DISCRETE)
                break;
        }
    }
    return result;
}

ea::shared_ptr<void> CreateGLContext(SDL_Window* window)
{
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    return glContext ? ea::shared_ptr<void>(glContext, SDL_GL_DeleteContext) : nullptr;
}

#if GL_SUPPORTED || GLES_SUPPORTED
class ProxySwapChainGL : public Diligent::SwapChainBase<Diligent::ISwapChainGL>
{
public:
    using TBase = SwapChainBase<ISwapChainGL>;

    ProxySwapChainGL(Diligent::IReferenceCounters* pRefCounters, Diligent::IRenderDevice* pDevice,
        Diligent::IDeviceContext* pDeviceContext, const Diligent::SwapChainDesc& SCDesc, SDL_Window* window)
        : TBase(pRefCounters, pDevice, pDeviceContext, SCDesc)
        , window_(window)
    {
        InitializeParameters();
        CreateDummyBuffers();
    }

    /// Implement ISwapChainGL
    /// @{
    void DILIGENT_CALL_TYPE Present(Uint32 SyncInterval) override { SDL_GL_SwapWindow(window_); }

    void DILIGENT_CALL_TYPE SetFullscreenMode(const Diligent::DisplayModeAttribs& DisplayMode) override
    {
        URHO3D_ASSERT(false, "Fullscreen mode cannot be set through the proxy swap chain");
    }

    void DILIGENT_CALL_TYPE SetWindowedMode() override
    {
        URHO3D_ASSERT(false, "Windowed mode cannot be set through the proxy swap chain");
    }

    void DILIGENT_CALL_TYPE Resize(
        Uint32 NewWidth, Uint32 NewHeight, Diligent::SURFACE_TRANSFORM NewPreTransform) override
    {
        if (NewPreTransform == Diligent::SURFACE_TRANSFORM_OPTIMAL)
            NewPreTransform = Diligent::SURFACE_TRANSFORM_IDENTITY;
        URHO3D_ASSERT(NewPreTransform == Diligent::SURFACE_TRANSFORM_IDENTITY, "Unsupported pre-transform");

        if (TBase::Resize(NewWidth, NewHeight, NewPreTransform))
        {
            CreateDummyBuffers();
        }
    }

    GLuint DILIGENT_CALL_TYPE GetDefaultFBO() const override { return defaultFBO_; }

    Diligent::ITextureView* DILIGENT_CALL_TYPE GetCurrentBackBufferRTV() override { return renderTargetView_; }
    Diligent::ITextureView* DILIGENT_CALL_TYPE GetDepthBufferDSV() override { return depthStencilView_; }
    /// @}

private:
    void InitializeParameters()
    {
        if (m_SwapChainDesc.PreTransform == Diligent::SURFACE_TRANSFORM_OPTIMAL)
            m_SwapChainDesc.PreTransform = Diligent::SURFACE_TRANSFORM_IDENTITY;

        // Get default framebuffer for iOS platforms
        const PlatformId platform = GetPlatform();
        if (platform == PlatformId::iOS || platform == PlatformId::tvOS)
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, reinterpret_cast<GLint*>(&defaultFBO_));

        // Get swap chain parameters
        int width{};
        int height{};
        SDL_GL_GetDrawableSize(window_, &width, &height);
        m_SwapChainDesc.Width = static_cast<Uint32>(width);
        m_SwapChainDesc.Height = static_cast<Uint32>(height);

        m_SwapChainDesc.ColorBufferFormat =
            IsSRGB() ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB : Diligent::TEX_FORMAT_RGBA8_UNORM;
        m_SwapChainDesc.DepthBufferFormat = GetDepthStencilFormat();
    }

    void CreateDummyBuffers()
    {
        if (m_SwapChainDesc.Width == 0 || m_SwapChainDesc.Height == 0)
            return;

        Diligent::RefCntAutoPtr<Diligent::IRenderDeviceGL> deviceGL(m_pRenderDevice, Diligent::IID_RenderDeviceGL);

        Diligent::TextureDesc dummyTexDesc;
        dummyTexDesc.Name = "Back buffer proxy";
        dummyTexDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        dummyTexDesc.Format = m_SwapChainDesc.ColorBufferFormat;
        dummyTexDesc.Width = m_SwapChainDesc.Width;
        dummyTexDesc.Height = m_SwapChainDesc.Height;
        dummyTexDesc.BindFlags = Diligent::BIND_RENDER_TARGET;
        Diligent::RefCntAutoPtr<Diligent::ITexture> dummyRenderTarget;
        deviceGL->CreateDummyTexture(dummyTexDesc, Diligent::RESOURCE_STATE_RENDER_TARGET, &dummyRenderTarget);
        renderTargetView_ = dummyRenderTarget->GetDefaultView(Diligent::TEXTURE_VIEW_RENDER_TARGET);

        dummyTexDesc.Name = "Depth buffer proxy";
        dummyTexDesc.Format = m_SwapChainDesc.DepthBufferFormat;
        dummyTexDesc.BindFlags = Diligent::BIND_DEPTH_STENCIL;
        Diligent::RefCntAutoPtr<Diligent::ITexture> dummyDepthBuffer;
        deviceGL->CreateDummyTexture(dummyTexDesc, Diligent::RESOURCE_STATE_DEPTH_WRITE, &dummyDepthBuffer);
        depthStencilView_ = dummyDepthBuffer->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
    }

    bool IsSRGB() const
    {
        int effectiveSRGB{};
        if (SDL_GL_GetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, &effectiveSRGB) != 0)
            return false;
        return effectiveSRGB != 0;
    }

    Diligent::TEXTURE_FORMAT GetDepthStencilFormat() const
    {
        static const Diligent::TEXTURE_FORMAT defaultFormat = Diligent::TEX_FORMAT_D24_UNORM_S8_UINT;

        int effectiveDepthBits{};
        if (SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &effectiveDepthBits) != 0)
            return defaultFormat;
        int effectiveStencilBits{};
        if (SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &effectiveStencilBits) != 0)
            return defaultFormat;

        if (effectiveDepthBits == 16 && effectiveStencilBits == 0)
            return Diligent::TEX_FORMAT_D16_UNORM;
        else if (effectiveDepthBits == 24 && effectiveStencilBits == 0)
            return Diligent::TEX_FORMAT_D24_UNORM_S8_UINT;
        else if (effectiveDepthBits == 24 && effectiveStencilBits == 8)
            return Diligent::TEX_FORMAT_D24_UNORM_S8_UINT;
        else if (effectiveDepthBits == 32 && effectiveStencilBits == 0)
            return Diligent::TEX_FORMAT_D32_FLOAT;
        else if (effectiveDepthBits == 32 && effectiveStencilBits == 8)
            return Diligent::TEX_FORMAT_D32_FLOAT_S8X24_UINT;
        else
            return defaultFormat;
    }

    SDL_Window* window_{};

    Diligent::RefCntAutoPtr<Diligent::ITextureView> renderTargetView_;
    Diligent::RefCntAutoPtr<Diligent::ITextureView> depthStencilView_;

    GLuint defaultFBO_{};
};
#endif

#if URHO3D_PLATFORM_UNIVERSAL_WINDOWS
IntVector2 CalculateSwapChainSize(SDL_Window* window)
{
    SDL_SysWMinfo sysInfo;
    SDL_VERSION(&sysInfo.version);
    SDL_GetWindowWMInfo(window, &sysInfo);

    const auto displayInfo = Windows::Graphics::Display::DisplayInformation::GetForCurrentView();
    const float dpiScale = displayInfo->LogicalDpi / 96.0f;

    const auto coreWindow = reinterpret_cast<Windows::UI::Core::CoreWindow^>(sysInfo.info.winrt.window);
    const float width = coreWindow->Bounds.Width * dpiScale;
    const float height = coreWindow->Bounds.Height * dpiScale;
    return VectorCeilToInt(Vector2{width, height});
}
#endif

} // namespace

RenderDevice::RenderDevice(Context* context, const RenderDeviceSettings& settings)
    : Object(context)
    , settings_(settings)
{
    if (settings_.externalWindowHandle_)
        settings_.window_.mode_ = WindowMode::Windowed;

    ValidateWindowSettings(settings_.window_);
    InitializeWindow();
    InitializeFactory();
    InitializeDevice();

    const Diligent::SwapChainDesc& desc = swapChain_->GetDesc();
    URHO3D_LOGINFO("RenderDevice is initialized for {}: size={}x{}px ({}x{}dp), color={}, depth={}",
        ToString(settings_.backend_), desc.Width, desc.Height, settings_.window_.size_.x_, settings_.window_.size_.y_,
        Diligent::GetTextureFormatAttribs(desc.ColorBufferFormat).Name,
        Diligent::GetTextureFormatAttribs(desc.DepthBufferFormat).Name);
}

RenderDevice::~RenderDevice()
{
}

void RenderDevice::InitializeWindow()
{
    if (settings_.backend_ == RenderBackend::OpenGL)
    {
        window_ = CreateOpenGLWindow(
            IsOpenGLESBackend(settings_.backend_), settings_.window_, settings_.externalWindowHandle_);

        glContext_ = CreateGLContext(window_.get());
        if (!glContext_)
            throw RuntimeException("Could not create OpenGL context: {}", SDL_GetError());

        int effectiveMultiSample{};
        if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &settings_.window_.multiSample_) == 0)
            settings_.window_.multiSample_ = ea::max(1, effectiveMultiSample);

        int effectiveSRGB{};
        if (SDL_GL_GetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, &effectiveSRGB) == 0)
            settings_.window_.sRGB_ = effectiveSRGB != 0;

        SDL_GL_SetSwapInterval(settings_.window_.vSync_ ? 1 : 0);
    }
    else
    {
        window_ = CreateEmptyWindow(settings_.backend_, settings_.window_, settings_.externalWindowHandle_);
        if (IsMetalBackend(settings_.backend_))
            metalView_ = CreateMetalView(window_.get());
    }

    SDL_GetWindowSize(window_.get(), &settings_.window_.size_.x_, &settings_.window_.size_.y_);
}

void RenderDevice::InitializeFactory()
{
    switch (settings_.backend_)
    {
#if D3D11_SUPPORTED
    case RenderBackend::D3D11:
        factoryD3D11_ = Diligent::GetEngineFactoryD3D11();
        factory_ = factoryD3D11_;
        break;
#endif
#if D3D12_SUPPORTED
    case RenderBackend::D3D12:
        factoryD3D12_ = Diligent::GetEngineFactoryD3D12();
        if (!factoryD3D12_->LoadD3D12())
            throw RuntimeException("Could not load D3D12 runtime");
        factory_ = factoryD3D12_;
        break;
#endif
#if GL_SUPPORTED || GLES_SUPPORTED
    case RenderBackend::OpenGL:
        factoryOpenGL_ = Diligent::GetEngineFactoryOpenGL();
        factory_ = factoryOpenGL_;
        break;
#endif
#if VULKAN_SUPPORTED
    case RenderBackend::Vulkan:
        factoryVulkan_ = Diligent::GetEngineFactoryVk();
        factory_ = factoryVulkan_;
        break;
#endif
    default: throw RuntimeException("Unsupported render backend");
    }
}

void RenderDevice::InitializeDevice()
{
    Diligent::NativeWindow nativeWindow = GetNativeWindow(window_.get(), metalView_.get());

    const Diligent::TEXTURE_FORMAT colorFormats[2][2] = {
        {Diligent::TEX_FORMAT_RGBA8_UNORM, Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB},
        {Diligent::TEX_FORMAT_BGRA8_UNORM, Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB},
    };

    // TODO(diligent): Why is that?
    const bool isBGRA = settings_.backend_ == RenderBackend::Vulkan;

    Diligent::SwapChainDesc swapChainDesc{};
    swapChainDesc.ColorBufferFormat = colorFormats[isBGRA][settings_.window_.sRGB_];
    swapChainDesc.DepthBufferFormat = Diligent::TEX_FORMAT_D24_UNORM_S8_UINT;
#if URHO3D_PLATFORM_UNIVERSAL_WINDOWS
    const IntVector2 swapChainSize = CalculateSwapChainSize(window_.get());
    swapChainDesc.Width = swapChainSize.x_;
    swapChainDesc.Height = swapChainSize.y_;
#endif

    Diligent::FullScreenModeDesc fullscreenDesc{};
    fullscreenDesc.Fullscreen = settings_.window_.mode_ == WindowMode::Fullscreen;
    fullscreenDesc.RefreshRateNumerator = settings_.window_.refreshRate_;
    fullscreenDesc.RefreshRateDenominator = 1;

    switch (settings_.backend_)
    {
#if D3D11_SUPPORTED
    case RenderBackend::D3D11:
    {
        Diligent::EngineD3D11CreateInfo createInfo;
        createInfo.GraphicsAPIVersion = Diligent::Version{11, 0};
        createInfo.AdapterId = FindBestAdapter(factory_, createInfo.GraphicsAPIVersion, settings_.adapterId_);
        createInfo.EnableValidation = true;
        createInfo.D3D11ValidationFlags = Diligent::D3D11_VALIDATION_FLAG_VERIFY_COMMITTED_RESOURCE_RELEVANCE;

        factoryD3D11_->CreateDeviceAndContextsD3D11(createInfo, &renderDevice_, &deviceContext_);
        factoryD3D11_->CreateSwapChainD3D11(
            renderDevice_, deviceContext_, swapChainDesc, fullscreenDesc, nativeWindow, &swapChain_);

        renderDeviceD3D11_ =
            Diligent::RefCntAutoPtr<Diligent::IRenderDeviceD3D11>(renderDevice_, Diligent::IID_RenderDeviceD3D11);
        deviceContextD3D11_ =
            Diligent::RefCntAutoPtr<Diligent::IDeviceContextD3D11>(deviceContext_, Diligent::IID_DeviceContextD3D11);
        swapChainD3D11_ = Diligent::RefCntAutoPtr<Diligent::ISwapChainD3D11>(swapChain_, Diligent::IID_SwapChainD3D11);
        break;
    }
#endif
#if D3D12_SUPPORTED
    case RenderBackend::D3D12:
    {
        Diligent::EngineD3D12CreateInfo createInfo;
        createInfo.GraphicsAPIVersion = Diligent::Version{11, 0};
        // TODO(diligent): Revisit limits, make configurable?
        createInfo.GPUDescriptorHeapDynamicSize[0] = 32768;
        createInfo.DynamicDescriptorAllocationChunkSize[0] = 32;
        createInfo.DynamicDescriptorAllocationChunkSize[1] = 8;
        createInfo.AdapterId = FindBestAdapter(factory_, createInfo.GraphicsAPIVersion, settings_.adapterId_);

        factoryD3D12_->CreateDeviceAndContextsD3D12(createInfo, &renderDevice_, &deviceContext_);
        factoryD3D12_->CreateSwapChainD3D12(
            renderDevice_, deviceContext_, swapChainDesc, fullscreenDesc, nativeWindow, &swapChain_);

        renderDeviceD3D12_ =
            Diligent::RefCntAutoPtr<Diligent::IRenderDeviceD3D12>(renderDevice_, Diligent::IID_RenderDeviceD3D12);
        deviceContextD3D12_ =
            Diligent::RefCntAutoPtr<Diligent::IDeviceContextD3D12>(deviceContext_, Diligent::IID_DeviceContextD3D12);
        swapChainD3D12_ = Diligent::RefCntAutoPtr<Diligent::ISwapChainD3D12>(swapChain_, Diligent::IID_SwapChainD3D12);
        break;
    }
#endif
#if VULKAN_SUPPORTED
    case RenderBackend::Vulkan:
    {
        Diligent::EngineVkCreateInfo createInfo;
        const char* const ppIgnoreDebugMessages[] = {
            // Validation Performance Warning: [ UNASSIGNED-CoreValidation-Shader-OutputNotConsumed ]
            // vertex shader writes to output location 1.0 which is not consumed by fragment shader
            "UNASSIGNED-CoreValidation-Shader-OutputNotConsumed" //
        };
        createInfo.Features = Diligent::DeviceFeatures{Diligent::DEVICE_FEATURE_STATE_OPTIONAL};
        createInfo.Features.TransferQueueTimestampQueries = Diligent::DEVICE_FEATURE_STATE_DISABLED;
        // TODO(diligent): Revisit limits, make configurable?
        createInfo.DynamicHeapSize = 32 << 20;
        createInfo.ppIgnoreDebugMessageNames = ppIgnoreDebugMessages;
        createInfo.IgnoreDebugMessageCount = _countof(ppIgnoreDebugMessages);
        createInfo.AdapterId = FindBestAdapter(factory_, createInfo.GraphicsAPIVersion, settings_.adapterId_);

        factoryVulkan_->CreateDeviceAndContextsVk(createInfo, &renderDevice_, &deviceContext_);
        factoryVulkan_->CreateSwapChainVk(renderDevice_, deviceContext_, swapChainDesc, nativeWindow, &swapChain_);

        renderDeviceVulkan_ =
            Diligent::RefCntAutoPtr<Diligent::IRenderDeviceVk>(renderDevice_, Diligent::IID_RenderDeviceVk);
        deviceContextVulkan_ =
            Diligent::RefCntAutoPtr<Diligent::IDeviceContextVk>(deviceContext_, Diligent::IID_DeviceContextVk);
        swapChainVulkan_ = Diligent::RefCntAutoPtr<Diligent::ISwapChainVk>(swapChain_, Diligent::IID_SwapChainVk);
        break;
    }
#endif
#if GL_SUPPORTED || GLES_SUPPORTED
    case RenderBackend::OpenGL:
    {
        Diligent::EngineGLCreateInfo createInfo;
        createInfo.AdapterId = FindBestAdapter(factory_, createInfo.GraphicsAPIVersion, settings_.adapterId_);

        factoryOpenGL_->AttachToActiveGLContext(createInfo, &renderDevice_, &deviceContext_);

        renderDeviceGL_ =
            Diligent::RefCntAutoPtr<Diligent::IRenderDeviceGL>(renderDevice_, Diligent::IID_RenderDeviceGL);
        deviceContextGL_ =
            Diligent::RefCntAutoPtr<Diligent::IDeviceContextGL>(deviceContext_, Diligent::IID_DeviceContextGL);
    #if GLES_SUPPORTED && (URHO3D_PLATFORM_WEB || URHO3D_PLATFORM_ANDROID)
        renderDeviceGLES_ =
            Diligent::RefCntAutoPtr<Diligent::IRenderDeviceGLES>(renderDevice_, Diligent::IID_RenderDeviceGLES);
    #endif

        auto& defaultAllocator = Diligent::DefaultRawMemoryAllocator::GetAllocator();
        swapChainGL_ = NEW_RC_OBJ(defaultAllocator, "ProxySwapChainGL instance", ProxySwapChainGL)(
            renderDevice_, deviceContext_, swapChainDesc, window_.get());
        deviceContextGL_->SetSwapChain(swapChainGL_);

        swapChain_ = swapChainGL_;
        break;
    }
#endif
    default: throw RuntimeException("Unsupported render backend");
    }
}

Diligent::RefCntAutoPtr<Diligent::ISwapChain> RenderDevice::CreateSecondarySwapChain(SDL_Window* sdlWindow)
{
    const auto metalView = IsMetalBackend(settings_.backend_) ? CreateMetalView(sdlWindow) : nullptr;
    Diligent::NativeWindow nativeWindow = GetNativeWindow(sdlWindow, metalView.get());
    Diligent::SwapChainDesc swapChainDesc{};
    swapChainDesc.ColorBufferFormat = swapChain_->GetDesc().ColorBufferFormat;
    swapChainDesc.DepthBufferFormat = swapChain_->GetDesc().DepthBufferFormat;
    Diligent::FullScreenModeDesc fullscreenDesc{};

    switch (settings_.backend_)
    {
#if D3D11_SUPPORTED
    case RenderBackend::D3D11:
    {
        Diligent::RefCntAutoPtr<Diligent::ISwapChain> secondarySwapChain;
        factoryD3D11_->CreateSwapChainD3D11(
            renderDevice_, deviceContext_, swapChainDesc, fullscreenDesc, nativeWindow, &secondarySwapChain);
        return secondarySwapChain;
    }
#endif
#if D3D12_SUPPORTED
    case RenderBackend::D3D12:
    {
        Diligent::RefCntAutoPtr<Diligent::ISwapChain> secondarySwapChain;
        factoryD3D12_->CreateSwapChainD3D12(
            renderDevice_, deviceContext_, swapChainDesc, fullscreenDesc, nativeWindow, &secondarySwapChain);
        return secondarySwapChain;
    }
#endif
#if VULKAN_SUPPORTED
    case RenderBackend::Vulkan:
    {
        Diligent::RefCntAutoPtr<Diligent::ISwapChain> secondarySwapChain;
        factoryVulkan_->CreateSwapChainVk(
            renderDevice_, deviceContext_, swapChainDesc, nativeWindow, &secondarySwapChain);
        return secondarySwapChain;
    }
#endif
#if GL_SUPPORTED || GLES_SUPPORTED
    case RenderBackend::OpenGL:
    {
        SDL_GLContext currentContext = SDL_GL_GetCurrentContext();
        URHO3D_ASSERT(currentContext && currentContext != glContext_.get());

        auto& defaultAllocator = Diligent::DefaultRawMemoryAllocator::GetAllocator();
        auto secondarySwapChain = NEW_RC_OBJ(defaultAllocator, "Secondary ProxySwapChainGL instance", ProxySwapChainGL)(
            renderDevice_, deviceContext_, swapChainDesc, sdlWindow);

        return Diligent::RefCntAutoPtr<Diligent::ISwapChain>{secondarySwapChain};
    }
#endif
    default:
    {
        URHO3D_ASSERT(false, "Unsupported render backend");
        return {};
    }
    }
}

void RenderDevice::UpdateSwapChainSize()
{
    const IntVector2 oldWindowSize = settings_.window_.size_;
    const IntVector2 oldSwapChainSize = GetSwapChainSize();

    SDL_GetWindowSize(window_.get(), &settings_.window_.size_.x_, &settings_.window_.size_.y_);

    switch (settings_.backend_)
    {
#if GL_SUPPORTED || GLES_SUPPORTED
    case RenderBackend::OpenGL:
    {
        // OpenGL is managed by SDL, use SDL_GL_GetDrawableSize to get the actual size
        int width{};
        int height{};
        SDL_GL_GetDrawableSize(window_.get(), &width, &height);

        swapChain_->Resize(width, height);
        break;
    }
#endif
#if VULKAN_SUPPORTED
    case RenderBackend::Vulkan:
    {
        const VkPhysicalDevice physicalDevice = renderDeviceVulkan_->GetVkPhysicalDevice();
        const VkSurfaceKHR surface = swapChainVulkan_->GetVkSurface();

        VkSurfaceCapabilitiesKHR surfCapabilities = {};
        const VkResult err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfCapabilities);
        if (err == VK_SUCCESS && surfCapabilities.currentExtent.width != 0xFFFFFFFF)
        {
            swapChain_->Resize(surfCapabilities.currentExtent.width, surfCapabilities.currentExtent.height);
        }
        else
        {
            URHO3D_LOGERROR("Cannot resize Vulkan swap chain");
        }

        break;
    }
#endif
#if D3D11_SUPPORTED || D3D12_SUPPORTED
    case RenderBackend::D3D11:
    case RenderBackend::D3D12:
    {
    #if URHO3D_PLATFORM_WINDOWS
        // Use client rect to get the actual size on Windows
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(window_.get(), &wmInfo);

        RECT rect;
        GetClientRect(wmInfo.info.win.window, &rect);
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        swapChain_->Resize(width, height);
    #elif URHO3D_PLATFORM_UNIVERSAL_WINDOWS
        const IntVector2 swapChainSize = CalculateSwapChainSize(window_.get());
        swapChain_->Resize(swapChainSize.x_, swapChainSize.y_);
    #else
        URHO3D_ASSERT(false, "Unsupported render backend");
    #endif
        break;
    }
#endif
    default:
    {
        URHO3D_ASSERT(false, "Unsupported render backend");
        break;
    }
    }

    const IntVector2 newSwapChainSize = GetSwapChainSize();
    if (oldWindowSize != settings_.window_.size_ || oldSwapChainSize != newSwapChainSize)
    {
        URHO3D_LOGINFO("Swap chain is resized to {}x{}px ({}x{}dp)", newSwapChainSize.x_,
            newSwapChainSize.y_, settings_.window_.size_.x_, settings_.window_.size_.y_);
    }
}

void RenderDevice::UpdateWindowSettings(const WindowSettings& settings)
{
    WindowSettings& oldSettings = settings_.window_;
    WindowSettings newSettings = settings;
    ValidateWindowSettings(newSettings);

    const bool sizeChanged = oldSettings.size_ != newSettings.size_;
    if (sizeChanged || oldSettings.mode_ != newSettings.mode_ || oldSettings.refreshRate_ != newSettings.refreshRate_)
    {
        oldSettings.size_ = newSettings.size_;
        oldSettings.mode_ = newSettings.mode_;
        oldSettings.refreshRate_ = newSettings.refreshRate_;

        if (sizeChanged && oldSettings.mode_ == WindowMode::Windowed)
        {
            if (GetPlatform() != PlatformId::UniversalWindowsPlatform)
                SDL_SetWindowSize(window_.get(), oldSettings.size_.x_, oldSettings.size_.y_);
            else
                URHO3D_LOGWARNING("Window resize by application is not supported in UWP.");
        }
        SetWindowFullscreen(window_.get(), oldSettings);

        UpdateSwapChainSize();
    }

    if (oldSettings.monitor_ != newSettings.monitor_)
    {
        oldSettings.monitor_ = newSettings.monitor_;

        const int x = SDL_WINDOWPOS_UNDEFINED_DISPLAY(newSettings.monitor_);
        const int y = SDL_WINDOWPOS_UNDEFINED_DISPLAY(newSettings.monitor_);
        SDL_SetWindowPosition(window_.get(), x, y);
    }

    if (oldSettings.title_ != newSettings.title_)
    {
        oldSettings.title_ = newSettings.title_;

        SDL_SetWindowTitle(window_.get(), newSettings.title_.c_str());
    }

    if (oldSettings.resizable_ != newSettings.resizable_)
    {
        oldSettings.resizable_ = newSettings.resizable_;

        SDL_SetWindowResizable(window_.get(), newSettings.resizable_ ? SDL_TRUE : SDL_FALSE);
    }

    if (oldSettings.vSync_ != newSettings.vSync_)
    {
        oldSettings.vSync_ = newSettings.vSync_;

        if (settings_.backend_ == RenderBackend::OpenGL)
            SDL_GL_SetSwapInterval(settings_.window_.vSync_ ? 1 : 0);
    }
}

bool RenderDevice::Restore()
{
#if URHO3D_PLATFORM_ANDROID && GLES_SUPPORTED
    URHO3D_ASSERT(settings_.backend_ == RenderBackend::OpenGL);

    if (!SDL_GL_GetCurrentContext())
    {
        InvalidateGLESContext();
        return RestoreGLESContext();
    }
#endif
    return true;
}

bool RenderDevice::EmulateLossAndRestore()
{
    if (GetPlatform() != PlatformId::Android)
        return true;

    // TODO(diligent): Support Vulkan on Android
    if (settings_.backend_ != RenderBackend::OpenGL)
        return true;

    InvalidateGLESContext();
    SDL_Delay(250);
    return RestoreGLESContext();
}

void RenderDevice::InvalidateGLESContext()
{
#if URHO3D_PLATFORM_ANDROID && GLES_SUPPORTED
    URHO3D_LOGINFO("OpenGL context is lost");
    OnDeviceLost(this);
    deviceContextGL_->InvalidateState();
    renderDeviceGLES_->Invalidate();
    glContext_ = nullptr;
#else
    URHO3D_LOGWARNING("RenderDevice::InvalidateGLContext is supported only for Android platform");
#endif
}

bool RenderDevice::RestoreGLESContext()
{
#if URHO3D_PLATFORM_ANDROID && GLES_SUPPORTED
    glContext_ = CreateGLContext(window_.get());
    if (!glContext_)
    {
        URHO3D_LOGERROR("Could not restore OpenGL context: {}", SDL_GetError());
        return false;
    }

    renderDeviceGLES_->Resume(nullptr);
    OnDeviceRestored(this);
    URHO3D_LOGINFO("OpenGL context is restored");
    return true;
#else
    URHO3D_LOGWARNING("RenderDevice::RestoreGLContext is supported only for Android platform");
    return true;
#endif
}

void RenderDevice::Present()
{
    swapChain_->Present(settings_.window_.vSync_ ? 1 : 0);

    // If using an external window, check it for size changes, and reset screen mode if necessary
    if (settings_.externalWindowHandle_ != nullptr)
    {
        IntVector2 currentSize;
        SDL_GetWindowSize(window_.get(), &currentSize.x_, &currentSize.y_);

        if (settings_.window_.size_ != currentSize)
            UpdateSwapChainSize();
    }
}

IntVector2 RenderDevice::GetSwapChainSize() const
{
    if (!swapChain_)
        return IntVector2::ZERO;
    const Diligent::SwapChainDesc& desc = swapChain_->GetDesc();
    return {static_cast<int>(desc.Width), static_cast<int>(desc.Height)};
}

IntVector2 RenderDevice::GetWindowSize() const
{
    return settings_.window_.size_;
}

float RenderDevice::GetDpiScale() const
{
    const Vector2 ratio = GetSwapChainSize().ToVector2() / GetWindowSize().ToVector2();
    // This is just a hack to get rid of possible rounding errors
    return SnapRound((ratio.x_ + ratio.y_) / 2.0f, 0.05f);
}

ea::vector<FullscreenMode> RenderDevice::GetFullscreenModes(int monitor)
{
    ea::vector<FullscreenMode> result;
#if URHO3D_PLATFORM_WEB
    // Emscripten is not able to return a valid list
#else
    const int numModes = SDL_GetNumDisplayModes(monitor);
    for (int i = 0; i < numModes; ++i)
    {
        SDL_DisplayMode mode;
        SDL_GetDisplayMode(monitor, i, &mode);

        result.push_back(FullscreenMode{{mode.w, mode.h}, mode.refresh_rate});
    }

    ea::sort(result.begin(), result.end());
    result.erase(ea::unique(result.begin(), result.end()), result.end());
#endif
    return result;
}

unsigned RenderDevice::GetClosestFullscreenModeIndex(const FullscreenModeVector& modes, FullscreenMode desiredMode)
{
    URHO3D_ASSERT(!modes.empty());

    // 1. Try to find exact match
    {
        const auto iterMatch = ea::find(modes.begin(), modes.end(), desiredMode);
        if (iterMatch != modes.end())
            return static_cast<unsigned>(ea::distance(modes.begin(), iterMatch));
    }

    // 2. Try to find exact resolution match with different refresh rate
    const auto iter = ea::upper_bound(modes.begin(), modes.end(), FullscreenMode{desiredMode.size_, M_MAX_INT});
    if (iter != modes.begin())
    {
        const auto iterMatch = ea::prev(iter);
        if (iterMatch->size_ == desiredMode.size_)
            return static_cast<unsigned>(ea::distance(modes.begin(), iterMatch));
    }

    // 3. Try to find better mode
    if (iter != modes.end())
    {
        const auto isBetter = [&](const FullscreenMode& mode) { return mode.refreshRate_ >= desiredMode.refreshRate_; };
        const auto iterBetterMatch = ea::find_if(iter, modes.end(), isBetter);
        const auto iterMatch = iterBetterMatch != modes.end() ? iterBetterMatch : iter;
        return static_cast<unsigned>(ea::distance(modes.begin(), iterMatch));
    }

    // 4. Pick the best mode
    return modes.size() - 1;
}

FullscreenMode RenderDevice::GetClosestFullscreenMode(const FullscreenModeVector& modes, FullscreenMode desiredMode)
{
    const unsigned index = GetClosestFullscreenModeIndex(modes, desiredMode);
    return modes[index];
}

} // namespace Urho3D
