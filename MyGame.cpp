/*****************************************************************//**
 * \file   MyGame.cpp
 * \brief  draw a box
 *
 * \author LirRuntu liruntu2333@gmail.com
 * \date   June 2022
 *********************************************************************/

#include <DirectXColors.h>

#include "MyGame.h"
#include "GeometryGenerator.h"
#include "3rdparty/DirectXTK12/Inc/DDSTextureLoader.h"
#include "3rdparty/DirectXTK12/Inc/ResourceUploadBatch.h"

using namespace DX;

namespace 
{
	const float* gRenderTargetCleanValue = DirectX::Colors::OrangeRed.f;
}

MyGame::MyGame(HINSTANCE hInstance) : D3DApp(hInstance) {}

MyGame::~MyGame()
{
	if (md3dDevice != nullptr)
	{
		FlushCommandQueue();
	}
}

bool MyGame::Initialize()
{
	if (!D3DApp::Initialize()) return false;

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Check device supported msaa sample count.
	for (mSampleCount = SAMPLE_COUNT_MAX; mSampleCount > 1; mSampleCount--)
	{
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = { mBackBufferFormat, mSampleCount };
		if (FAILED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))))
			continue;

		if (levels.NumQualityLevels > 0)
			break;
	}

	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mBlurFilter = std::make_unique<BlurFilter>(md3dDevice.Get(), 
		mClientWidth, mClientHeight, mBackBufferFormat);

	mWaves = std::make_unique<Waves>(md3dDevice.Get(),mCommandQueue.Get(),
		256, 256, 0.25f, 0.03f, 2.0f, 0.2f);

	mSobelFilter = std::make_unique<SobelFilter>(md3dDevice.Get(),
		mClientWidth, mClientHeight, mBackBufferFormat);

	mMsaaResolveDest = std::make_unique<MidwayTexture>(md3dDevice.Get(), 
		mClientWidth, mClientHeight, mBackBufferFormat);

	LoadTextures();
	BuildRootSignature();
	BuildPostProcessRootSignature();
	BuildWavesRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildLandGeometry();
	BuildWavesGeometry();
	BuildTreeSpriteGeometry();
	BuildSphereGeometry();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildPipelineStateObjects();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* commandLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	FlushCommandQueue();

	OnResize();
	return true;
}

void MyGame::OnResize()
{
	using namespace DirectX;

	D3DApp::OnResize();

	{
		CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

		// Create an MSAA render target.
		D3D12_RESOURCE_DESC msaaRtDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			mBackBufferFormat,
			mClientWidth, mClientHeight,
			1, 1, mSampleCount);
		msaaRtDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

		D3D12_CLEAR_VALUE rtClearValue{};
		rtClearValue.Format = mBackBufferFormat;
		memcpy(rtClearValue.Color, gRenderTargetCleanValue, sizeof(float) * 4);

		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&msaaRtDesc,
			D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
			&rtClearValue,
			IID_PPV_ARGS(mMsaaRenderTarget.GetAddressOf())));

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
		rtvDesc.Format = mBackBufferFormat;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;

		md3dDevice->CreateRenderTargetView(mMsaaRenderTarget.Get(), &rtvDesc,
			mRtvDescHeap->GetCPUDescriptorHandleForHeapStart());

		mMsaaRenderTarget->SetName(L"MSAA Render Target");

		// Create an MSAA depth stencil view.
		D3D12_RESOURCE_DESC msaaDsDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			mDepthStencilFormat,
			mClientWidth, mClientHeight,
			1, 1, mSampleCount);
		msaaDsDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		msaaDsDesc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

		D3D12_CLEAR_VALUE dsClearValue{};
		dsClearValue.Format = mDepthStencilFormat;
		dsClearValue.DepthStencil.Depth = 1.0f;
		dsClearValue.DepthStencil.Stencil = 0;

		ThrowIfFailed(md3dDevice->CreateCommittedResource(
			&heapProperties,
			D3D12_HEAP_FLAG_NONE,
			&msaaDsDesc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&dsClearValue,
			IID_PPV_ARGS(mMsaaDepthStencil.GetAddressOf())));

		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = mDepthStencilFormat;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;

		md3dDevice->CreateDepthStencilView(mMsaaDepthStencil.Get(),
			&dsvDesc, mDsvDescHeap->GetCPUDescriptorHandleForHeapStart());

		mMsaaDepthStencil->SetName(L"MSAA Depth Stencil");
	}

	const XMMATRIX proj = XMMatrixPerspectiveFovLH(
		0.25f * MathHelper::Pi, AspectRatio(),
		1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, proj);

	if (mBlurFilter != nullptr)
	{
		mBlurFilter->OnResize(mClientWidth, mClientHeight);
	}

	if (mSobelFilter != nullptr)
	{
		mSobelFilter->OnResize(mClientWidth, mClientHeight);
	}

	if (mMsaaResolveDest != nullptr)
	{
		mMsaaResolveDest->OnResize(mClientWidth, mClientHeight);
	}
}

void MyGame::Update(const GameTimer& gameTimer)
{
	OnKeyboardInput(gameTimer);
	UpdateCamera(gameTimer);

	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % FRAME_RESOURCES_NUM;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	if (mCurrFrameResource->Fence != 0 &&
		mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		if (const HANDLE handle = 
			CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS))
		{
			ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, handle));
			WaitForSingleObject(handle, INFINITE);
			CloseHandle(handle);
		}
	}

	AnimateMaterials(gameTimer);
	UpdateObjectConstBuffs(gameTimer);
	UpdateMaterialConstBuffs(gameTimer);
	UpdateMainPassConstBuffs(gameTimer);
}

