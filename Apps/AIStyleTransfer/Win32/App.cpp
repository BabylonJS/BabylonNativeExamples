#include <Babylon/AppRuntime.h>
#include <Babylon/Graphics/Device.h>
#include <Babylon/ScriptLoader.h>
#include <Babylon/Plugins/ExternalTexture.h>
#include <Babylon/Plugins/NativeEngine.h>
#include <Babylon/Polyfills/Console.h>
#include <Babylon/Polyfills/Window.h>
#include <Babylon/Polyfills/XMLHttpRequest.h>
#include <Babylon/Plugins/NativeInput.h>
#include <Babylon/Polyfills/Canvas.h>

#include <winrt/base.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.AI.MachineLearning.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <Windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <PathCch.h>
#include <Windows.h>
#include <Windowsx.h>
#include <Shlwapi.h>
#include <filesystem>
#include <stdio.h>
#include <wrl.h>
#include <dxgi1_2.h>
#include <filesystem>

#include "resource.h"

#include "Utility.h"
#include "SystemTime.h"

using namespace winrt::Windows::AI::MachineLearning;
using namespace winrt::Windows::Foundation::Collections;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using namespace winrt::Windows::Graphics::DirectX;

using namespace Microsoft::WRL;
using namespace Windows::Graphics::DirectX::Direct3D11;

#define MAX_LOADSTRING 100

namespace
{
	// Global variables
	constexpr const uint32_t WIDTH = 720;
	constexpr const uint32_t HEIGHT = 720;

	const std::vector<winrt::hstring> g_models = {
		L"\\Models\\candy.onnx", 
		L"\\Models\\la_muse.onnx", 
		L"\\Models\\mosaic.onnx", 
		L"\\Models\\udnie.onnx"
	};

	std::vector<LearningModelSession> g_sessions{};
	std::vector<LearningModelBinding> g_bindings{};

	int g_selectedModel = 0;

	// Global Variables:
	HINSTANCE hInst;                     // current instance
	WCHAR szTitle[MAX_LOADSTRING];       // The title bar text
	WCHAR szWindowClass[MAX_LOADSTRING]; // the main window class name

	std::unique_ptr<Babylon::Graphics::Device> g_device{};
	std::unique_ptr<Babylon::Graphics::DeviceUpdate> update{};
	Babylon::Plugins::NativeInput* nativeInput{};
	std::unique_ptr<Babylon::AppRuntime> runtime{};
	std::unique_ptr<Babylon::Polyfills::Canvas> nativeCanvas{};
	bool minimized{ false };

	winrt::com_ptr<IDXGISwapChain1>		g_SwapChain = nullptr;
	winrt::com_ptr<ID3D11Device>		g_d3dDevice = nullptr;
	winrt::com_ptr<ID3D11DeviceContext> g_d3dContext = nullptr;
	winrt::com_ptr<ID3D11Texture2D>		g_renderTexture = nullptr;

	std::filesystem::path GetModulePath()
	{
		WCHAR modulePath[4096];
		DWORD result = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
		winrt::check_bool(result != 0 && result != std::size(modulePath));
		return std::filesystem::path{ modulePath }.parent_path();
	}

	void InitializeGraphicsInfra(HWND g_coreWindow, IDirect3DDevice device)
	{
		winrt::com_ptr<IDirect3DDxgiInterfaceAccess> dxgiInterfaceAccess{ device.as<IDirect3DDxgiInterfaceAccess>() };
		winrt::com_ptr<IDXGIDevice2> dxgiDevice1;

		winrt::check_hresult(dxgiInterfaceAccess->GetInterface(__uuidof(dxgiDevice1), dxgiDevice1.put_void()));

		g_d3dDevice = dxgiDevice1.as<ID3D11Device>();

		g_d3dDevice->GetImmediateContext(g_d3dContext.put());

		// Allocate a descriptor.
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
		swapChainDesc.Width = WIDTH;                           // use automatic sizing
		swapChainDesc.Height = HEIGHT;
		swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // this is the most common swapchain format
		swapChainDesc.Stereo = false;

		// Use 4X MSAA? --must match swap chain MSAA values.
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;

		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT | DXGI_USAGE_UNORDERED_ACCESS;
		swapChainDesc.BufferCount = 2; // use double buffering to enable flip
		swapChainDesc.Scaling = DXGI_SCALING_NONE;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; // all apps must use this SwapEffect
		swapChainDesc.Flags = 0;

		// Identify the physical adapter (GPU or card) this device is runs on.
		winrt::com_ptr<IDXGIAdapter> dxgiAdapter;
		ASSERT_SUCCEEDED(dxgiDevice1->GetAdapter(dxgiAdapter.put()));

		// Get the factory object that created the DXGI device.
		winrt::com_ptr<IDXGIFactory2> dxgiFactory;
		ASSERT_SUCCEEDED(dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));

