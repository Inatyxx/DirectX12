#include "pch.h"
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#include <d3d12.h>
#include "d3dx12.h"
#include <windowsX.h>
#include <dxgidebug.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

void Resize();

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ------
// Global 
// ------

ID3D12Debug* d3d12Debug = nullptr;
IDXGIDebug1* dxgiDebug = nullptr;

// HRESULT Debug
HRESULT result;

//fence
ID3D12Fence* fence = nullptr;

// WINDOW Caracteristics
HWND hMainWnd = nullptr; // main window handle
// name of the window (not the title)
LPCTSTR WindowName = L"DirectX12";
// title of the window
LPCTSTR WindowTitle = L"Youpii";

ID3D12Device* device = nullptr;
IDXGISwapChain* swapChain = nullptr;

// Command List, Allocator et Queue
ID3D12GraphicsCommandList* commandList = nullptr;
ID3D12CommandAllocator* commandAllocator = nullptr;
ID3D12CommandQueue* commandQueue = nullptr;

static const int SwapChainBufferCount = 2;
ID3D12Resource* mSwapChainBuffer[SwapChainBufferCount];
UINT64 mCurrentFence = 0;

ID3D12Resource* depthStencilBuffer = nullptr;
int mCurrBackBuffer = 0;

// Creation de Descriptor Heaps
ID3D12DescriptorHeap* RTVDescriptorHeap = nullptr; // Render Target View
ID3D12DescriptorHeap* DSVDescriptorHeap = nullptr; // Depth Stencil View

D3D12_DESCRIPTOR_HEAP_DESC RTVDescriptorHeapDesc;
D3D12_DESCRIPTOR_HEAP_DESC DSVDescriptorHeapDesc;

// Descriptor Size
UINT mRtvDescriptorSize;
UINT mDsvDescriptorSize;
UINT mCbvSrvDescriptorSize;

bool canUseMSAA = false;
UINT m4xMsaaQuality = 0; // Qualite du MSAA utilise sur l'appareil

D3D12_VIEWPORT viewPort;
D3D12_RECT mScissorRect = { 0, 0, 800 / 2, 600 / 2 };

bool FullScreen = false;


