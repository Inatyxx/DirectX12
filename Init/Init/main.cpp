#include "pch.h"
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#include <d3d12.h>
#include "d3dx12.h"
#include <windowsX.h>
#include <dxgidebug.h>
#include "MathHelper.h"
#include "d3dutil.h"
#include "UploadBuffer.h"


using Microsoft::WRL::ComPtr;
using namespace DirectX;

void InitDirectX12();
void Resize();
void mainloop();
void BuildTriangleGeometry();

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

ID3D12Device* pDevice = nullptr;
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

struct ObjectConstants
{
	XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
};

struct Vertex
{
    XMFLOAT3 Pos;
    XMFLOAT4 Color;
};

// Creation de Descriptor Heaps
ID3D12DescriptorHeap* RTVDescriptorHeap = nullptr; // Render Target View
ID3D12DescriptorHeap* DSVDescriptorHeap = nullptr; // Depth Stencil View

D3D12_DESCRIPTOR_HEAP_DESC RTVDescriptorHeapDesc;
D3D12_DESCRIPTOR_HEAP_DESC DSVDescriptorHeapDesc;

ID3D12Resource* VertexBuffer = nullptr;
D3D12_VERTEX_BUFFER_VIEW* vertexBufferView = nullptr;


// Descriptor Size
UINT mRtvDescriptorSize;
UINT mDsvDescriptorSize;
UINT mCbvSrvDescriptorSize;
ID3D12RootSignature* mRootSignature = nullptr;
ID3D12PipelineState* mPSO = nullptr;
std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB = nullptr;
std::unique_ptr<MeshGeometry> mTriangleGeo = nullptr;
D3D12_VERTEX_BUFFER_VIEW mBoxVBView;
ID3D12Resource* rtBuffers[2];

bool canUseMSAA = false;
UINT m4xMsaaQuality = 0; // Qualite du MSAA utilise sur l'appareil

D3D12_VIEWPORT viewPort;
D3D12_RECT mScissorRect = { 0, 0, 800 / 2, 600 / 2 };

bool FullScreen = false;