		// Get the final swap chain for this window from the DXGI factory.
		ASSERT_SUCCEEDED(dxgiFactory->CreateSwapChainForHwnd(g_d3dDevice.get(), g_coreWindow, &swapChainDesc, nullptr, nullptr, g_SwapChain.put()));

		// Ensure that DXGI doesn't queue more than one frame at a time.
		ASSERT_SUCCEEDED(dxgiDevice1->SetMaximumFrameLatency(1));

		Microsoft::WRL::ComPtr<IDXGISurface1> dxgiBuffer;
		ASSERT_SUCCEEDED(g_SwapChain->GetBuffer(0, __uuidof(IDXGISurface1), &dxgiBuffer));

		ASSERT_SUCCEEDED(dxgiBuffer->QueryInterface(__uuidof(ID3D11Texture2D), g_renderTexture.put_void()));
	}

	std::unique_ptr<Babylon::Graphics::Device> CreateGraphicsDevice(ID3D11Device* d3dDevice)
	{
		Babylon::Graphics::DeviceConfiguration config{ d3dDevice };
		std::unique_ptr<Babylon::Graphics::Device> device = Babylon::Graphics::Device::Create(config);
		device->UpdateSize(WIDTH, HEIGHT);
		return device;
	}

	winrt::hstring GetInstalledLocation()
	{
		WCHAR modulePath[ 4096 ];
		DWORD result { ::GetModuleFileNameW( nullptr, modulePath, ARRAYSIZE(modulePath))};
		winrt::check_bool( result != 0 && result != std::size( modulePath ) );
		winrt::check_hresult(PathCchRemoveFileSpec(modulePath, ARRAYSIZE( modulePath)));
		return modulePath;
	}

	// Machine learning
	LearningModel LoadModel(winrt::hstring modelPath)
	{
		auto installPath = GetInstalledLocation();
		auto finalPath = installPath + modelPath;

		// load the model
		Utility::Printf( "Loading modelfile '%ws'\n", finalPath.c_str() );

		auto ticks = SystemTime::GetCurrentTick();
		auto model = LearningModel::LoadFromFilePath( finalPath.c_str());
		ticks = SystemTime::GetCurrentTick() - ticks;
		Utility::Printf("model file loaded in %f ms\n", SystemTime::TicksToMillisecs(ticks));

		return model;
	}

	LearningModelDevice CreateLearnDevice()
	{
		return LearningModelDevice(LearningModelDeviceKind::DirectXHighPerformance);
	}

	LearningModelSession CreateModelSession(LearningModel model, LearningModelDevice device)
	{
		return LearningModelSession(model, device);
	}

	LearningModelBinding BindMLModel(LearningModelSession session)
	{
		Utility::Printf("Binding the model...\n");
		auto ticks = SystemTime::GetCurrentTick();

		auto outputFrame = VideoFrame::CreateAsDirect3D11SurfaceBacked(DirectXPixelFormat::B8G8R8A8UIntNormalized, WIDTH, HEIGHT, session.Device().Direct3D11Device());

		// now create a session and binding
		auto binding = LearningModelBinding{ session };
		binding.Bind(L"outputImage", outputFrame);

		ticks = SystemTime::GetCurrentTick() - ticks;
		Utility::Printf("Model bound in %f ms\n", SystemTime::TicksToMillisecs(ticks));

		return binding;
	}

	VideoFrame RunModel(LearningModelSession session, LearningModelBinding binding, VideoFrame inputFrame)
	{
		// now run the model
		auto ticks = SystemTime::GetCurrentTick();

		// bind the intput image
		binding.Bind(L"inputImage", inputFrame);

		auto results = session.Evaluate(binding, L"RunId");

		VideoFrame evalOutput = results.Outputs().Lookup(L"outputImage").try_as<VideoFrame>();

		ticks = SystemTime::GetCurrentTick() - ticks;
		Utility::Printf("Model run took %f ms \n", SystemTime::TicksToMillisecs(ticks));
		return evalOutput;
	}

	void Uninitialize()
	{
		if (g_device)
		{
			update->Finish();
			g_device->FinishRenderingCurrentFrame();
		}

		nativeInput = {};
		runtime.reset();
		nativeCanvas.reset();
		update.reset();
		g_device.reset();
	}

	void CopyTo(VideoFrame src, VideoFrame dst)
	{
		auto srcD3DSurface = src.Direct3DSurface().as<IDirect3DDxgiInterfaceAccess>();
		auto dstD3DSurface = dst.Direct3DSurface().as<IDirect3DDxgiInterfaceAccess>();

		winrt::com_ptr<ID3D11Texture2D> srcTexture;
		srcD3DSurface->GetInterface(IID_PPV_ARGS(&srcTexture));

		winrt::com_ptr<ID3D11Texture2D> dstTexture;
		dstD3DSurface->GetInterface(IID_PPV_ARGS(&dstTexture));

		g_d3dContext->CopyResource(dstTexture.get(), srcTexture.get());
	}

	void CopyTo(ID3D11Texture2D* src, VideoFrame dst)
	{
		winrt::com_ptr<ID3D11Texture2D> dstTexture;
		dst.Direct3DSurface().as<IDirect3DDxgiInterfaceAccess>()->GetInterface(IID_PPV_ARGS(&dstTexture));

		g_d3dContext->CopyResource(dstTexture.get(), src);
	}

	void CopyTo(VideoFrame src, ID3D11Texture2D* dst)
	{
		winrt::com_ptr<ID3D11Texture2D> srcTexture;
		src.Direct3DSurface().as<IDirect3DDxgiInterfaceAccess>()->GetInterface(IID_PPV_ARGS(&srcTexture));

		g_d3dContext->CopyResource(dst, srcTexture.get());
	}
}

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nCmdShow)
{
	winrt::init_apartment();

	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// Initialize global strings
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_PLAYGROUNDWIN32, szWindowClass, MAX_LOADSTRING);

	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PLAYGROUNDWIN32));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_PLAYGROUNDWIN32);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	RegisterClassExW(&wcex);

	RECT rc = { 0, 0, WIDTH, HEIGHT };
	AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

	HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance, nullptr);

	if (!hWnd)
	{
		return FALSE;
	}

	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);
	EnableMouseInPointer(true);

	SystemTime::Initialize();

	auto learnDevice = CreateLearnDevice();

	InitializeGraphicsInfra(hWnd, learnDevice.Direct3D11Device());

	// Create the Babylon Native graphics device and update.
	g_device = CreateGraphicsDevice( g_d3dDevice.get() );
	update = std::make_unique<Babylon::Graphics::DeviceUpdate>( g_device->GetUpdate( "update" ) );

	auto inputFrame = VideoFrame::CreateAsDirect3D11SurfaceBacked( DirectXPixelFormat::B8G8R8A8UIntNormalized, WIDTH, HEIGHT, learnDevice.Direct3D11Device() );

	//Machien Learning initalization
	for( size_t i = 0; i < g_models.size(); i++ )
	{
		LearningModel model = LoadModel(g_models[ i ]);
		auto session = CreateModelSession( model, learnDevice );
		auto binding = BindMLModel(session);
		g_sessions.push_back(session);
		g_bindings.push_back(binding);
	}

	// Start rendering a frame to unblock the JavaScript from queuing graphics
	// commands.
	g_device->StartRenderingCurrentFrame();
	update->Start();

	// Create a Babylon Native application runtime which hosts a JavaScript
	// engine on a new thread.
	runtime = std::make_unique<Babylon::AppRuntime>();

	runtime->Dispatch([](Napi::Env env)
		{
			// Add the Babylon Native graphics device to the JavaScript environment.
			g_device->AddToJavaScript(env);

			// Initialize the console polyfill.
			Babylon::Polyfills::Console::Initialize(env, [](const char* message, auto)
				{
					Utility::Print(message);
				});

			// Initialize the window, XMLHttpRequest, and NativeEngine polyfills.
			Babylon::Polyfills::Window::Initialize(env);
			Babylon::Polyfills::XMLHttpRequest::Initialize(env);

			nativeCanvas = std::make_unique<Babylon::Polyfills::Canvas>(Babylon::Polyfills::Canvas::Initialize(env));

			Babylon::Plugins::NativeEngine::Initialize(env);
			nativeInput = &Babylon::Plugins::NativeInput::CreateForJavaScript(env);
		});

	// Load the scripts for Babylon.js core and loaders plus this app's index.js.
	Babylon::ScriptLoader loader{ *runtime };
	loader.LoadScript("app:///Scripts/babylon.max.js");
	loader.LoadScript("app:///Scripts/babylonjs.loaders.js");
	loader.LoadScript("app:///Scripts/index.js");

	std::promise<void> addToContext{};
	std::promise<void> startup{};

	// Create an external texture for the render target texture and pass it to
	// the `startup` JavaScript function.
	loader.Dispatch([externalTexture = Babylon::Plugins::ExternalTexture{ g_renderTexture.get() }, &addToContext, &startup](Napi::Env env)
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
	update->Finish();
	g_device->FinishRenderingCurrentFrame();

	// Wait for `startup` to finish.
	startup.get_future().wait();

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_PLAYGROUNDWIN32));

	MSG msg{};

	g_device->StartRenderingCurrentFrame();
	update->Start();

	auto previewsTick = SystemTime::GetCurrentTick();

	// Main message loop:
	while (msg.message != WM_QUIT)
	{
		BOOL result;

		if (minimized)
		{
			result = GetMessage(&msg, nullptr, 0, 0);
		}
		else
		{
			if (g_device)
			{
				auto currentTick = SystemTime::GetCurrentTick();
				auto deltaT = SystemTime::TicksToSeconds(currentTick - previewsTick);
				auto fps = 1.0 / deltaT;
				previewsTick = currentTick;

				update->Finish();
				g_device->FinishRenderingCurrentFrame();

				Utility::Printf("FPS %f \n", fps);

				if (g_selectedModel >= 0)
				{
					CopyTo(g_renderTexture.get(), inputFrame);
					VideoFrame result = RunModel(g_sessions[g_selectedModel], g_bindings[g_selectedModel], inputFrame);
					CopyTo(result, g_renderTexture.get());
				}

				ASSERT_SUCCEEDED(g_SwapChain->Present(1, 0));
				g_device->StartRenderingCurrentFrame();
				update->Start();
			}

			result = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE) && msg.message != WM_QUIT;
		}

		if (result)
		{
			if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	return (int)msg.wParam;
}