void MyGame::Draw(const GameTimer& gameTimer)
{
	using namespace DirectX;

	const auto& cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPipelineStateObjects["opaque"].Get()));

	ID3D12DescriptorHeap* heaps[] = { mSrvUavDescHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

	UpdateWavesGpu(gameTimer);

	// MSAA render target and depth stencil setups
	{
		{
			D3D12_RESOURCE_BARRIER barrier =
				CD3DX12_RESOURCE_BARRIER::Transition(mMsaaRenderTarget.Get(),
					D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

			mCommandList->ResourceBarrier(1, &barrier);
		}

		const auto& hRtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvDescHeap->GetCPUDescriptorHandleForHeapStart());
		const auto& hDsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvDescHeap->GetCPUDescriptorHandleForHeapStart());
		mCommandList->ClearRenderTargetView(hRtv, gRenderTargetCleanValue, 0, nullptr);
		mCommandList->ClearDepthStencilView(hDsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
			1.0f, 0, 0, nullptr);
		mCommandList->OMSetRenderTargets(1, &hRtv,
			true, &hDsv);
	}

	// forward rendering
	{
		mCommandList->SetPipelineState(mPipelineStateObjects["opaque"].Get());
		mCommandList->RSSetViewports(1, &mScreenViewport);
		mCommandList->RSSetScissorRects(1, &mScissorRect);
		mCommandList->SetGraphicsRootSignature(mRootSignature.Get());
		auto passCb = mCurrFrameResource->PassConstBuff->Resource();
		mCommandList->SetGraphicsRootConstantBufferView(2, passCb->GetGPUVirtualAddress());

		DrawIndexedRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)]);

		mCommandList->SetPipelineState(mPipelineStateObjects["alphaTested"].Get());
		DrawIndexedRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::AlphaTested)]);

		mCommandList->SetPipelineState(mPipelineStateObjects["treeSprite"].Get());
		DrawIndexedRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::AlphaTestedTreeSprite)]);

		mCommandList->SetPipelineState(mPipelineStateObjects["transparent"].Get());
		DrawIndexedRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::Transparent)]);

		mCommandList->SetGraphicsRootDescriptorTable(4, mWaves->GetDisplacementMap());
		mCommandList->SetPipelineState(mPipelineStateObjects["wavesRender"].Get());
		DrawIndexedRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::GpuWaves)]);

#ifdef VISUALIZE_NORMAL
		mCommandList->SetPipelineState(mPipelineStateObjects["visNorm"].Get());
		DrawRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::VisualNorm)]);