void InitDirectX12()
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
    result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice));
    assert(result == S_OK);

    // Creation d'une fence
    result = pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
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

    result = pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&commandQueue));
    assert(result == S_OK);
    result = pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
    assert(result == S_OK);
    result = pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr, IID_PPV_ARGS(&commandList));
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
    if (FAILED(result)) {
        MessageBox(0, L"Failed to create DXGIFactory", 0, 0);
        return;
    }
    assert(result == S_OK);
    result = factory->CreateSwapChain(commandQueue, &swapChainDesc, &swapChain);
    assert(result == S_OK);

    // Creation des descriptor sizes
    mRtvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvDescriptorSize = pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    RTVDescriptorHeapDesc.NumDescriptors = SwapChainBufferCount;
    RTVDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    RTVDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    RTVDescriptorHeapDesc.NodeMask = 0;
    result = pDevice->CreateDescriptorHeap(&RTVDescriptorHeapDesc, IID_PPV_ARGS(&RTVDescriptorHeap));
    assert(result == S_OK);

    DSVDescriptorHeapDesc.NumDescriptors = 1;
    DSVDescriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    DSVDescriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    DSVDescriptorHeapDesc.NodeMask = 0;
    result = pDevice->CreateDescriptorHeap(&DSVDescriptorHeapDesc, IID_PPV_ARGS(&DSVDescriptorHeap));
    assert(result == S_OK);

    BuildTriangleGeometry();

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
    result = pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &depthStencilDesc, D3D12_RESOURCE_STATE_COMMON, &optClear, IID_PPV_ARGS(&depthStencilBuffer));
    assert(result == S_OK);
    pDevice->CreateDepthStencilView(depthStencilBuffer, nullptr, DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

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

    ////Create Swapchain
    //HRESULT result = factory->CreateSwapChain(commandQueue, &swapChainDesc, (IDXGISwapChain**)&swapChain);
    //if (FAILED(result)) {
    //    MessageBox(0, L"Failed to create Swapchain", 0, 0);
    //    return;
    //}


    //Query Interface
    swapChain->QueryInterface(IID_PPV_ARGS(&swapChain)),"Failed to Query Interface to Swapchain4";


    // Create RTV Descriptor Heap
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = 1;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    pDevice->CreateDescriptorHeap(&cbvHeapDesc,IID_PPV_ARGS(&RTVDescriptorHeap));

    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(pDevice, 1, true);

    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    D3D12_GPU_VIRTUAL_ADDRESS cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();
    // Offset to the ith object constant buffer in the buffer.
    int boxCBufIndex = 0;
    cbAddress += boxCBufIndex * objCBByteSize;

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    cbvDesc.BufferLocation = cbAddress;
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    pDevice->CreateConstantBufferView(&cbvDesc, RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    // Shader Compliation

    ID3DBlob* vertexShader;
    ID3DBlob* pixelShader;
	ID3DBlob* errorBlob;

#if defined (_DEBUG)
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    // Vertex Shader
    HRESULT hr = D3DCompileFromFile(L"VertexShader.hlsl", nullptr, nullptr, "main", "vs_5_0", compileFlags, 0, &vertexShader, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        assert(false && "Failed to compile vertex shader");
        return;
    }

    // Pixel Shader
    hr = D3DCompileFromFile(L"PixelShader.hlsl", nullptr, nullptr, "main", "ps_5_0", compileFlags, 0, &pixelShader, &errorBlob);
    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        assert(false && "Failed to compile pixel shader");
        return;
    }


    // Create Vertex Buffer
    float VB_Data[] =
    {
         0.0f   ,  0.25f,
         0.25f  , -0.25f,
        -0.25f  , -0.25f
    };

    // Heap Properties
    heapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    // Resource Description
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = sizeof(VB_Data);
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = pDevice->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&VertexBuffer));
    assert(SUCCEEDED(hr) && "Failed to create committed resource for vertex buffer");

    // Map
    float* pSharedData;
    hr = VertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pSharedData));
    assert(SUCCEEDED(hr) && "Failed to map vertex buffer");

    // Copy
    memcpy(pSharedData, VB_Data, sizeof(VB_Data));

    // Unmap
    VertexBuffer->Unmap(0, nullptr);

    // Create Root Signature

    ID3DBlob* RootBlob;
    D3D12_ROOT_SIGNATURE_DESC Root_Desc = {};
    Root_Desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    Root_Desc.NumStaticSamplers = 0;
    Root_Desc.NumParameters = 0;

    CD3DX12_ROOT_PARAMETER slotRootParameter[1];

    // Create a single descriptor table of CBVs.
    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable);

    // A root signature is an array of root parameters.
    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ID3DBlob* serializedRootSig = nullptr;
   

    if (errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }

    pDevice->CreateRootSignature(0,serializedRootSig->GetBufferPointer(),serializedRootSig->GetBufferSize(),IID_PPV_ARGS(&mRootSignature));

    //Serialize Root Signature
    D3D12SerializeRootSignature(&Root_Desc, D3D_ROOT_SIGNATURE_VERSION_1, &RootBlob, nullptr),"Failed to Serialize Root Signature";


        //Create Root Signature
    pDevice->CreateRootSignature(0, RootBlob->GetBufferPointer(), RootBlob->GetBufferSize(), IID_PPV_ARGS(&mRootSignature)),"Failed to Create Root Signature";


        // Create Pipeline State Object

        //Input Layout
        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    //PSO Description
    D3D12_GRAPHICS_PIPELINE_STATE_DESC PSO_Desc = {};

    PSO_Desc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };

    PSO_Desc.pRootSignature = mRootSignature;

    PSO_Desc.VS.BytecodeLength = vertexShader->GetBufferSize();
    PSO_Desc.VS.pShaderBytecode = vertexShader->GetBufferPointer();

    PSO_Desc.PS.BytecodeLength = pixelShader->GetBufferSize();
    PSO_Desc.PS.pShaderBytecode = pixelShader->GetBufferPointer();

    PSO_Desc.NodeMask = 0;

    PSO_Desc.SampleDesc.Count = 1;
    PSO_Desc.SampleDesc.Quality = 0;

    PSO_Desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    PSO_Desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    PSO_Desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    PSO_Desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    PSO_Desc.NumRenderTargets = 1;
    PSO_Desc.RTVFormats[0] = swapChainDesc.BufferDesc.Format;

    //Create PSO
    pDevice->CreateGraphicsPipelineState(&PSO_Desc, IID_PPV_ARGS(&mPSO)),"Failed to Create PSO";

    //Create Fence
    pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "Failed to Create Fence";

    //Create Command List
    pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, mPSO, IID_PPV_ARGS(&commandList)), "Failed to create Command List";

        commandList->Close();
}




