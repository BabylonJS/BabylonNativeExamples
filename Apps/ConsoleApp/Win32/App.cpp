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
#include "SystemTime.h"

#include <winrt/Windows.AI.MachineLearning.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.h>
#include <Windows.graphics.directx.direct3d11.interop.h>

using namespace winrt::Windows::AI::MachineLearning;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Storage;

namespace
{
    // Global variables  
    constexpr const uint32_t WIDTH = 720;
    constexpr const uint32_t HEIGHT = 720;

    LearningModelSession session = nullptr;
    LearningModelBinding binding = nullptr;

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
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc = {1, 0};
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
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

    // Machine learning 
    LearningModel LoadModel(winrt::hstring  modelPath)
    {
        // load the model
        printf( "Loading modelfile '%ws'\n", modelPath.c_str());
        auto ticks = SystemTime::GetCurrentTick();
        auto model = LearningModel::LoadFromFilePath(modelPath);
        ticks = SystemTime::GetCurrentTick() - ticks;
        printf( "model file loaded in %f ms\n", SystemTime::TicksToMillisecs(ticks));

        // Debugging logic to see the input and output of ther model and retrieve dimensions of input/output variables
        // ### DEBUG ###
        static std::vector<std::string> kind_values { "Tensor", "Sequence", "Map", "Image" };

        static std::map<int, std::string> channel_values {
            {0,   "Unknown"},
            {12,  "Rgba16"},
            {30,  "Rgba8"},
            {57,  "Gray16"},
            {62,  "Gray8"},
            {87,  "Bgra8"},
            {103, "Nv12"},
            {104, "P010"},
            {107, "Yuy2"}
        };

        for( auto inputF : model.InputFeatures() )
        {
            auto kind = static_cast< int >(inputF.Kind());
            auto name = inputF.Name();

            printf( "input | kind:'%s', name:'%ws' \n", kind_values[ kind ].c_str(), name.c_str() );

            IImageFeatureDescriptor imgDesc = inputF.try_as<IImageFeatureDescriptor>();
            ITensorFeatureDescriptor tfDesc = inputF.try_as<ITensorFeatureDescriptor>();

            auto n = ( int ) ( imgDesc == nullptr ? tfDesc.Shape().GetAt( 0 ) : 1 );
            auto width = ( int ) ( imgDesc == nullptr ? tfDesc.Shape().GetAt( 3 ) : imgDesc.Width() );
            auto height = ( int ) ( imgDesc == nullptr ? tfDesc.Shape().GetAt( 2 ) : imgDesc.Height() );
            auto channel = ( imgDesc == nullptr ? tfDesc.Shape().GetAt( 1 ) : static_cast< int >( imgDesc.BitmapPixelFormat() ) );

            printf( "N: %i, Width:%i, Height:%i, Channel: '%s' \n", n, width, height, channel_values[ channel ].c_str() );
        }

        for( auto outputF : model.OutputFeatures() )
        {
            auto kind = static_cast< int >( outputF.Kind() );
            auto name = outputF.Name();

            printf( "output | kind:'%s', name:'%ws' \n", kind_values[ kind ].c_str(), name.c_str() );

            IImageFeatureDescriptor imgDesc = outputF.try_as<IImageFeatureDescriptor>();
            ITensorFeatureDescriptor tfDesc = outputF.try_as<ITensorFeatureDescriptor>();

            auto n = ( int ) ( imgDesc == nullptr ? tfDesc.Shape().GetAt( 0 ) : 1 );
            auto width = ( int ) ( imgDesc == nullptr ? tfDesc.Shape().GetAt( 3 ) : imgDesc.Width() );
            auto height = ( int ) ( imgDesc == nullptr ? tfDesc.Shape().GetAt( 2 ) : imgDesc.Height() );
            auto channel = ( imgDesc == nullptr ? tfDesc.Shape().GetAt( 1 ) : static_cast< int >( imgDesc.BitmapPixelFormat() ) );

            printf( "N: %i, Width:%i, Height:%i, Channel: '%s' \n", n, width, height, channel_values[ channel ].c_str() );
        }

        return model;
    }

