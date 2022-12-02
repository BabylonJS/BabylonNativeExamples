#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Plugins/ExternalTexture.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>

#include <winrt/base.h>

#include <WICTextureLoader.h>
#include <ScreenGrab.h>
#include <wincodec.h>

#include <filesystem>
#include <iostream>

#include "RenderDoc.h"

namespace
{
    constexpr const uint32_t WIDTH = 1024;
    constexpr const uint32_t HEIGHT = 1024;

    std::filesystem::path GetModulePath()
    {
        WCHAR modulePath[4096];
        DWORD result = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
        winrt::check_bool(result != 0 && result != std::size(modulePath));
        return std::filesystem::path{modulePath}.parent_path();
    }

    winrt::com_ptr<ID3D11Device> CreateD3DDevice()
    {
        winrt::com_ptr<ID3D11Device> d3dDevice{};
        uint32_t flags = D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, d3dDevice.put(), nullptr, nullptr));
        return d3dDevice;
    }

    winrt::com_ptr<ID3D11Texture2D> CreateD3DRenderTargetTexture(ID3D11Device* d3dDevice)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = WIDTH;
        desc.Height = HEIGHT;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc = {1, 0};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        winrt::com_ptr<ID3D11Texture2D> texture;
        winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, texture.put()));
        return texture;
    }

    std::unique_ptr<Babylon::Graphics::Device> CreateGraphicsDevice(ID3D11Device* d3dDevice)
    {
        Babylon::Graphics::DeviceConfiguration config{d3dDevice};
        std::unique_ptr<Babylon::Graphics::Device> device = Babylon::Graphics::Device::Create(config);
        device->UpdateSize(WIDTH, HEIGHT);
        return device;
    }
}

int main()
{
    auto d3dDevice = CreateD3DDevice();

    winrt::com_ptr<ID3D11DeviceContext> context;
    d3dDevice->GetImmediateContext(context.put());

    auto device = CreateGraphicsDevice(d3dDevice.get());
    auto deviceUpdate = std::make_unique<Babylon::Graphics::DeviceUpdate>(device->GetUpdate("update"));

    device->StartRenderingCurrentFrame();
    deviceUpdate->Start();

    auto runtime = std::make_unique<Babylon::AppRuntime>();
    runtime->Dispatch([&device](Napi::Env env)
    {
        device->AddToJavaScript(env);

        Babylon::Polyfills::Console::Initialize(env, [](const char* message, auto)
        {
            std::cout << message;
        });

        Babylon::Polyfills::Window::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);

        Babylon::Plugins::NativeEngine::Initialize(env);
    });

    Babylon::ScriptLoader loader{*runtime};
    loader.LoadScript("app:///Scripts/babylon.max.js");
    loader.LoadScript("app:///Scripts/babylonjs.loaders.js");
    loader.LoadScript("app:///Scripts/index.js");

    // Create a render target texture for the output.
    winrt::com_ptr<ID3D11Texture2D> outputTexture = CreateD3DRenderTargetTexture(d3dDevice.get());

    std::promise<void> addToContext{};
    std::promise<void> startup{};

    loader.Dispatch([externalTexture = Babylon::Plugins::ExternalTexture{outputTexture.get()}, &addToContext, &startup](Napi::Env env)
    {
        auto jsPromise = externalTexture.AddToContextAsync(env);
        addToContext.set_value();

        jsPromise.Get("then").As<Napi::Function>().Call(jsPromise,
        {
            Napi::Function::New(env, [&startup](const Napi::CallbackInfo& info)
            {
                auto nativeTexture = info[0];
                info.Env().Global().Get("startup").As<Napi::Function>().Call(
                {
                    nativeTexture,
                    Napi::Value::From(info.Env(), WIDTH),
                    Napi::Value::From(info.Env(), HEIGHT),
                });
                startup.set_value();
            })
        });
    });

    addToContext.get_future().wait();

    deviceUpdate->Finish();
    device->FinishRenderingCurrentFrame();

    startup.get_future().wait();

    struct Asset
    {
        const char* Name;
        const char* Url;
    };

    std::array<Asset, 3> assets =
    {
        Asset{"BoomBox", "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Models/master/2.0/BoomBox/glTF/BoomBox.gltf"},
        Asset{"GlamVelvetSofa", "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Models/master/2.0/GlamVelvetSofa/glTF/GlamVelvetSofa.gltf"},
        Asset{"MaterialsVariantsShoe", "https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Models/master/2.0/MaterialsVariantsShoe/glTF/MaterialsVariantsShoe.gltf"},
    };

    for (const auto& asset : assets)
    {
        RenderDoc::StartFrameCapture(d3dDevice.get());

        device->StartRenderingCurrentFrame();
        deviceUpdate->Start();

        std::promise<void> loadAndRenderAsset{};

        loader.Dispatch([&loadAndRenderAsset, &asset](Napi::Env env)
        {
            std::cout << "Loading " << asset.Name << std::endl;

            auto jsPromise = env.Global().Get("loadAndRenderAssetAsync").As<Napi::Function>().Call(
            {
                Napi::String::From(env, asset.Name),
                Napi::String::From(env, asset.Url)
            }).As<Napi::Promise>();

            jsPromise.Get("then").As<Napi::Function>().Call(jsPromise,
            {
                Napi::Function::New(env, [&loadAndRenderAsset](const Napi::CallbackInfo&)
                {
                    loadAndRenderAsset.set_value();
                })
            });
        });

        loadAndRenderAsset.get_future().wait();

        deviceUpdate->Finish();
        device->FinishRenderingCurrentFrame();

        RenderDoc::StopFrameCapture(d3dDevice.get());

        auto filePath = GetModulePath() / asset.Name;
        filePath.concat(".png");
        std::cout << "Writing " << filePath.string() << std::endl;

        // See https://github.com/Microsoft/DirectXTK/wiki/ScreenGrab#srgb-vs-linear-color-space
        winrt::check_hresult(DirectX::SaveWICTextureToFile(context.get(), outputTexture.get(), GUID_ContainerFormatPng, filePath.c_str(), nullptr, nullptr, true));
    }

    runtime.reset();
    deviceUpdate.reset();
    device.reset();

    return 0;
}