void Render()
{
    if (swapChain)
    {
        // Reset Command List and Allocator
        commandAllocator->Reset();
        commandList->Reset(commandAllocator, mPSO);

        // Set Root Signature
        commandList->SetGraphicsRootSignature(mRootSignature);

        // Indicate that Backbuffer will be used as Render Target
        D3D12_RESOURCE_BARRIER rb1 = {};
        rb1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb1.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rb1.Transition.pResource = mSwapChainBuffer[mCurrBackBuffer];
        rb1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rb1.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        rb1.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(1, &rb1);

        // Set RenderTargets
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
        cpu_handle.ptr += mCurrBackBuffer * pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        commandList->OMSetRenderTargets(1, &cpu_handle, false, nullptr);

        // Clear Render Target View
        const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        commandList->ClearRenderTargetView(cpu_handle, clearColor, 0, nullptr);

        // Draw Triangle
        commandList->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->IASetVertexBuffers(0, 1, vertexBufferView);
        commandList->DrawInstanced(3, 1, 0, 0);

        // Indicate that Backbuffer will now be used to Present
        D3D12_RESOURCE_BARRIER rb2 = {};
        rb2.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rb2.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        rb2.Transition.pResource = mSwapChainBuffer[mCurrBackBuffer];
        rb2.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rb2.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        rb2.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        commandList->ResourceBarrier(1, &rb2);

        // Execute Command Lists
        commandList->Close();
        ID3D12CommandList* cmdsLists[] = { commandList };
        commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

        // Wait For Command Queue
        mCurrentFence++;
        commandQueue->Signal(fence, mCurrentFence);

        if (fence->GetCompletedValue() < mCurrentFence)
        {
            HANDLE eventHandle = CreateEventEx(nullptr, 0, false, EVENT_ALL_ACCESS);
            fence->SetEventOnCompletion(mCurrentFence, eventHandle);
            WaitForSingleObject(eventHandle, INFINITE);
            CloseHandle(eventHandle);
        }

        // Present
        swapChain->Present(1, 0);
        mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
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
        std::cerr << "Erreur : l'instance de l'application n'a pas été créée" << std::endl;
        return false;
    }

    WNDCLASS wc = {};

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

    // Creation de la fenêtre
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

    ShowWindow(hMainWnd, SW_SHOW);
    InitDirectX12(); // DirectX12 Init

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
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void Cleanup()
{
    if (fence) fence->Release();
    if (commandList) commandList->Release();
    if (commandAllocator) commandAllocator->Release();
    if (commandQueue) commandQueue->Release();
    if (swapChain) swapChain->Release();
    if (pDevice) pDevice->Release();
    if (RTVDescriptorHeap) RTVDescriptorHeap->Release();
    if (DSVDescriptorHeap) DSVDescriptorHeap->Release();
    if (depthStencilBuffer) depthStencilBuffer->Release();
    for (int i = 0; i < SwapChainBufferCount; ++i)
    {
        if (mSwapChainBuffer[i]) mSwapChainBuffer[i]->Release();
    }
    if (d3d12Debug) d3d12Debug->Release();
    if (dxgiDebug) dxgiDebug->Release();
}


int WINAPI main(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    // create the window
    if (!InitializeWindow(hInstance, nShowCmd, 800, 600, false))
    {
        MessageBox(0, L"Window Initialization - Failed",
            L"Error", MB_OK);
        return 0;
    }
    mainloop();

    return 0;
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
    Cleanup();
}

void OnResize()
{
    assert(pDevice);
    assert(swapChain);
    assert(commandAllocator);

    // Flush before changing any resources.
    mCurrentFence++;
    commandQueue->Signal(fence, mCurrentFence);

    if (fence->GetCompletedValue() < mCurrentFence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, 0, false, EVENT_ALL_ACCESS);
        fence->SetEventOnCompletion(mCurrentFence, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    commandList->Reset(commandAllocator, nullptr);

    // Release the previous resources we will be recreating.
    for (int i = 0; i < SwapChainBufferCount; ++i)
    {
        if (mSwapChainBuffer[i]) mSwapChainBuffer[i]->Release();
    }
    if (depthStencilBuffer) depthStencilBuffer->Release();

    // Resize the swap chain.
    swapChain->ResizeBuffers(SwapChainBufferCount, 800, 600, DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);

    mCurrBackBuffer = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SwapChainBufferCount; i++)
    {
        swapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i]));
        pDevice->CreateRenderTargetView(mSwapChainBuffer[i], nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(i, mRtvDescriptorSize);
    }

    // Create the depth/stencil buffer and view.
    D3D12_RESOURCE_DESC depthStencilDesc;
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

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;
    CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);
    pDevice->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE, &depthStencilDesc, D3D12_RESOURCE_STATE_COMMON, &optClear, IID_PPV_ARGS(&depthStencilBuffer));

    // Create descriptor to mip level 0 of entire resource using the format of the resource.
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.Texture2D.MipSlice = 0;
    pDevice->CreateDepthStencilView(depthStencilBuffer, &dsvDesc, DSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    // Transition the resource from its initial state to be used as a depth buffer.
    CD3DX12_RESOURCE_BARRIER rscBarrier = CD3DX12_RESOURCE_BARRIER::Transition(depthStencilBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    commandList->ResourceBarrier(1, &rscBarrier);

    // Execute the resize commands.
    commandList->Close();
    ID3D12CommandList* cmdsLists[] = { commandList };
    commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until resize is complete.
    mCurrentFence++;
    commandQueue->Signal(fence, mCurrentFence);

    if (fence->GetCompletedValue() < mCurrentFence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, 0, false, EVENT_ALL_ACCESS);
        fence->SetEventOnCompletion(mCurrentFence, eventHandle);
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

    // Update the viewport.
    viewPort.TopLeftX = 0.0f;
    viewPort.TopLeftY = 0.0f;
    viewPort.Width = static_cast<float>(800);
    viewPort.Height = static_cast<float>(600);
    viewPort.MinDepth = 0.0f;
    viewPort.MaxDepth = 1.0f;
    commandList->RSSetViewports(1, &viewPort);
}


void BuildTriangleGeometry()
{
    // Définir les sommets pour un triangle
    std::array<Vertex, 3> vertices =
    {
        Vertex({ XMFLOAT3(0.0f, 0.25f, 0.0f), XMFLOAT4(Colors::White) }),
        Vertex({ XMFLOAT3(0.25f, -0.25f, 0.0f), XMFLOAT4(Colors::Black) }),
        Vertex({ XMFLOAT3(-0.25f, -0.25f, 0.0f), XMFLOAT4(Colors::Red) })
    };

    // Définir les indices pour un triangle
    std::array<std::uint16_t, 3> indices =
    {
        0, 1, 2
    };

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    mTriangleGeo = std::make_unique<MeshGeometry>();
    mTriangleGeo->Name = "triangleGeo";

    D3DCreateBlob(vbByteSize, &mTriangleGeo->VertexBufferCPU);
    CopyMemory(mTriangleGeo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    D3DCreateBlob(ibByteSize, &mTriangleGeo->IndexBufferCPU);
    CopyMemory(mTriangleGeo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    mTriangleGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(pDevice, commandList, vertices.data(), vbByteSize, mTriangleGeo->VertexBufferUploader);

    mTriangleGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(pDevice, commandList, indices.data(), ibByteSize, mTriangleGeo->IndexBufferUploader);

    mTriangleGeo->VertexByteStride = sizeof(Vertex);
    mTriangleGeo->VertexBufferByteSize = vbByteSize;
    mTriangleGeo->IndexFormat = DXGI_FORMAT_R16_UINT;
    mTriangleGeo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    mTriangleGeo->DrawArgs["triangle"] = submesh;
}