#endif
	}

	// MSAA render target resolve to back buffer
	{
		CD3DX12_RESOURCE_BARRIER barriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(mMsaaResolveDest->GetResource(),
			                                     D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RESOLVE_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(mMsaaRenderTarget.Get(),
			                                     D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
		};

		mCommandList->ResourceBarrier(_countof(barriers), barriers);
		mCommandList->ResolveSubresource(mMsaaResolveDest->GetResource(), 0,
		mMsaaRenderTarget.Get(), 0, mBackBufferFormat);
	}

	// Blur Post Effect pass
	{
		mBlurFilter->Execute(
			mCommandList.Get(),
			mBlurRootSignature.Get(),
			mPipelineStateObjects["horzBlur"].Get(),
			mPipelineStateObjects["vertBlur"].Get(),
			mMsaaResolveDest->GetResource(),
			0, 2.5f, D3D12_FLOAT32_MAX);
	}

	// Sobel filter pass
	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mMsaaResolveDest->GetResource(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_GENERIC_READ);
		mCommandList->ResourceBarrier(1, &barrier);

		mSobelFilter->Execute(mCommandList.Get(), mSobelRootSignature.Get(),
			mPipelineStateObjects["sobel"].Get(), mMsaaResolveDest->GetSrv());

		barrier = CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
		mCommandList->ResourceBarrier(1, &barrier);

		const D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtv = GetCurrentBackBufferRtv();
		mCommandList->OMSetRenderTargets(1, &backBufferRtv,
			true, nullptr);
		mCommandList->SetGraphicsRootSignature(mSobelRootSignature.Get());
		mCommandList->SetPipelineState(mPipelineStateObjects["composite"].Get());
		mCommandList->SetGraphicsRootDescriptorTable(0, mMsaaResolveDest->GetSrv());
		mCommandList->SetGraphicsRootDescriptorTable(1, mSobelFilter->GetOutputSrv());

		{	// draw a fullscreen rectangle, shader SV_VertexID defines the vertex
			mCommandList->IASetVertexBuffers(0, 0, nullptr);
			mCommandList->IASetIndexBuffer(nullptr);
			mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			mCommandList->DrawInstanced(6, 1, 0, 0);
		}

		barrier = CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
		mCommandList->ResourceBarrier(1, &barrier);
	}

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;

	mCurrFrameResource->Fence = ++mCurrentFence;
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void MyGame::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void MyGame::OnMouseMove(WPARAM btnState, int x, int y)
{
	using namespace DirectX;

	if ((btnState & MK_LBUTTON) != 0)
	{
		const float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		const float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mTheta += dx;
		mPhi += dy;

		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		const float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
		const float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);

		mRadius += dx - dy;

		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void MyGame::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void MyGame::OnKeyboardInput(const GameTimer& gameTimer)
{
	const float dt = gameTimer.DeltaTime();

	if (GetAsyncKeyState(VK_LEFT) & 0x8000)
		mSunTheta -= 1.0f * dt;
	if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
		mSunTheta += 1.0f * dt;
	if (GetAsyncKeyState(VK_UP) & 0x8000)
		mSunPhi -= 1.0f * dt;
	if (GetAsyncKeyState(VK_DOWN) & 0x8000)
		mSunPhi += 1.0f * dt;
}

void MyGame::UpdateCamera(const GameTimer& gameTimer)
{
	using namespace DirectX;

	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	const auto pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	const auto target = XMVectorZero();
	const auto up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	const auto view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void MyGame::AnimateMaterials(const GameTimer& gameTimer)
{
	auto water = mMaterials["water"].get();

	float& tu = water->MatTransform(3, 0);
	float& tv = water->MatTransform(3, 1);

	tu += 0.1f * gameTimer.DeltaTime();
	tv += 0.02f * gameTimer.DeltaTime();

	if (tu >= 0.1f) tu -= 1.0f;
	if (tv >= 1.0f) tv -= 1.0f;

	water->NumFrameDirty = FRAME_RESOURCES_NUM;
}

void MyGame::UpdateObjectConstBuffs(const GameTimer& gameTimer) const
{
	using namespace DirectX;

	const auto currObjCb = mCurrFrameResource->ObjConstBuff.get();
	for (auto & ri : mRenderItems)
	{
		if (ri->NumFrameDirty > 0)
		{
			const XMMATRIX world = XMLoadFloat4x4(&ri->World);
			const XMMATRIX texTransform = XMLoadFloat4x4(&ri->TexTransform);
			const XMMATRIX worldInvT = MathHelper::InverseTranspose(world);

			ObjectConstants objConst;
			XMStoreFloat4x4(&objConst.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConst.TexTransform, XMMatrixTranspose(texTransform));
			XMStoreFloat4x4(&objConst.WorldInvTranspose, XMMatrixTranspose(worldInvT));
			objConst.DisplacementMapTexelSize = ri->DisplacementMapTexelSize;
			objConst.GridSpatialStep = ri->GridSpatialStep;

			currObjCb->CopyData(ri->ObjConstBuffIndex, objConst);

			ri->NumFrameDirty--;
		}
	}
}

void MyGame::UpdateMaterialConstBuffs(const GameTimer& gameTimer) const
{
	using namespace DirectX;

	auto currMatCb = mCurrFrameResource->MatConstBuff.get();
	for (const auto & pair : mMaterials)
	{
		Material* material = pair.second.get();
		if (material->NumFrameDirty > 0)
		{
			MaterialConstants matConst;
			matConst.DiffuseAlbedo = material->DiffuseAlbedo;
			matConst.FresnelR0 = material->FresnelR0;
			matConst.Roughness = material->Roughness;

			XMMATRIX matTrans = XMLoadFloat4x4(&material->MatTransform);
			XMStoreFloat4x4(&matConst.MatTransform, XMMatrixTranspose(matTrans));

			currMatCb->CopyData(material->MatCbIndex, matConst);
			--material->NumFrameDirty;
		}
	}
}

void MyGame::UpdateMainPassConstBuffs(const GameTimer& gameTimer)
{
	using namespace DirectX;

	const XMMATRIX view = XMLoadFloat4x4(&mView);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);
	const XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	const XMMATRIX invView = XMMatrixInverse(nullptr, view);
	const XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	const XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	XMStoreFloat4x4(&mMainPassConstBuff.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassConstBuff.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassConstBuff.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassConstBuff.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassConstBuff.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassConstBuff.InvViewProj, XMMatrixTranspose(invViewProj));

	mMainPassConstBuff.EyePosW = mEyePos;
	mMainPassConstBuff.RenderTargetSize =
	{ static_cast<float>(mClientWidth), static_cast<float>(mClientHeight) };
	mMainPassConstBuff.InvRenderTargetSize =
	{ 1.0f / static_cast<float>(mClientWidth), 1.0f / static_cast<float>(mClientHeight) };
	mMainPassConstBuff.NearZ = 1.0f;
	mMainPassConstBuff.FarZ = 1000.0f;
	mMainPassConstBuff.TotalTime = gameTimer.TotalTime();
	mMainPassConstBuff.DeltaTime = gameTimer.DeltaTime();

	mMainPassConstBuff.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

	const auto sunDir = -MathHelper::SphericalToCartesian(1.0f, mSunTheta, mSunPhi);
	XMStoreFloat3(&mMainPassConstBuff.Lights[0].Direction, sunDir);

	mMainPassConstBuff.Lights[0].Intensity = { 0.6f, 0.6f, 0.6f };
	mMainPassConstBuff.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassConstBuff.Lights[1].Intensity = { 0.3f, 0.3f, 0.3f };
	mMainPassConstBuff.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassConstBuff.Lights[2].Intensity = { 0.15f, 0.15f, 0.15f };
	
	const auto currPassCb = mCurrFrameResource->PassConstBuff.get();
	currPassCb->CopyData(0, mMainPassConstBuff);
}

void MyGame::UpdateWavesGpu(const GameTimer& gameTimer)
{
	static float tBase = 0.0f;

	if (gameTimer.TotalTime() - tBase >= 0.25f)
	{
		tBase += 0.25f;

		int i = MathHelper::Rand(4, mWaves->GetRowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->GetColumnCount() - 5);

		float r = MathHelper::RandF(1.0f, 2.0f);

		mWaves->Disturb(mCommandList.Get(), mWavesRootSignature.Get(),
			mPipelineStateObjects["wavesDisturb"].Get(), i, j, r);
	}

	mWaves->Update(gameTimer, mCommandList.Get(), mWavesRootSignature.Get(), 
		mPipelineStateObjects["wavesUpdate"].Get());
}

void MyGame::BuildDescriptorHeaps()
{
	// Create descriptor heaps for MSAA render target views and depth stencil views.
	{
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
		rtvHeapDesc.NumDescriptors = 2;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

		ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc,
			IID_PPV_ARGS(mRtvDescHeap.GetAddressOf())));

		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
		dsvHeapDesc.NumDescriptors = 1;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

		ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dsvHeapDesc,
			IID_PPV_ARGS(mDsvDescHeap.GetAddressOf())));
	}

	const int texCnt = static_cast<int>(mTextures.size());

	// Create descriptor heap for CBVs, SRVs and UAVs.
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.NumDescriptors = static_cast<UINT>(texCnt + BlurFilter::SRV_UAV_COUNT
		+ Waves::SRV_UAV_COUNT + SobelFilter::SRV_UAV_COUNT + MidwayTexture::SRV_COUNT);
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&heapDesc,
		IID_PPV_ARGS(mSrvUavDescHeap.GetAddressOf())));

	auto hCpuStart = mSrvUavDescHeap->GetCPUDescriptorHandleForHeapStart();
	auto hGpuStart = mSrvUavDescHeap->GetGPUDescriptorHandleForHeapStart();

	{
		// Fill actual SRVs into the heap.
		auto grass = mTextures["grassTex"]->Resource;
		auto water = mTextures["waterTex"]->Resource;
		auto fence = mTextures["fenceTex"]->Resource;
		auto trees = mTextures["treeArrayTex"]->Resource;

		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(hCpuStart);

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format                    = grass->GetDesc().Format;
		srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		// Set to -1 to indicate all the mipmap levels from MostDetailedMip on down to least detailed.
		srvDesc.Texture2D.MipLevels       = -1;
		md3dDevice->CreateShaderResourceView(grass.Get(), &srvDesc, hDescriptor);

		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		srvDesc.Format = water->GetDesc().Format;
		md3dDevice->CreateShaderResourceView(water.Get(), &srvDesc, hDescriptor);

		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		srvDesc.Format = fence->GetDesc().Format;
		md3dDevice->CreateShaderResourceView(fence.Get(), &srvDesc, hDescriptor);

		hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
		srvDesc.ViewDimension                  = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Format                         = trees->GetDesc().Format;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.MipLevels       = -1;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.Texture2DArray.ArraySize       = trees->GetDesc().DepthOrArraySize;
		md3dDevice->CreateShaderResourceView(trees.Get(), &srvDesc, hDescriptor);
	}

	// Create descriptor heaps for BlurFilter resources.
	int srvUavOffset = texCnt;
	mBlurFilter->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(hCpuStart, srvUavOffset, mCbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(hGpuStart, srvUavOffset, mCbvSrvUavDescriptorSize),
		mCbvSrvUavDescriptorSize);

	srvUavOffset += BlurFilter::SRV_UAV_COUNT;
	mWaves->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(hCpuStart, srvUavOffset, mCbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(hGpuStart, srvUavOffset, mCbvSrvUavDescriptorSize),
		mCbvSrvUavDescriptorSize);

	srvUavOffset += Waves::SRV_UAV_COUNT;
	mSobelFilter->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(hCpuStart, srvUavOffset, mCbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(hGpuStart, srvUavOffset, mCbvSrvUavDescriptorSize),
		mCbvSrvUavDescriptorSize);

	srvUavOffset += SobelFilter::SRV_UAV_COUNT;
	mMsaaResolveDest->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(hCpuStart, srvUavOffset, mCbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(hGpuStart, srvUavOffset, mCbvSrvUavDescriptorSize));
}