void ProcessMouseButtons(tagPOINTER_BUTTON_CHANGE_TYPE changeType, int x, int y)
{
	switch (changeType)
	{
	case POINTER_CHANGE_FIRSTBUTTON_DOWN:
		nativeInput->MouseDown(Babylon::Plugins::NativeInput::LEFT_MOUSE_BUTTON_ID, x, y);
		break;
	case POINTER_CHANGE_FIRSTBUTTON_UP:
		nativeInput->MouseUp(Babylon::Plugins::NativeInput::LEFT_MOUSE_BUTTON_ID, x, y);
		break;
	case POINTER_CHANGE_SECONDBUTTON_DOWN:
		nativeInput->MouseDown(Babylon::Plugins::NativeInput::RIGHT_MOUSE_BUTTON_ID, x, y);
		break;
	case POINTER_CHANGE_SECONDBUTTON_UP:
		nativeInput->MouseUp(Babylon::Plugins::NativeInput::RIGHT_MOUSE_BUTTON_ID, x, y);
		break;
	case POINTER_CHANGE_THIRDBUTTON_DOWN:
		nativeInput->MouseDown(Babylon::Plugins::NativeInput::MIDDLE_MOUSE_BUTTON_ID, x, y);
		break;
	case POINTER_CHANGE_THIRDBUTTON_UP:
		nativeInput->MouseUp(Babylon::Plugins::NativeInput::MIDDLE_MOUSE_BUTTON_ID, x, y);
		break;
	}
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_SYSCOMMAND:
	{
		if ((wParam & 0xFFF0) == SC_MINIMIZE)
		{
			if (g_device)
			{
				update->Finish();
				g_device->FinishRenderingCurrentFrame();
			}

			runtime->Suspend();

			minimized = true;
		}
		else if ((wParam & 0xFFF0) == SC_RESTORE)
		{
			if (minimized)
			{
				runtime->Resume();

				minimized = false;

				if (g_device)
				{
					g_device->StartRenderingCurrentFrame();
					update->Start();
				}
			}
		}
		DefWindowProc(hWnd, message, wParam, lParam);
		break;
	}
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	}
	case WM_DESTROY:
	{
		Uninitialize();
		PostQuitMessage(0);
		break;
	}
	case WM_KEYDOWN:
	{
		if (wParam == 'R')
		{
			g_selectedModel = g_selectedModel < 3 ? ( g_selectedModel + 1):  -1;
		}
		break;
	}
	case WM_POINTERWHEEL:
	{
		if (nativeInput != nullptr)
		{
			nativeInput->MouseWheel(Babylon::Plugins::NativeInput::MOUSEWHEEL_Y_ID, -GET_WHEEL_DELTA_WPARAM(wParam));
		}
		break;
	}
	case WM_POINTERDOWN:
	{
		if (nativeInput != nullptr)
		{
			POINTER_INFO info;
			auto pointerId = GET_POINTERID_WPARAM(wParam);

			if (GetPointerInfo(pointerId, &info))
			{
				auto x = GET_X_LPARAM(lParam);
				auto y = GET_Y_LPARAM(lParam);

				if (info.pointerType == PT_MOUSE)
				{
					ProcessMouseButtons(info.ButtonChangeType, x, y);
				}
				else
				{
					nativeInput->TouchDown(pointerId, x, y);
				}
			}
		}
		break;
	}
	case WM_POINTERUPDATE:
	{
		if (nativeInput != nullptr)
		{
			POINTER_INFO info;
			auto pointerId = GET_POINTERID_WPARAM(wParam);

			if (GetPointerInfo(pointerId, &info))
			{
				auto x = GET_X_LPARAM(lParam);
				auto y = GET_Y_LPARAM(lParam);

				if (info.pointerType == PT_MOUSE)
				{
					ProcessMouseButtons(info.ButtonChangeType, x, y);
					nativeInput->MouseMove(x, y);
				}
				else
				{
					nativeInput->TouchMove(pointerId, x, y);
				}
			}
		}
		break;
	}
	case WM_POINTERUP:
	{
		if (nativeInput != nullptr)
		{
			POINTER_INFO info;
			auto pointerId = GET_POINTERID_WPARAM(wParam);

			if (GetPointerInfo(pointerId, &info))
			{
				auto x = GET_X_LPARAM(lParam);
				auto y = GET_Y_LPARAM(lParam);

				if (info.pointerType == PT_MOUSE)
				{
					ProcessMouseButtons(info.ButtonChangeType, x, y);
				}
				else
				{
					nativeInput->TouchUp(pointerId, x, y);
				}
			}
		}
		break;
	}
	default:
	{
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	}
	return 0;
}