void Init()
{
    // Debug directX
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d3d12Debug))))
    {
        d3d12Debug->EnableDebugLayer();
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiDebug))))
        {
            dxgiDebug->EnableLeakTrackingForThread();
        }
    }

    // Creation du device
    result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device));
    assert(result == S_OK);

    // Creation d'une fence
    result = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    assert(result == S_OK);

    // Check si l'appareil est capable d'utiliser 4XMSAA
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
    msQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // RGBA 8 bits
    msQualityLevels.SampleCount = 4;
    msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    msQualityLevels.NumQualityLevels = 4;
    m4xMsaaQuality = msQualityLevels.NumQualityLevels;
    assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    result = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    assert(result == S_OK);
    result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    assert(result == S_OK);
    result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
    assert(result == S_OK);
    commandList->Close();

    // Creation de la Swap Chain
    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferDesc.Width = 800;
    swapChainDesc.BufferDesc.Height = 600;
    swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
    swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    swapChainDesc.SampleDesc.Count = canUseMSAA ? 4 : 1;
    swapChainDesc.SampleDesc.Quality = canUseMSAA ? (m4xMsaaQuality - 1) : 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = SwapChainBufferCount;
    swapChainDesc.OutputWindow = hMainWnd;
    swapChainDesc.Windowed = true;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    IDXGIFactory4* factory = nullptr;
    result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    assert(result == S_OK);
    result = factory->CreateSwapChain(commandQueue, &swapChainDesc, &swapChain);
    assert(result == S_OK);

    // Creation des descriptor sizes
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    RTVDescriptorHeapDesc.NumDescriptors = SwapChainBufferCount;
    RTVDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    RTVDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    RTVDescriptorHeapDesc.NodeMask = 0;
    result = device->CreateDescriptorHeap(&RTVDescriptorHeapDesc, IID_PPV_ARGS(&RTVDescriptorHeap));
    assert(result == S_OK);

    DSVDescriptorHeapDesc.NumDescriptors = 1;
    DSVDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    DSVDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    DSVDescriptorHeapDesc.NodeMask = 0;
    result = device->CreateDescriptorHeap(&DSVDescriptorHeapDesc, IID_PPV_ARGS(&DSVDescriptorHeap));
    assert(result == S_OK);

    // Initialize the render target views (RTVs)
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SwapChainBufferCount; i++)
    {
        result = swapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i]));
        assert(result == S_OK);
        device->CreateRenderTargetView(mSwapChainBuffer[i], nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(1, mRtvDescriptorSize);
    }

    // Depth/Stencil Buffer View
    D3D12_RESOURCE_DESC depthStencilDesc = {};
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = 800;
    depthStencilDesc.Height = 600;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = canUseMSAA ? 4 : 1;
    depthStencilDesc.SampleDesc.Quality = canUseMSAA ? (m4xMsaaQuality - 1) : 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear = {};
    optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;

    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    result = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &depthStencilDesc, D3D12_RESOURCE_STATE_COMMON, &optClear, IID_PPV_ARGS(&depthStencilBuffer));
    assert(result == S_OK);
    device->CreateDepthStencilView(depthStencilBuffer, nullptr, DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    // Transition the resource from its initial state to be used as a depth buffer.
    CD3DX12_RESOURCE_BARRIER rscTransition = CD3DX12_RESOURCE_BARRIER::Transition(depthStencilBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    commandList->ResourceBarrier(1, &rscTransition);

    // Performance Timer
    double mSecondsPerCount = 0.0;
    double mDeltaTime = -1.0;
    __int64 mBaseTime = 0;
    __int64 mPausedTime = 0;
    __int64 mStopTime = 0;
    __int64 mPrevTime = 0;
    __int64 mCurrTime = 0;
    bool mStopped = false;

    __int64 countsPerSec;
    QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
    mSecondsPerCount = 1.0 / (double)countsPerSec;

    bool mAppPaused = false;

    MSG msg = { 0 };
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            if (mStopped)
            {
                mDeltaTime = 0.0;
                return;
            }

            __int64 currTime;
            QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
            mCurrTime = currTime;
            mDeltaTime = (mCurrTime - mPrevTime) * mSecondsPerCount;
            mPrevTime = mCurrTime;
            if (mDeltaTime < 0.0)
            {
                mDeltaTime = 0.0;
            }

            float totalTime = 0;

            if (!mAppPaused)
            {
                static int frameCnt = 0;
                static float timeElapsed = 0.0f;

                frameCnt++;

                if (mStopped)
                {
                    totalTime = (float)(((mStopTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
                }
                else
                {
                    totalTime = (float)(((mCurrTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
                }

                if ((totalTime - timeElapsed) >= 1.0f)
                {
                    float fps = (float)frameCnt;
                    float mspf = 1000.0f / fps;

                    std::wstring fpsStr = std::to_wstring(fps);
                    std::wstring mspfStr = std::to_wstring(mspf);
                    std::wstring mMainWndCaption = L"d3d App";

                    std::wstring windowText = mMainWndCaption +
                        L"    fps: " + fpsStr +
                        L"   mspf: " + mspfStr;
                    SetWindowText(hMainWnd, windowText.c_str());

                    frameCnt = 0;
                    timeElapsed += 1.0f;
                }

                result = commandAllocator->Reset();
                assert(result == S_OK);

                result = commandList->Reset(commandAllocator, nullptr);
                assert(result == S_OK);

                rscTransition = CD3DX12_RESOURCE_BARRIER::Transition(mSwapChainBuffer[mCurrBackBuffer], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
                commandList->ResourceBarrier(1, &rscTransition);

                commandList->RSSetViewports(1, &viewPort);
                commandList->RSSetScissorRects(1, &mScissorRect);

                CD3DX12_CPU_DESCRIPTOR_HANDLE RTVDepthCPUHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mCurrBackBuffer, mRtvDescriptorSize);
                commandList->ClearRenderTargetView(RTVDepthCPUHandle, Colors::LightSteelBlue, 0, nullptr);
                commandList->ClearDepthStencilView(DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

                CD3DX12_CPU_DESCRIPTOR_HANDLE RTVHandleDesc(RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), mCurrBackBuffer, mRtvDescriptorSize);
                CD3DX12_CPU_DESCRIPTOR_HANDLE DSVHandleDesc(DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

                commandList->OMSetRenderTargets(1, &RTVHandleDesc, true, &DSVHandleDesc);

                rscTransition = CD3DX12_RESOURCE_BARRIER::Transition(mSwapChainBuffer[mCurrBackBuffer], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
                commandList->ResourceBarrier(1, &rscTransition);

                result = commandList->Close();
                assert(result == S_OK);

                ID3D12CommandList* cmdsLists[] = { commandList };
                commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

                result = swapChain->Present(0, 0);
                assert(result == S_OK);
                mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

                mCurrentFence++;
                result = commandQueue->Signal(fence, mCurrentFence);
                assert(result == S_OK);

                if (fence->GetCompletedValue() < mCurrentFence)
                {
                    HANDLE eventHandle = CreateEventEx(nullptr, 0, false, EVENT_ALL_ACCESS);
                    result = fence->SetEventOnCompletion(mCurrentFence, eventHandle);
                    assert(result == S_OK);
                    WaitForSingleObject(eventHandle, INFINITE);
                    CloseHandle(eventHandle);
                }
                else
                {
                    Sleep(100);
                }
            }
            int param = (int)msg.wParam;
        }
    }
}

bool InitializeWindow(HINSTANCE hInstance, int ShowWnd, int width, int height, bool fullscreen)
{
	if (fullscreen)
	{
		HMONITOR hmon = MonitorFromWindow(hMainWnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		GetMonitorInfo(hmon, &mi);

		width = mi.rcMonitor.right - mi.rcMonitor.left;
		height = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}

	// Creation de l'instance de l'application
	HINSTANCE mhAppInst = GetModuleHandle(nullptr);
	if (mhAppInst == nullptr)
	{
		std::cerr << "Erreur : l'instance de l'application n'a pas ?t? cr??e" << std::endl;
		return false;
	}

	WNDCLASS wc;

	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = NULL;
	wc.cbWndExtra = NULL;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = WindowName;

	if (!RegisterClass(&wc))
	{
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return false;
	}

	// Creation de la fen?tre
	RECT R = { 0, 0, 800, 600 };
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int rWidth = R.right - R.left;
	int rHeight = R.bottom - R.top;

	hMainWnd = CreateWindowEx(NULL, WindowName, WindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, hInstance, NULL);
	if (!hMainWnd)
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return false;
	}

	if (fullscreen)
	{
		SetWindowLong(hMainWnd, GWL_STYLE, 0);
	}

	ShowWindow(hMainWnd, ShowWnd);
	UpdateWindow(hMainWnd);

	Init(); // DirectX12 Init

	return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			if (MessageBox(0, L"Are you sure you want to exit?",
				L"Really?", MB_YESNO | MB_ICONQUESTION) == IDYES)
				DestroyWindow(hwnd);
		}
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd,
		msg,
		wParam,
		lParam);
}

void mainloop() {
	MSG msg;
	ZeroMemory(&msg, sizeof(MSG));

	while (true)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			// run game code
		}
	}
}

//

int WINAPI main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	// create the window
	if (!InitializeWindow(hInstance, nShowCmd, 800, 600, false))
	{
		MessageBox(0, L"Window Initialization - Failed",
			L"Error", MB_OK);
		return 0;
	}

	// start the main loop
	mainloop();

	return 0;
}

void Resize()
{
	assert(device);
	assert(swapChain);
	assert(commandAllocator);

	// Flush before changing any resources.
	// Advance the fence value to mark commands up to this fence point.
	mCurrentFence++;

	// Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	commandQueue->Signal(fence, mCurrentFence);

	// Wait until the GPU has completed commands up to this fence point.
	if (fence->GetCompletedValue() < mCurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, (LPCWSTR)false, false, EVENT_ALL_ACCESS);

		// Fire event when GPU hits current fence.  
		fence->SetEventOnCompletion(mCurrentFence, eventHandle);

		// Wait until the GPU hits current fence event is fired.
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	commandList->Reset(commandAllocator, nullptr);

	// Release the previous resources we will be recreating.
	for (int i = 0; i < SwapChainBufferCount; ++i)
	{

		mSwapChainBuffer[i]->Release();
		mSwapChainBuffer[i] = nullptr;
	}
	depthStencilBuffer->Release();
	depthStencilBuffer = nullptr;

	// Resize the swap chain.
	swapChain->ResizeBuffers(SwapChainBufferCount, 800, 600, DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

	mCurrBackBuffer = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SwapChainBufferCount; i++)
	{
		swapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i]));
		device->CreateRenderTargetView(mSwapChainBuffer[i], nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, mRtvDescriptorSize);
	}

	// Create the depth/stencil buffer and view.
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = 800;
	depthStencilDesc.Height = 600;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;

	// Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from 
	// the depth buffer.  Therefore, because we need to create two views to the same resource:
	//   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
	//   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
	// we need to create the depth buffer resource with a typeless format.  
	depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

	depthStencilDesc.SampleDesc.Count = canUseMSAA ? 4 : 1;
	depthStencilDesc.SampleDesc.Quality = canUseMSAA ? (m4xMsaaQuality - 1) : 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;
	CD3DX12_HEAP_PROPERTIES heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &depthStencilDesc, D3D12_RESOURCE_STATE_COMMON, &optClear, IID_PPV_ARGS(&depthStencilBuffer));

	// Create descriptor to mip level 0 of entire resource using the format of the resource.
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.Texture2D.MipSlice = 0;
	device->CreateDepthStencilView(depthStencilBuffer, &dsvDesc, DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	// Transition the resource from its initial state to be used as a depth buffer.
	CD3DX12_RESOURCE_BARRIER rscBarrier = CD3DX12_RESOURCE_BARRIER::Transition(depthStencilBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	commandList->ResourceBarrier(1, &rscBarrier);

	// Execute the resize commands.
	commandList->Close();
	ID3D12CommandList* cmdsLists[] = { commandList };
	commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until resize is complete.
	mCurrentFence++;

	// Add an instruction to the command queue to set a new fence point.  Because we 
	// are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	result = commandQueue->Signal(fence, mCurrentFence);
	assert(result == S_OK);

	// Wait until the GPU has completed commands up to this fence point.
	if (fence->GetCompletedValue() < mCurrentFence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, 0, false, EVENT_ALL_ACCESS);

		// Fire event when GPU hits current fence.  
		result = fence->SetEventOnCompletion(mCurrentFence, eventHandle);
		assert(result == S_OK);

		// Wait until the GPU hits current fence event is fired.
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);

		// ViewPort utilise pour faire des tailles de scene differentes (ex video yt sur une page web)
		viewPort.TopLeftX = 0.0f;
		viewPort.TopLeftY = 0.0f;
		viewPort.Width = static_cast<float>(800);
		viewPort.Height = static_cast<float>(600);
		viewPort.MinDepth = 0.0f;
		viewPort.MaxDepth = 1.0f;
		commandList->RSSetViewports(1, &viewPort);
	}
}