void MyGame::LoadTextures()
{
	auto grass = std::make_unique<Texture>();
	grass->Name = "grassTex";
	grass->FileName = L"Textures/grass12.dds";
	{
		DirectX::ResourceUploadBatch upload(md3dDevice.Get());
		upload.Begin();
		ThrowIfFailed(CreateDDSTextureFromFile(
			md3dDevice.Get(), upload,
			grass->FileName.c_str(),
			grass->Resource.GetAddressOf()));
		auto finished = upload.End(mCommandQueue.Get());
		finished.wait();
	}

	auto water = std::make_unique<Texture>();
	water->Name = "waterTex";
	water->FileName = L"Textures/juice.dds";
	{
		DirectX::ResourceUploadBatch upload(md3dDevice.Get());
		upload.Begin();
		ThrowIfFailed(CreateDDSTextureFromFile(
			md3dDevice.Get(), upload,
			water->FileName.c_str(),
			water->Resource.GetAddressOf()));
		auto finished = upload.End(mCommandQueue.Get());
		finished.wait();
	}

	auto fence = std::make_unique<Texture>();
	fence->Name = "fenceTex";
	fence->FileName = L"Textures/WireFence.dds";
	{
		DirectX::ResourceUploadBatch upload(md3dDevice.Get());
		upload.Begin();
		ThrowIfFailed(CreateDDSTextureFromFile(
			md3dDevice.Get(), upload,
			fence->FileName.c_str(),
			fence->Resource.GetAddressOf()));
		auto finished = upload.End(mCommandQueue.Get());
		finished.wait();
	}

	auto trees = std::make_unique<Texture>();
	trees->Name = "treeArrayTex";
	trees->FileName = L"Textures/treeArray2.dds";
	{
		DirectX::ResourceUploadBatch upload(md3dDevice.Get());
		upload.Begin();
		ThrowIfFailed(CreateDDSTextureFromFile(
			md3dDevice.Get(), upload,
			trees->FileName.c_str(),
			trees->Resource.GetAddressOf()));
		auto finished = upload.End(mCommandQueue.Get());
		finished.wait();
	}

	mTextures[grass->Name] = std::move(grass);
	mTextures[water->Name] = std::move(water);
	mTextures[fence->Name] = std::move(fence);
	mTextures[trees->Name] = std::move(trees);
}

void MyGame::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTbl;
	texTbl.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE dispMapTbl;
	dispMapTbl.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	CD3DX12_ROOT_PARAMETER slotRootParameter[5];
	slotRootParameter[0].InitAsDescriptorTable(1, &texTbl, D3D12_SHADER_VISIBILITY_PIXEL);	// diffuse texture
	slotRootParameter[1].InitAsConstantBufferView(0);	// object constants at b0
	slotRootParameter[2].InitAsConstantBufferView(1);	// pass constants at b1
	slotRootParameter[3].InitAsConstantBufferView(2);	// material constants at b2
	slotRootParameter[4].InitAsDescriptorTable(1, &dispMapTbl, D3D12_SHADER_VISIBILITY_ALL);	// material constants at t1

	const auto& staticSamplers = GetStaticSamplers();

	const CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter, 
		static_cast<UINT>(staticSamplers.size()), staticSamplers.data(), 
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
	serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void MyGame::BuildPostProcessRootSignature()
{
	{	// blur filter root signature
		CD3DX12_DESCRIPTOR_RANGE srvTbl, uavTbl;
		srvTbl.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		uavTbl.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

		CD3DX12_ROOT_PARAMETER slotRootParams[3];
		slotRootParams[0].InitAsConstants(13, 0);
		slotRootParams[1].InitAsDescriptorTable(1, &srvTbl);
		slotRootParams[2].InitAsDescriptorTable(1, &uavTbl);

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParams, 0, nullptr,
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
		const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
		}
		ThrowIfFailed(hr);

		ThrowIfFailed(md3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mBlurRootSignature.GetAddressOf())));
	}

	{	// Sobel filter root signature
		CD3DX12_DESCRIPTOR_RANGE srvTbl0;
		srvTbl0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		CD3DX12_DESCRIPTOR_RANGE srvTbl1;
		srvTbl1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
		CD3DX12_DESCRIPTOR_RANGE uavTbl0;
		uavTbl0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

		CD3DX12_ROOT_PARAMETER rootParams[3];
		rootParams[0].InitAsDescriptorTable(1, &srvTbl0);
		rootParams[1].InitAsDescriptorTable(1, &srvTbl1);
		rootParams[2].InitAsDescriptorTable(1, &uavTbl0);

		auto staticSamplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, rootParams, 
			static_cast<UINT>(staticSamplers.size()), staticSamplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
		const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
			serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
		}
		ThrowIfFailed(hr);

		ThrowIfFailed(md3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mSobelRootSignature.GetAddressOf())));
	}
}

