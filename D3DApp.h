/*****************************************************************//**
 * \file   d3dApp.h
 * \brief  a d3dApp base class, initializing window
 *			and dx12, shutdown stuffs
 *
 * \author LirRuntu liruntu2333@gmail.com
 * \date   June 2022
 *********************************************************************/
#pragma once

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")

#include "D3DUtil.h"
#include "GameTimer.h"

class D3DApp
{
public:

	D3DApp(const D3DApp&) = delete;
	D3DApp(D3DApp&&)      = delete;
	D3DApp& operator      =(const D3DApp&) = delete;
	D3DApp& operator      =(D3DApp&&) = delete;

protected:
	explicit D3DApp(HINSTANCE hInstance);
	virtual ~D3DApp();

public:

	static D3DApp* GetApp() { return mApp; }

	HINSTANCE AppInst() const { return mhAppInst; }
	HWND      MainWnd()const { return mhMainWnd; }
	float     AspectRatio()const
	{
		return static_cast<float>(mClientWidth) / mClientHeight;
	}

	int Run();

	virtual bool Initialize();
	virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
	virtual void CreateRtvAndDsvDescriptorHeaps();
	virtual void OnResize();
	virtual void Update(const GameTimer& gt) = 0;
	virtual void Draw(const GameTimer& gt) = 0;

	virtual void OnMouseDown(WPARAM btnState, int x, int y) { }
	virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
	virtual void OnMouseMove(WPARAM btnState, int x, int y) { }

protected:

	bool InitMainWindow();
	bool InitDirect3D();
	void CreateCommandObjects();
	void CreateSwapChain();

	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer() const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;

	void CalculateFrameStats() const;

	void LogAdapters() const;
	void LogAdapterOutputs(IDXGIAdapter* adapter) const;
	void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format) const;

protected:

	static D3DApp* mApp;

	HINSTANCE mhAppInst = nullptr;
	HWND      mhMainWnd = nullptr;
	bool      mAppPaused = false;
	bool      mMinimized = false;
	bool      mMaximized = false;
	bool      mResizing = false;
	bool      mFullscreenState = false;

	GameTimer mTimer;

	Microsoft::WRL::ComPtr<IDXGIFactory4> mDxgiFactory;
	Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
	Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice;

	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
	UINT64 mCurrentFence = 0;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;

	static constexpr int SWAP_CHAIN_BUFFER_COUNT = 2;
	int mCurrBackBuffer = 0;
	Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SWAP_CHAIN_BUFFER_COUNT];

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;

	D3D12_VIEWPORT mScreenViewport{};
	D3D12_RECT mScissorRect{};

	UINT mRtvDescriptorSize = 0;
	UINT mDsvDescriptorSize = 0;
	UINT mCbvSrvUavDescriptorSize = 0;

	// Derived class should set these in derived constructor to customize starting values.
	std::wstring mMainWndCaption = L"壹隻憂鬱臺灣烏龜尋釁幾羣骯髒變態囓齒鱷龞，幾羣骯髒變態囓齒鱷龞圍毆壹隻憂鬱臺灣烏龜。";
	// D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	int mClientWidth = 0;
	int mClientHeight = 0;

};