    LearningModelSession CreateModelSession(LearningModel model)
    {
        auto session = LearningModelSession { model, LearningModelDevice(LearningModelDeviceKind::DirectXHighPerformance) };
        return session;
    }

    LearningModelBinding BindModel(LearningModelSession session, VideoFrame inputFrame, VideoFrame outputFrame )
    {
        printf( "Binding the model...\n" );
        auto ticks = SystemTime::GetCurrentTick();

        // now create a session and binding
        binding = LearningModelBinding { session };       

        binding.Bind( L"outputImage", outputFrame );

        ticks = SystemTime::GetCurrentTick() - ticks;
        printf( "Model bound in %f ms\n", SystemTime::TicksToMillisecs(ticks) );

        return binding;
    }

    VideoFrame RunModel(LearningModelSession session, LearningModelBinding binding, VideoFrame inputFrame)
    {
        // now run the model
        printf("Running the model...\n" );
        auto ticks = SystemTime::GetCurrentTick();
        // bind the intput image
        binding.Bind(L"inputImage", inputFrame);

        auto results = session.Evaluate( binding, L"RunId" );
        VideoFrame evalOutput = results.Outputs().Lookup(L"outputImage").try_as<VideoFrame>();

        ticks = SystemTime::GetCurrentTick() - ticks;
        printf( "model run took %f ms\n", SystemTime::TicksToMillisecs(ticks));
        return evalOutput;
    }
}