void MyGame::BuildWavesRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE uavTbl0;
	uavTbl0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
	CD3DX12_DESCRIPTOR_RANGE uavTbl1;
	uavTbl1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
	CD3DX12_DESCRIPTOR_RANGE uavTbl2;
	uavTbl2.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);

	CD3DX12_ROOT_PARAMETER rootParams[4];
	rootParams[0].InitAsConstants(6, 0);
	rootParams[1].InitAsDescriptorTable(1, &uavTbl0);
	rootParams[2].InitAsDescriptorTable(1, &uavTbl1);
	rootParams[3].InitAsDescriptorTable(1, &uavTbl2);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, rootParams);
	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mWavesRootSignature.GetAddressOf())));
}

void MyGame::BuildShadersAndInputLayout()
{
	mShaders["defaultVS"] = LoadBinary(L"CompiledShaders/defaultVS.cso");
	mShaders["defaultPS"] = LoadBinary(L"CompiledShaders/defaultPS.cso");
	mShaders["alphaTestedPS"] = LoadBinary(L"CompiledShaders/alphaTestedPS.cso");

	mShaders["treeSpriteVS"] = LoadBinary(L"CompiledShaders/treeSpriteVS.cso");
	mShaders["treeSpriteGS"] = LoadBinary(L"CompiledShaders/treeSpriteGS.cso");
	mShaders["treeSpritePS"] = LoadBinary(L"CompiledShaders/treeSpritePS.cso");

	mShaders["sphereVS"] = LoadBinary(L"CompiledShaders/sphereVS.cso");
	mShaders["sphereGS"] = LoadBinary(L"CompiledShaders/sphereGS.cso");
	mShaders["spherePS"] = LoadBinary(L"CompiledShaders/spherePS.cso");

#ifdef VISUALIZE_NORMAL

	mShaders["visNormVS"] = LoadBinary(L"CompiledShaders/visNormVS.cso");
	mShaders["visNormGS"] = LoadBinary(L"CompiledShaders/visNormGS.cso");
	mShaders["visNormPS"] = LoadBinary(L"CompiledShaders/visNormPS.cso");

#endif

	mShaders["horzBlurCS"] = LoadBinary(L"CompiledShaders/horzBlurCS.cso");
	mShaders["vertBlurCS"] = LoadBinary(L"CompiledShaders/vertBlurCS.cso");

	mShaders["wavesVS"] = LoadBinary(L"CompiledShaders/wavesVS.cso");
	mShaders["wavesUpdateCS"] = LoadBinary(L"CompiledShaders/wavesUpdateCS.cso");
	mShaders["wavesDisturbCS"] = LoadBinary(L"CompiledShaders/wavesDisturbCS.cso");

	mShaders["sobelCS"] = LoadBinary(L"CompiledShaders/sobelCS.cso");
	mShaders["compositeVS"] = LoadBinary(L"CompiledShaders/compositeVS.cso");
	mShaders["compositePS"] = LoadBinary(L"CompiledShaders/compositePS.cso");

	mInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, Position),    D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL",	 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, Normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(Vertex, TexCoord),   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};

	mTreeSpriteInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"SIZE",     0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
}

void MyGame::BuildLandGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.f, 50, 50);

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		const auto& pos = grid.Vertices[i].Position;
		vertices[i].Position = pos;
		vertices[i].Position.y = GetHillsHeight(pos.x, pos.z);
		vertices[i].Normal = GetHillsNormal(pos.x, pos.z);
		vertices[i].TexCoord = grid.Vertices[i].TexC;
	}

	std::vector<uint16_t> indices = grid.GetIndices16();
	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, geo->VertexBufferCPU.GetAddressOf()));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSize, geo->IndexBufferCPU.GetAddressOf()));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vbByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);
}

void MyGame::BuildWavesGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 
		mWaves->GetRowCount(), mWaves->GetColumnCount());

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (int i = 0; i < vertices.size(); ++i)
	{
		vertices[i].Position = grid.Vertices[i].Position;
		vertices[i].Normal   = grid.Vertices[i].Normal;
		vertices[i].TexCoord = grid.Vertices[i].TexC;
	}

	std::vector indices(grid.Indices32);

	const UINT vbByteSize = mWaves->GetVertexCount() * sizeof(Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(uint32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	memcpy(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, geo->IndexBufferCPU.GetAddressOf()));
	memcpy(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat         = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount         = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void MyGame::BuildSphereGeometry()
{
	using namespace DirectX;

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData sphere = geoGen.CreateGeosphere(8.0f, 0);

	std::vector<Vertex> vertices(sphere.Vertices.size());
	for (int i = 0; i < sphere.Vertices.size(); ++i)
	{
		vertices[i].Position = sphere.Vertices[i].Position;
		vertices[i].Normal = sphere.Vertices[i].Normal;
		auto m = XMMatrixAffineTransformation2D({ 1.f,1.f,1.f }, { 0.5f,0.5f,}, XM_PIDIV4, {});
		XMStoreFloat2(&vertices[i].TexCoord, XMVector2Transform(XMLoadFloat2(&sphere.Vertices[i].TexC), m));
	}

	std::vector<uint16_t> indices = sphere.GetIndices16();
	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "sphereGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, geo->VertexBufferCPU.GetAddressOf()));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSize, geo->IndexBufferCPU.GetAddressOf()));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vbByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["sphere"] = submesh;

	mGeometries["sphereGeo"] = std::move(geo);
}

void MyGame::BuildTreeSpriteGeometry()
{
	using namespace DirectX;
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	constexpr int treeCnt = 16;
	std::array<TreeSpriteVertex, treeCnt> vertices{};
	for (auto& vertex : vertices)
	{
		const float x = MathHelper::RandF(-45.0f, 45.0f);
		const float z = MathHelper::RandF(-45.0f, 45.0f);
		const float y = GetHillsHeight(x, z) + 8.0f;

		vertex.Pos = XMFLOAT3(x, y, z);
		vertex.Size = XMFLOAT2(20.0f, 20.0f);
	}

	const std::array<uint16_t, treeCnt> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
	};

	constexpr UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(TreeSpriteVertex);
	constexpr UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpriteGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, geo->VertexBufferCPU.GetAddressOf()));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, geo->IndexBufferCPU.GetAddressOf()));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride     = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat          = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize  = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["points"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void MyGame::BuildPipelineStateObjects()
{
	// PSO for opaque objects.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc{};
	opaquePsoDesc.InputLayout.pInputElementDescs = mInputLayout.data();
	opaquePsoDesc.InputLayout.NumElements        = static_cast<UINT>(mInputLayout.size());
	opaquePsoDesc.pRootSignature                 = mRootSignature.Get();

	opaquePsoDesc.VS.pShaderBytecode             = mShaders["defaultVS"]->GetBufferPointer();
	opaquePsoDesc.VS.BytecodeLength              = mShaders["defaultVS"]->GetBufferSize();
	opaquePsoDesc.PS.pShaderBytecode             = mShaders["defaultPS"]->GetBufferPointer();
	opaquePsoDesc.PS.BytecodeLength              = mShaders["defaultPS"]->GetBufferSize();

	opaquePsoDesc.RasterizerState                = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState                     = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState              = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask                     = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets               = 1;
	opaquePsoDesc.RTVFormats[0]                  = mBackBufferFormat;

	opaquePsoDesc.SampleDesc.Count               = mSampleCount;
	opaquePsoDesc.SampleDesc.Quality             = 0;

	opaquePsoDesc.DSVFormat                      = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["opaque"].GetAddressOf())));

#ifdef VISUALIZE_NORMAL
	// PSO for visualizing normals
	D3D12_GRAPHICS_PIPELINE_STATE_DESC visNormPsoDesc = opaquePsoDesc;

	visNormPsoDesc.VS.pShaderBytecode = mShaders["visNormVS"]->GetBufferPointer();
	visNormPsoDesc.VS.BytecodeLength  = mShaders["visNormVS"]->GetBufferSize();
	visNormPsoDesc.GS.pShaderBytecode = mShaders["visNormGS"]->GetBufferPointer();
	visNormPsoDesc.GS.BytecodeLength  = mShaders["visNormGS"]->GetBufferSize();
	visNormPsoDesc.PS.pShaderBytecode = mShaders["visNormPS"]->GetBufferPointer();
	visNormPsoDesc.PS.BytecodeLength  = mShaders["visNormPS"]->GetBufferSize();

	visNormPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	visNormPsoDesc.RasterizerState.MultisampleEnable     = true;
	visNormPsoDesc.RasterizerState.AntialiasedLineEnable = true;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&visNormPsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["visNorm"].GetAddressOf())));
#endif

	// PSO for transparent objects.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC trnPsoDesc = opaquePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC trnBlendDesc{};
	trnBlendDesc.BlendEnable = true;
	trnBlendDesc.LogicOpEnable = false;
	trnBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	trnBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	trnBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	trnBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	trnBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	trnBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	trnBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	trnBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	trnPsoDesc.BlendState.RenderTarget[0] = trnBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&trnPsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["transparent"].GetAddressOf())));

	// PSO for Alpha tested objects.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestPsoDesc = opaquePsoDesc;
	alphaTestPsoDesc.VS =
	{
		mShaders["sphereVS"]->GetBufferPointer(),
		mShaders["sphereVS"]->GetBufferSize()
	};
	alphaTestPsoDesc.GS =
	{
		mShaders["sphereGS"]->GetBufferPointer(),
		mShaders["sphereGS"]->GetBufferSize()
	};
	alphaTestPsoDesc.PS =
	{
		mShaders["spherePS"]->GetBufferPointer(),
		mShaders["spherePS"]->GetBufferSize()
	};
	alphaTestPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	alphaTestPsoDesc.BlendState.AlphaToCoverageEnable = true;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestPsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["alphaTested"].GetAddressOf())));
;
	// PSO for tree sprite.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
	treeSpritePsoDesc.InputLayout =
	{
		mTreeSpriteInputLayout.data(),
		static_cast<UINT>(mTreeSpriteInputLayout.size())
	};
	treeSpritePsoDesc.VS =
	{
		mShaders["treeSpriteVS"]->GetBufferPointer(),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};
	treeSpritePsoDesc.GS =
	{
		mShaders["treeSpriteGS"]->GetBufferPointer(),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PS =
	{
		mShaders["treeSpritePS"]->GetBufferPointer(),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	treeSpritePsoDesc.BlendState.AlphaToCoverageEnable = true;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["treeSprite"].GetAddressOf())));

	// PSO for horizontal blur
	D3D12_COMPUTE_PIPELINE_STATE_DESC horzBlurPso{};
	horzBlurPso.pRootSignature = mBlurRootSignature.Get();
	horzBlurPso.CS.pShaderBytecode = mShaders["horzBlurCS"]->GetBufferPointer();
	horzBlurPso.CS.BytecodeLength = mShaders["horzBlurCS"]->GetBufferSize();

	ThrowIfFailed(md3dDevice->CreateComputePipelineState(&horzBlurPso,
		IID_PPV_ARGS(&mPipelineStateObjects["horzBlur"])));

	// PSO for vertical blur
	D3D12_COMPUTE_PIPELINE_STATE_DESC vertBlurPso{};
	vertBlurPso.pRootSignature = mBlurRootSignature.Get();
	vertBlurPso.CS.pShaderBytecode = mShaders["vertBlurCS"]->GetBufferPointer();
	vertBlurPso.CS.BytecodeLength = mShaders["vertBlurCS"]->GetBufferSize();

	ThrowIfFailed(md3dDevice->CreateComputePipelineState(&vertBlurPso,
		IID_PPV_ARGS(&mPipelineStateObjects["vertBlur"])));

	// PSO for drawing waves
	D3D12_GRAPHICS_PIPELINE_STATE_DESC wavesRenderPso = trnPsoDesc;
	wavesRenderPso.VS.pShaderBytecode = mShaders["wavesVS"]->GetBufferPointer();
	wavesRenderPso.VS.BytecodeLength = mShaders["wavesVS"]->GetBufferSize();
	//wavesRenderPso.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&wavesRenderPso,
		IID_PPV_ARGS(mPipelineStateObjects["wavesRender"].GetAddressOf())));

	// PSOs for computing waves
	D3D12_COMPUTE_PIPELINE_STATE_DESC wavesDisturbPso{};
	wavesDisturbPso.pRootSignature = mWavesRootSignature.Get();
	wavesDisturbPso.CS.pShaderBytecode = mShaders["wavesDisturbCS"]->GetBufferPointer();
	wavesDisturbPso.CS.BytecodeLength = mShaders["wavesDisturbCS"]->GetBufferSize();
	ThrowIfFailed(md3dDevice->CreateComputePipelineState(&wavesDisturbPso,
		IID_PPV_ARGS(&mPipelineStateObjects["wavesDisturb"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC wavesUpdatePso{};
	wavesUpdatePso.pRootSignature = mWavesRootSignature.Get();
	wavesUpdatePso.CS.pShaderBytecode = mShaders["wavesUpdateCS"]->GetBufferPointer();
	wavesUpdatePso.CS.BytecodeLength = mShaders["wavesUpdateCS"]->GetBufferSize();
	ThrowIfFailed(md3dDevice->CreateComputePipelineState(&wavesUpdatePso,
		IID_PPV_ARGS(&mPipelineStateObjects["wavesUpdate"])));

	// PSO for composition of Sobel filter
	D3D12_GRAPHICS_PIPELINE_STATE_DESC compositePsoDesc = opaquePsoDesc;
	compositePsoDesc.SampleDesc.Count = 1;
	compositePsoDesc.pRootSignature   = mSobelRootSignature.Get();
	compositePsoDesc.DepthStencilState.DepthEnable    = false;
	compositePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	compositePsoDesc.DepthStencilState.DepthFunc      = D3D12_COMPARISON_FUNC_ALWAYS;

	compositePsoDesc.VS.pShaderBytecode = mShaders["compositeVS"]->GetBufferPointer();
	compositePsoDesc.VS.BytecodeLength  = mShaders["compositeVS"]->GetBufferSize();
	compositePsoDesc.PS.pShaderBytecode = mShaders["compositePS"]->GetBufferPointer();
	compositePsoDesc.PS.BytecodeLength  = mShaders["compositePS"]->GetBufferSize();
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&compositePsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["composite"].GetAddressOf())));

	// PSO for Sobel Filter
	D3D12_COMPUTE_PIPELINE_STATE_DESC sobelPso{};
	sobelPso.pRootSignature = mSobelRootSignature.Get();
	sobelPso.CS.pShaderBytecode = mShaders["sobelCS"]->GetBufferPointer();
	sobelPso.CS.BytecodeLength = mShaders["sobelCS"]->GetBufferSize();
	ThrowIfFailed(md3dDevice->CreateComputePipelineState(&sobelPso,
		IID_PPV_ARGS(mPipelineStateObjects["sobel"].GetAddressOf())));
}