int main()
{
    SystemTime::Initialize();

    // Initialize RenderDoc.
    RenderDoc::Init();

    // Create a DirectX device.
    auto d3dDevice = CreateD3DDevice();

    // Get the immediate context for DirectXTK when saving the texture.
    winrt::com_ptr<ID3D11DeviceContext> d3dDeviceContext;
    d3dDevice->GetImmediateContext(d3dDeviceContext.put());

    // Create the Babylon Native graphics device and update.
    auto device = CreateGraphicsDevice(d3dDevice.get());
    auto deviceUpdate = std::make_unique<Babylon::Graphics::DeviceUpdate>(device->GetUpdate("update"));

    winrt::hstring modelPath = L"D:\\Github\\Windows-Machine-Learning\\SharedContent\\models\\candy.onnx";

    //Machien Learning initalization
    LearningModel model = LoadModel(modelPath);
    auto session = CreateModelSession(model);

    VideoFrame::CreateAsDirect3D11SurfaceBacked(winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8Typeless, WIDTH, HEIGHT);

    auto outputFrame = VideoFrame(winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8, WIDTH, HEIGHT);
    auto inputFrame = VideoFrame(winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8, WIDTH, HEIGHT);

    auto binding = BindModel(session, inputFrame, outputFrame);

    // Start rendering a frame to unblock the JavaScript from queuing graphics
    // commands.
    device->StartRenderingCurrentFrame();
    deviceUpdate->Start();

    // Create a Babylon Native application runtime which hosts a JavaScript
    // engine on a new thread.
    auto runtime = std::make_unique<Babylon::AppRuntime>();

    runtime->Dispatch([&device](Napi::Env env)
    {
        // Add the Babylon Native graphics device to the JavaScript environment.
        device->AddToJavaScript(env);

        // Initialize the console polyfill.
        Babylon::Polyfills::Console::Initialize(env, [](const char* message, auto)
        {
            std::cout << message;
        });

        // Initialize the window, XMLHttpRequest, and NativeEngine polyfills.
        Babylon::Polyfills::Window::Initialize(env);
        Babylon::Polyfills::XMLHttpRequest::Initialize(env);
        Babylon::Plugins::NativeEngine::Initialize(env);
    });

    // Load the scripts for Babylon.js core and loaders plus this app's index.js.
    Babylon::ScriptLoader loader{*runtime};
    loader.LoadScript("app:///Scripts/babylon.max.js");
    loader.LoadScript("app:///Scripts/babylonjs.loaders.js");
    loader.LoadScript("app:///Scripts/index.js");

    // Create a render target texture for the output.
    winrt::com_ptr<ID3D11Texture2D> outputTexture = CreateD3DRenderTargetTexture(d3dDevice.get());

    std::promise<void> addToContext{};
    std::promise<void> startup{};

    // Create an external texture for the render target texture and pass it to
    // the `startup` JavaScript function.
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

    // Wait for `AddToContextAsync` to be called.
    addToContext.get_future().wait();

    // Render a frame so that `AddToContextAsync` will complete.
    deviceUpdate->Finish();
    device->FinishRenderingCurrentFrame();

    // Wait for `startup` to finish.
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
        // Tell RenderDoc to start capturing.
        RenderDoc::StartFrameCapture(d3dDevice.get());

        // Start rendering a frame to unblock the JavaScript again.
        device->StartRenderingCurrentFrame();
        deviceUpdate->Start();

        std::promise<void> loadAndRenderAsset{};

        // Call `loadAndRenderAssetAsync` with the asset URL.
        loader.Dispatch([&loadAndRenderAsset, &asset](Napi::Env env)
        {
            std::cout << "Loading " << asset.Name << std::endl;

            auto jsPromise = env.Global().Get("loadAndRenderAssetAsync").As<Napi::Function>().Call({
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

        // Wait for the function to complete.
        loadAndRenderAsset.get_future().wait();

        // Finish rendering the frame.
        deviceUpdate->Finish();
        device->FinishRenderingCurrentFrame();

        // Tell RenderDoc to stop capturing.
        RenderDoc::StopFrameCapture(d3dDevice.get());

        // Save the texture into an PNG next to the executable.
        auto filePath = GetModulePath() / asset.Name;
        filePath.concat(".png");
        std::cout << "Writing original to: " << filePath.string() << std::endl;

        auto modifiedFilePath = GetModulePath() / asset.Name;
        modifiedFilePath.concat("_styled").concat(".png");
        std::cout << "Writing styled to: " << modifiedFilePath.string() << std::endl;

        // See https://github.com/Microsoft/DirectXTK/wiki/ScreenGrab#srgb-vs-linear-color-space
        winrt::check_hresult(DirectX::SaveWICTextureToFile( d3dDeviceContext.get(), outputTexture.get(), GUID_ContainerFormatPng, filePath.c_str(), nullptr, nullptr, true ) );

        //Filter applyed
        IDXGISurface* surface = nullptr;
        winrt::check_hresult(outputTexture->QueryInterface(__uuidof( IDXGISurface ), (void**) &surface));

        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface d3dSurface2 {nullptr};

        winrt::check_hresult(CreateDirect3D11SurfaceFromDXGISurface(surface, reinterpret_cast<::IInspectable**>(winrt::put_abi(d3dSurface2))));

        auto videoFrame = VideoFrame::CreateWithDirect3D11Surface(d3dSurface2);

        videoFrame.CopyToAsync(inputFrame).get();

        auto result = RunModel(session, binding, inputFrame);

        result.CopyToAsync(videoFrame).get();

        // See https://github.com/Microsoft/DirectXTK/wiki/ScreenGrab#srgb-vs-linear-color-space
        winrt::check_hresult(DirectX::SaveWICTextureToFile(d3dDeviceContext.get(), outputTexture.get(), GUID_ContainerFormatPng, modifiedFilePath.c_str(), nullptr, nullptr, true));
    }

    // Reset the application runtime, then graphics device update, then graphics device, in that order.
    runtime.reset();
    deviceUpdate.reset();
    device.reset();

    return 0;
}