void MyGame::BuildFrameResources()
{
	for (int i = 0; i < FRAME_RESOURCES_NUM; ++i)
	{
		mFrameResources.push_back(
			std::make_unique<FrameResource>
			(
				md3dDevice.Get(),
				1,
				static_cast<UINT>(mRenderItems.size()),
				static_cast<UINT>(mMaterials.size())
			)
		);
	}
}

void MyGame::BuildMaterials()
{
	using namespace DirectX;

	auto grass = std::make_unique<Material>();
	grass->Name                = "grass";
	grass->MatCbIndex          = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo       = { 1.0f, 1.0f, 1.0f, 1.0f };
	grass->FresnelR0           = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness           = 0.8f;

	auto water = std::make_unique<Material>();
	water->Name                = "water";
	water->MatCbIndex          = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo       = { 1.0f, 1.0f, 1.0f, 0.5f };
	water->FresnelR0           = XMFLOAT3(0.2f, 0.2f, 0.2f);
	water->Roughness           = 0.0f;

	auto wireFence = std::make_unique<Material>();
	wireFence->Name                = "wireFence";
	wireFence->MatCbIndex          = 2;
	wireFence->DiffuseSrvHeapIndex = 2;
	wireFence->DiffuseAlbedo       = { 1.0f, 1.0f, 1.0f, 1.0f };
	wireFence->FresnelR0           = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wireFence->Roughness           = 0.25f;

	auto treeSprite = std::make_unique<Material>();
	treeSprite->Name			    = "treeSprite";
	treeSprite->MatCbIndex          = 3;
	treeSprite->DiffuseSrvHeapIndex = 3;
	treeSprite->DiffuseAlbedo       = { 1.0f, 1.0f, 1.0f, 1.0f };
	treeSprite->FresnelR0           = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprite->Roughness           = 0.125f;

	mMaterials[grass->Name] = std::move(grass);
	mMaterials[water->Name] = std::move(water);
	mMaterials[wireFence->Name] = std::move(wireFence);
	mMaterials[treeSprite->Name] = std::move(treeSprite);
}

void MyGame::BuildRenderItems()
{
	using namespace DirectX;

	auto wave = std::make_unique<RenderItem>();
	wave->World              = MathHelper::Identity4x4();
	XMStoreFloat4x4(&wave->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	wave->DisplacementMapTexelSize = { 1.0f / mWaves->GetColumnCount(), 1.0f / mWaves->GetRowCount() };
	wave->ObjConstBuffIndex  = 0;
	wave->Material           = mMaterials["water"].get();
	wave->Geometry           = mGeometries["waterGeo"].get();
	wave->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wave->IndexCount         = wave->Geometry->DrawArgs["grid"].IndexCount;
	wave->StartIndexLocation = wave->Geometry->DrawArgs["grid"].StartIndexLocation;
	wave->BaseVertexLocation = wave->Geometry->DrawArgs["grid"].BaseVertexLocation;

	mRenderItemLayer[static_cast<int>(RenderLayer::GpuWaves)].push_back(wave.get());

	auto land = std::make_unique<RenderItem>();
	land->World              = MathHelper::Identity4x4();
	XMStoreFloat4x4(&land->TexTransform, XMMatrixScaling(20.0f, 20.0f, 1.0f));
	land->ObjConstBuffIndex  = 1;
	land->Material           = mMaterials["grass"].get();
	land->Geometry           = mGeometries["landGeo"].get();
	land->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	land->IndexCount         = land->Geometry->DrawArgs["grid"].IndexCount;
	land->StartIndexLocation = land->Geometry->DrawArgs["grid"].StartIndexLocation;
	land->BaseVertexLocation = land->Geometry->DrawArgs["grid"].BaseVertexLocation;

	mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(land.get());

	auto sphere = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&sphere->World, XMMatrixTranslation(3.0f, 5.0f, -9.0f));
	sphere->ObjConstBuffIndex  = 2;
	sphere->Material           = mMaterials["wireFence"].get();
	sphere->Geometry           = mGeometries["sphereGeo"].get();
	sphere->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphere->IndexCount         = sphere->Geometry->DrawArgs["sphere"].IndexCount;
	sphere->StartIndexLocation = sphere->Geometry->DrawArgs["sphere"].StartIndexLocation;
	sphere->BaseVertexLocation = sphere->Geometry->DrawArgs["sphere"].BaseVertexLocation;

	mRenderItemLayer[static_cast<int>(RenderLayer::AlphaTested)].push_back(sphere.get());

	auto treeSprite = std::make_unique<RenderItem>();
	treeSprite->World              = MathHelper::Identity4x4();
	treeSprite->ObjConstBuffIndex  = 3;
	treeSprite->Material           = mMaterials["treeSprite"].get();
	treeSprite->Geometry           = mGeometries["treeSpriteGeo"].get();
	treeSprite->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSprite->IndexCount         = treeSprite->Geometry->DrawArgs["points"].IndexCount;
	treeSprite->StartIndexLocation = treeSprite->Geometry->DrawArgs["points"].StartIndexLocation;
	treeSprite->BaseVertexLocation = treeSprite->Geometry->DrawArgs["points"].BaseVertexLocation;

	mRenderItemLayer[static_cast<int>(RenderLayer::AlphaTestedTreeSprite)].push_back(treeSprite.get());

#ifdef VISUALIZE_NORMAL

	//auto waveNorm = std::make_unique<RenderItem>(*wave);
	//waveNorm->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	//mRenderItemLayer[static_cast<int>(RenderLayer::VisualNorm)].push_back(waveNorm.get());
	auto landNorm = std::make_unique<RenderItem>(*land);
	landNorm->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	mRenderItemLayer[static_cast<int>(RenderLayer::VisualNorm)].push_back(landNorm.get());
	auto sphereNorm = std::make_unique<RenderItem>(*sphere);
	sphereNorm->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	mRenderItemLayer[static_cast<int>(RenderLayer::VisualNorm)].push_back(sphereNorm.get());

	//mRenderItems.push_back(std::move(waveNorm));
	mRenderItems.push_back(std::move(landNorm));
	mRenderItems.push_back(std::move(sphereNorm));

#endif

	mRenderItems.push_back(std::move(wave));
	mRenderItems.push_back(std::move(land));
	mRenderItems.push_back(std::move(sphere));	
	mRenderItems.push_back(std::move(treeSprite));	
}

void MyGame::DrawIndexedRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& renderItems) const
{
	UINT objCbByteSize = CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCbByteSize = CalcConstantBufferByteSize(sizeof(MaterialConstants));
	auto objectCb = mCurrFrameResource->ObjConstBuff->Resource();
	auto matCb = mCurrFrameResource->MatConstBuff->Resource();

	for (const auto & item : renderItems)
	{
		const D3D12_VERTEX_BUFFER_VIEW& vbv = item->Geometry->VertexBufferView();
		const D3D12_INDEX_BUFFER_VIEW& ibv = item->Geometry->IndexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vbv);
		cmdList->IASetIndexBuffer(&ibv);
		cmdList->IASetPrimitiveTopology(item->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvUavDescHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(item->Material->DiffuseSrvHeapIndex, mCbvSrvUavDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCbvAdr = objectCb->GetGPUVirtualAddress();
		objCbvAdr += objCbByteSize * item->ObjConstBuffIndex;
		D3D12_GPU_VIRTUAL_ADDRESS matCbvAdr = matCb->GetGPUVirtualAddress();
		matCbvAdr += matCbByteSize * item->Material->MatCbIndex;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCbvAdr);
		cmdList->SetGraphicsRootConstantBufferView(3, matCbvAdr);

		cmdList->DrawIndexedInstanced(item->IndexCount, 1, 
			item->StartIndexLocation, item->BaseVertexLocation, 0);
	}
}

void MyGame::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& renderItems) const
{
	UINT objCbByteSize = CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCbByteSize = CalcConstantBufferByteSize(sizeof(MaterialConstants));
	auto objectCb = mCurrFrameResource->ObjConstBuff->Resource();
	auto matCb = mCurrFrameResource->MatConstBuff->Resource();

	for (const auto& item : renderItems)
	{
		const D3D12_VERTEX_BUFFER_VIEW& vbv = item->Geometry->VertexBufferView();
		UINT vCnt = vbv.SizeInBytes / vbv.StrideInBytes;
		cmdList->IASetVertexBuffers(0, 1, &vbv);
		cmdList->IASetIndexBuffer(nullptr);
		cmdList->IASetPrimitiveTopology(item->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvUavDescHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(item->Material->DiffuseSrvHeapIndex, mCbvSrvUavDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCbvAdr = objectCb->GetGPUVirtualAddress();
		objCbvAdr += objCbByteSize * item->ObjConstBuffIndex;
		D3D12_GPU_VIRTUAL_ADDRESS matCbvAdr = matCb->GetGPUVirtualAddress();
		matCbvAdr += matCbByteSize * item->Material->MatCbIndex;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCbvAdr);
		cmdList->SetGraphicsRootConstantBufferView(3, matCbvAdr);

		cmdList->DrawInstanced(vCnt, 1, 0, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> MyGame::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap
	(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP
	);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp
	(
		1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP
	);

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap
	(
		2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP
	);

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp
	(
		3,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP
	);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap
	(
		4,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.0f,
		8
	);

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp
	(
		5,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		0.0f,
		8
	);

	return{
		pointWrap,pointClamp,
		linearWrap,linearClamp,
		anisotropicWrap,anisotropicClamp
	};
}

float MyGame::GetHillsHeight(float x, float z)
{
	return 0.3f * (z * sinf (0.1f * x) + x * cosf(0.1f * z));
}

DirectX::XMFLOAT3 MyGame::GetHillsNormal(float x, float z)
{
	using namespace DirectX;

	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}

int WINAPI WinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE prevInstance,
	_In_ PSTR cmdLine,
	_In_ int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		MyGame theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}
