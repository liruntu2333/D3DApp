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
	const float* gRenderTargetCleanValue = DirectX::Colors::LightSkyBlue.f;
	const auto gStaticSamplers = std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6>
	{
		CD3DX12_STATIC_SAMPLER_DESC
		(
			0,
			D3D12_FILTER_MIN_MAG_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP
		),

		CD3DX12_STATIC_SAMPLER_DESC
		(
			1,
			D3D12_FILTER_MIN_MAG_MIP_POINT,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP
		),

		CD3DX12_STATIC_SAMPLER_DESC
		(
			2,
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP
		),

		CD3DX12_STATIC_SAMPLER_DESC
		(
			3,
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP
		),

		CD3DX12_STATIC_SAMPLER_DESC
		(
			4,
			D3D12_FILTER_ANISOTROPIC,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,
			0.0f,
			8
		),

		CD3DX12_STATIC_SAMPLER_DESC
		(
			5,
			D3D12_FILTER_ANISOTROPIC,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
			0.0f,
			8
		),
	};
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

	// Check device supported msaa sample count.
	for (mSampleCount = SAMPLE_COUNT_MAX; mSampleCount > 1; mSampleCount--)
	{
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = { mBackBufferFormat, mSampleCount };
		if (FAILED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))))
			continue;

		if (levels.NumQualityLevels > 0)
			break;
	}

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildRoomGeometry();
	BuildSkullGeometry();
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

	CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

	// Create an MSAA render target.
	D3D12_RESOURCE_DESC msaaRTDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		mBackBufferFormat,
		mClientWidth, mClientHeight,
		1, 1, mSampleCount);
	msaaRTDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE rtClearValue{};
	rtClearValue.Format = mBackBufferFormat;
	memcpy(rtClearValue.Color, gRenderTargetCleanValue, sizeof(float) * 4);

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&msaaRTDesc,
		D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
		&rtClearValue,
		IID_PPV_ARGS(mMsaaRenderTarget.GetAddressOf())));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = mBackBufferFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;

	md3dDevice->CreateRenderTargetView(mMsaaRenderTarget.Get(), &rtvDesc,
		mMsaaRTVDescHeap->GetCPUDescriptorHandleForHeapStart());

	mMsaaRenderTarget->SetName(L"MSAA Render Target");

	// Create an MSAA depth stencil view.
	D3D12_RESOURCE_DESC msaaDSDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		mDepthStencilFormat,
		mClientWidth, mClientHeight,
		1, 1, mSampleCount);
	msaaDSDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE dsClearValue{};
	dsClearValue.Format = mDepthStencilFormat;
	dsClearValue.DepthStencil.Depth = 1.0f;
	dsClearValue.DepthStencil.Stencil = 0;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&msaaDSDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&dsClearValue,
		IID_PPV_ARGS(mMsaaDepthStencil.GetAddressOf())));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = mDepthStencilFormat;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;

	md3dDevice->CreateDepthStencilView(mMsaaDepthStencil.Get(),
		&dsvDesc, mMsaaDSVDescHeap->GetCPUDescriptorHandleForHeapStart());

	mMsaaDepthStencil->SetName(L"MSAA Depth Stencil");

	const XMMATRIX proj = XMMatrixPerspectiveFovLH(
		0.25f * MathHelper::Pi, AspectRatio(),
		1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, proj);
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
		if (HANDLE handle = 
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
	UpdateReflectedPassConstBuffs(gameTimer);
}

void MyGame::Draw(const GameTimer& gameTimer)
{
	using namespace DirectX;

	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPipelineStateObjects["opaque"].Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Clear MSAA render target & depth stencil.
	{
		D3D12_RESOURCE_BARRIER barrier = 
			CD3DX12_RESOURCE_BARRIER::Transition(mMsaaRenderTarget.Get(),
			D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);

		mCommandList->ResourceBarrier(1, &barrier);
	}

	auto hRtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mMsaaRTVDescHeap->GetCPUDescriptorHandleForHeapStart());
	auto hDsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mMsaaDSVDescHeap->GetCPUDescriptorHandleForHeapStart());
	mCommandList->ClearRenderTargetView(hRtv, gRenderTargetCleanValue, 0, nullptr);
	mCommandList->ClearDepthStencilView(hDsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1,&hRtv,
		true, &hDsv);

	ID3D12DescriptorHeap* heaps[] = {mSrvDescriptorHeap.Get()};
	mCommandList->SetDescriptorHeaps(_countof(heaps), heaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	UINT passCbByteSize = CalcConstantBufferByteSize(sizeof(PassConstants));
	auto mainPassBuffAdr = mCurrFrameResource->PassConstBuff->Resource()->GetGPUVirtualAddress();
	auto reflPassBuffAdr = mainPassBuffAdr + passCbByteSize;

	// opaque objects pass
	mCommandList->SetGraphicsRootConstantBufferView(2, mainPassBuffAdr);
	DrawRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)]);

	// mark mirror into stencil pass
	mCommandList->OMSetStencilRef(1);
	mCommandList->SetPipelineState(mPipelineStateObjects["markStencilMirrors"].Get());
	DrawRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::Mirrors)]);

	// reflection in mirror pass
	mCommandList->SetGraphicsRootConstantBufferView(2, reflPassBuffAdr);
	mCommandList->SetPipelineState(mPipelineStateObjects["drawStencilReflections"].Get());
	DrawRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::Reflected)]);

	mCommandList->SetPipelineState(mPipelineStateObjects["reflTrn"].Get());
	DrawRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::ReflectedTransparent)]);

	// transparent mirror pass
	mCommandList->SetGraphicsRootConstantBufferView(2, mainPassBuffAdr);
	mCommandList->OMSetStencilRef(0);
	mCommandList->SetPipelineState(mPipelineStateObjects["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::Transparent)]);

	// shadow pass
	mCommandList->SetPipelineState(mPipelineStateObjects["shadow"].Get());
	DrawRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::Shadow)]);

	{
		CD3DX12_RESOURCE_BARRIER barriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(mMsaaRenderTarget.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
		};
		mCommandList->ResourceBarrier(_countof(barriers), barriers);
	}

	mCommandList->ResolveSubresource(CurrentBackBuffer(), 0,
		mMsaaRenderTarget.Get(), 0, mBackBufferFormat);

	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PRESENT);
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
	using namespace DirectX;

	const float dt = gameTimer.DeltaTime();

	if (GetAsyncKeyState('A') & 0x8000)
		mSkullTranslation.x -= 1.0f * dt;

	if (GetAsyncKeyState('D') & 0x8000)
		mSkullTranslation.x += 1.0f * dt;

	if (GetAsyncKeyState('W') & 0x8000)
		mSkullTranslation.y += 1.0f * dt;

	if (GetAsyncKeyState('S') & 0x8000)
		mSkullTranslation.y -= 1.0f * dt;

	mSkullTranslation.y = MathHelper::Max(mSkullTranslation.y, 0.0f);

	XMMATRIX skullRotation = XMMatrixRotationY(0.5f * XM_PI);
	XMMATRIX skullScale = XMMatrixScaling(0.45f, 0.45f, 0.45f);
	XMMATRIX skullOffset = XMMatrixTranslation(mSkullTranslation.x, mSkullTranslation.y, mSkullTranslation.z);
	XMMATRIX skullWorld = skullRotation * skullScale * skullOffset;
	XMStoreFloat4x4(&mSkullRenderItem->World, skullWorld);

	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	XMMATRIX reflect = XMMatrixReflect(mirrorPlane);
	XMStoreFloat4x4(&mReflectedSkullRenderItem->World, skullWorld * reflect);

	XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMVECTOR toMainLight = -XMLoadFloat3(&mMainPassConstBuff.Lights[0].Direction);
	XMMATRIX shadow = XMMatrixShadow(shadowPlane, toMainLight);
	XMMATRIX shadowOffset = XMMatrixTranslation(0.0f, 0.001f, 0.0f);
	XMStoreFloat4x4(&mShadowedSkullRenderItem->World, skullWorld * shadow * shadowOffset);
	XMStoreFloat4x4(&mReflectedShadowedSkullRenderItem->World, skullWorld * shadow * shadowOffset * reflect);

	mSkullRenderItem->NumFrameDirty = FRAME_RESOURCES_NUM;
	mReflectedSkullRenderItem->NumFrameDirty = FRAME_RESOURCES_NUM;
	mShadowedSkullRenderItem->NumFrameDirty = FRAME_RESOURCES_NUM;
	mReflectedShadowedSkullRenderItem->NumFrameDirty = FRAME_RESOURCES_NUM;
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
			const XMMATRIX texTrans = XMLoadFloat4x4(&ri->TexTransform);

			ObjectConstants objConst;
			XMStoreFloat4x4(&objConst.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConst.TexTransform, XMMatrixTranspose(texTrans));

			currObjCb->CopyData(ri->ObjConstBuffIndex, objConst);

			ri->NumFrameDirty--;
		}
	}
}

void MyGame::UpdateMaterialConstBuffs(const GameTimer& gameTimer) const
{
	using namespace DirectX;

	auto currMatCb = mCurrFrameResource->MatConstBuff.get();
	for (const auto& pair : mMaterials)
	{
		Material* material = pair.second.get();
		assert(material && "material is nullptr");
		if (material->NumFrameDirty > 0)
		{
			MaterialConstants matConst;
			matConst.DiffuseAlbedo = material->DiffuseAlbedo;
			matConst.FresnelR0 = material->FresnelR0;
			matConst.Roughness = material->Roughness;
			XMStoreFloat4x4(&matConst.MatTransform, XMMatrixTranspose(XMLoadFloat4x4(&material->MatTransform)));

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

	XMVECTOR detView = XMMatrixDeterminant(view);
	XMVECTOR detProj = XMMatrixDeterminant(proj);
	XMVECTOR detViewProj = XMMatrixDeterminant(viewProj);

	const XMMATRIX invView = XMMatrixInverse(&detView, view);
	const XMMATRIX invProj = XMMatrixInverse(&detProj, proj);
	const XMMATRIX invViewProj = XMMatrixInverse(&detViewProj, viewProj);

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
	mMainPassConstBuff.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassConstBuff.Lights[0].Intensity = { 0.6f, 0.6f, 0.6f };
	mMainPassConstBuff.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassConstBuff.Lights[1].Intensity = { 0.3f, 0.3f, 0.3f };
	mMainPassConstBuff.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassConstBuff.Lights[2].Intensity = { 0.15f, 0.15f, 0.15f };

	const auto currPassCb = mCurrFrameResource->PassConstBuff.get();
	currPassCb->CopyData(0, mMainPassConstBuff);
}

void MyGame::UpdateReflectedPassConstBuffs(const GameTimer& gameTimer)
{
	using namespace DirectX;

	mReflectedPassConstBuff = mMainPassConstBuff;
	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	XMMATRIX reflect = XMMatrixReflect(mirrorPlane);

	for (int i = 0; i < 3; ++i)
	{
		XMVECTOR lightDir = XMLoadFloat3(&mMainPassConstBuff.Lights[i].Direction);
		XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, reflect);
		XMStoreFloat3(&mReflectedPassConstBuff.Lights[i].Direction, reflectedLightDir);
	}

	auto currPassCb = mCurrFrameResource->PassConstBuff.get();
	currPassCb->CopyData(1, mReflectedPassConstBuff);
}

void MyGame::LoadTextures()
{
	auto brick = std::make_unique<Texture>();
	brick->Name = "bricksTex";
	brick->FileName = L"Textures/bricks2.dds";
	{
		DirectX::ResourceUploadBatch upload(md3dDevice.Get());
		upload.Begin();
		ThrowIfFailed(CreateDDSTextureFromFile(
			md3dDevice.Get(), upload,
			brick->FileName.c_str(),
			brick->Resource.GetAddressOf()));
		auto finished = upload.End(mCommandQueue.Get());
		finished.wait();
	}

	auto checkBoard = std::make_unique<Texture>();
	checkBoard->Name = "checkboardTex";
	checkBoard->FileName = L"Textures/tile.dds";
	{
		DirectX::ResourceUploadBatch upload(md3dDevice.Get());
		upload.Begin();
		ThrowIfFailed(CreateDDSTextureFromFile(
			md3dDevice.Get(), upload,
			checkBoard->FileName.c_str(),
			checkBoard->Resource.GetAddressOf()));
		auto finished = upload.End(mCommandQueue.Get());
		finished.wait();
	}

	auto ice = std::make_unique<Texture>();
	ice->Name = "iceTex";
	ice->FileName = L"Textures/ice.dds";
	{
		DirectX::ResourceUploadBatch upload(md3dDevice.Get());
		upload.Begin();
		ThrowIfFailed(CreateDDSTextureFromFile(
			md3dDevice.Get(), upload,
			ice->FileName.c_str(),
			ice->Resource.GetAddressOf()));
		auto finished = upload.End(mCommandQueue.Get());
		finished.wait();
	}

	auto white1x1 = std::make_unique<Texture>();
	white1x1->Name = "white1x1Tex";
	white1x1->FileName = L"Textures/white1x1.dds";
	{
		DirectX::ResourceUploadBatch upload(md3dDevice.Get());
		upload.Begin();
		ThrowIfFailed(CreateDDSTextureFromFile(
			md3dDevice.Get(), upload,
			white1x1->FileName.c_str(),
			white1x1->Resource.GetAddressOf()));
		auto finished = upload.End(mCommandQueue.Get());
		finished.wait();
	}

	mTextures[brick->Name] = std::move(brick);
	mTextures[checkBoard->Name] = std::move(checkBoard);
	mTextures[ice->Name] = std::move(ice);
	mTextures[white1x1->Name] = std::move(white1x1);
}

void MyGame::BuildDescriptorHeaps()
{
	// Create descriptor heaps for MSAA render target views and depth stencil views.
	D3D12_DESCRIPTOR_HEAP_DESC rtvDescHeapDesc{};
	rtvDescHeapDesc.NumDescriptors = 1;
	rtvDescHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvDescHeapDesc,
		IID_PPV_ARGS(mMsaaRTVDescHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvDescHeapDesc{};
	dsvDescHeapDesc.NumDescriptors = 1;
	dsvDescHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dsvDescHeapDesc,
		IID_PPV_ARGS(mMsaaDSVDescHeap.GetAddressOf())));

	// Create the SRV heap.
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 4;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	// Fill out the heap with actual descriptors.
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto brick = mTextures["bricksTex"]->Resource;
	auto checkBoard = mTextures["checkboardTex"]->Resource;
	auto ice = mTextures["iceTex"]->Resource;
	auto white1x1 = mTextures["white1x1Tex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = brick->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = brick->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(brick.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = checkBoard->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = checkBoard->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(checkBoard.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = ice->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = ice->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(ice.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = white1x1->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = white1x1->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(white1x1.Get(), &srvDesc, hDescriptor);
}

void MyGame::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTbl;
	texTbl.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[4];
	slotRootParameter[0].InitAsDescriptorTable(1, &texTbl, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	const CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter, 
		static_cast<UINT>(gStaticSamplers.size()), gStaticSamplers.data(),
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

void MyGame::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = LoadBinary(L"CompiledShaders/defaultVS.cso");
	mShaders["opaquePS"] = LoadBinary(L"CompiledShaders/defaultPS.cso");
	//mShaders["alphaTestedPS"] = LoadBinary(L"CompiledShaders/alphaTestedPS.cso");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void MyGame::BuildRoomGeometry()
{
	using namespace DirectX;

	std::array<Vertex, 20> vertices =
	{
		Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0 
		Vertex(-3.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
		Vertex(7.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
		Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

		Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
		Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

		Vertex(6.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8 
		Vertex(6.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
		Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

		Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
		Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f),
		Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f),

		// Mirror
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 16
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(6.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
		Vertex(6.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
	};

	std::array<std::int16_t, 30> indices =
	{
		0, 1, 2,
		0, 2, 3,

		4, 5, 6,
		4, 6, 7,

		8, 9, 10,
		8, 10, 11,

		12, 13, 14,
		12, 14, 15,

		// Mirror
		16, 17, 18,
		16, 18, 19
	};

	SubmeshGeometry floorSubmesh;
	floorSubmesh.IndexCount = 6;
	floorSubmesh.StartIndexLocation = 0;
	floorSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry wallSubmesh;
	wallSubmesh.IndexCount = 18;
	wallSubmesh.StartIndexLocation = 6;
	wallSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry mirrorSubmesh;
	mirrorSubmesh.IndexCount = 6;
	mirrorSubmesh.StartIndexLocation = 24;
	mirrorSubmesh.BaseVertexLocation = 0;

	constexpr UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	constexpr UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "roomGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["floor"] = floorSubmesh;
	geo->DrawArgs["wall"] = wallSubmesh;
	geo->DrawArgs["mirror"] = mirrorSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void MyGame::BuildSkullGeometry()
{
	std::ifstream fin("Models/skull.txt");

	if (!fin)
	{
		MessageBox(nullptr, L"Models/skull.txt not found.", nullptr, 0);
		return;
	}

	UINT vCount = 0;
	UINT tCount = 0;
	std::string ignore;

	fin >> ignore >> vCount;
	fin >> ignore >> tCount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex> vertices(vCount);
	for (UINT i = 0; i < vCount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
	}

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tCount);
	for (UINT i = 0; i < tCount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);

	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["skull"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void MyGame::BuildPipelineStateObjects()
{
	// PSO for opaque objects.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDec{};
	opaquePsoDec.InputLayout.pInputElementDescs = mInputLayout.data();
	opaquePsoDec.InputLayout.NumElements        = static_cast<UINT>(mInputLayout.size());
	opaquePsoDec.pRootSignature                 = mRootSignature.Get();

	opaquePsoDec.VS.pShaderBytecode             = static_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer());
	opaquePsoDec.VS.BytecodeLength              = mShaders["standardVS"]->GetBufferSize();
	opaquePsoDec.PS.pShaderBytecode             = static_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer());
	opaquePsoDec.PS.BytecodeLength              = mShaders["opaquePS"]->GetBufferSize();

	opaquePsoDec.RasterizerState                = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDec.BlendState                     = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDec.DepthStencilState              = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDec.SampleMask                     = UINT_MAX;
	opaquePsoDec.PrimitiveTopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDec.NumRenderTargets               = 1;
	opaquePsoDec.RTVFormats[0]                  = mBackBufferFormat;

	opaquePsoDec.SampleDesc.Count               = mSampleCount;
	opaquePsoDec.SampleDesc.Quality             = 0;

	opaquePsoDec.DSVFormat                      = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDec,
		IID_PPV_ARGS(mPipelineStateObjects["opaque"].GetAddressOf())));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC trnPsoDesc = opaquePsoDec;

	// PSO for transparent objects.
	CD3DX12_BLEND_DESC trnBlendDesc(D3D12_DEFAULT);
	trnBlendDesc.RenderTarget[0].BlendEnable = true;
	trnBlendDesc.RenderTarget[0].SrcBlend    = D3D12_BLEND_SRC_ALPHA;
	trnBlendDesc.RenderTarget[0].DestBlend   = D3D12_BLEND_INV_SRC_ALPHA;

	trnPsoDesc.BlendState = trnBlendDesc;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&trnPsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["transparent"].GetAddressOf())));

	// PSO for stencil mirror.
	CD3DX12_DEPTH_STENCIL_DESC mirrorDsDesc(D3D12_DEFAULT);
	mirrorDsDesc.DepthWriteMask          = D3D12_DEPTH_WRITE_MASK_ZERO;
	mirrorDsDesc.StencilEnable           = true;
	mirrorDsDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDsDesc.BackFace.StencilPassOp  = D3D12_STENCIL_OP_REPLACE;	// does not matter

	CD3DX12_BLEND_DESC mirrorBlendDesc(D3D12_DEFAULT);
	mirrorBlendDesc.RenderTarget[0].RenderTargetWriteMask = 0;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC markMirrorPsoDesc = opaquePsoDec;
	markMirrorPsoDesc.BlendState        = mirrorBlendDesc;
	markMirrorPsoDesc.DepthStencilState = mirrorDsDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&markMirrorPsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["markStencilMirrors"].GetAddressOf())));

	// PSO for stencil reflection.
	CD3DX12_DEPTH_STENCIL_DESC reflectionDsDec(D3D12_DEFAULT);
	reflectionDsDec.StencilEnable         = true;
	reflectionDsDec.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	reflectionDsDec.BackFace.StencilFunc  = D3D12_COMPARISON_FUNC_EQUAL; // does not matter

	D3D12_GRAPHICS_PIPELINE_STATE_DESC reflectionsPsoDesc = opaquePsoDec;
	reflectionsPsoDesc.RasterizerState.FrontCounterClockwise = true;
	reflectionsPsoDesc.DepthStencilState = reflectionDsDec;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&reflectionsPsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["drawStencilReflections"].GetAddressOf())));

	// PSO for shadow object.
	CD3DX12_DEPTH_STENCIL_DESC shadowDsDesc(D3D12_DEFAULT);
	shadowDsDesc.StencilEnable           = true;
	shadowDsDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDsDesc.FrontFace.StencilFunc   = D3D12_COMPARISON_FUNC_EQUAL;
	shadowDsDesc.BackFace.StencilPassOp  = D3D12_STENCIL_OP_INCR; // does not matter
	shadowDsDesc.BackFace.StencilFunc    = D3D12_COMPARISON_FUNC_EQUAL; // does not mater

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = trnPsoDesc;
	shadowPsoDesc.DepthStencilState = shadowDsDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["shadow"].GetAddressOf())));

	// PSO for reflected shadow.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC reflShadowPsoDesc = shadowPsoDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&reflShadowPsoDesc,
		IID_PPV_ARGS(mPipelineStateObjects["reflTrn"].GetAddressOf())));
}

void MyGame::BuildFrameResources()
{
	for (int i = 0; i < FRAME_RESOURCES_NUM; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(
			md3dDevice.Get(), 
			2, // need 2 pass constant buffers, one for main pass, one for reflect pass
			static_cast<UINT>(mRenderItems.size()), 
			static_cast<UINT>(mMaterials.size())));
	}
}

void MyGame::BuildRenderItems()
{
	using namespace DirectX;

	auto floor = std::make_unique<RenderItem>();
	floor->World              = MathHelper::Identity4x4();
	floor->TexTransform       = MathHelper::Identity4x4();
	floor->ObjConstBuffIndex  = 0;
	floor->Mat				  = mMaterials["checkerTile"].get();
	floor->Geo				  = mGeometries["roomGeo"].get();
	floor->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	floor->IndexCount         = floor->Geo->DrawArgs["floor"].IndexCount;
	floor->StartIndexLocation = floor->Geo->DrawArgs["floor"].StartIndexLocation;
	floor->BaseVertexLocation = floor->Geo->DrawArgs["floor"].BaseVertexLocation;

	mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(floor.get());

	auto wall = std::make_unique<RenderItem>();
	wall->World              = MathHelper::Identity4x4();
	wall->TexTransform       = MathHelper::Identity4x4();
	wall->ObjConstBuffIndex  = 1;
	wall->Mat				 = mMaterials["bricks"].get();
	wall->Geo                = mGeometries["roomGeo"].get();
	wall->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wall->IndexCount         = wall->Geo->DrawArgs["wall"].IndexCount;
	wall->StartIndexLocation = wall->Geo->DrawArgs["wall"].StartIndexLocation;
	wall->BaseVertexLocation = wall->Geo->DrawArgs["wall"].BaseVertexLocation;

	mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(wall.get());

	auto skull = std::make_unique<RenderItem>();
	skull->World              = MathHelper::Identity4x4();
	skull->TexTransform       = MathHelper::Identity4x4();
	skull->ObjConstBuffIndex  = 2;
	skull->Mat                = mMaterials["skullMat"].get();
	skull->Geo                = mGeometries["skullGeo"].get();
	skull->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skull->IndexCount         = skull->Geo->DrawArgs["skull"].IndexCount;
	skull->StartIndexLocation = skull->Geo->DrawArgs["skull"].StartIndexLocation;
	skull->BaseVertexLocation = skull->Geo->DrawArgs["skull"].BaseVertexLocation;

	mSkullRenderItem = skull.get();
	mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(skull.get());

	auto reflectedSkull = std::make_unique<RenderItem>();
	*reflectedSkull = *skull;
	reflectedSkull->ObjConstBuffIndex = 3;
	mReflectedSkullRenderItem = reflectedSkull.get();
	mRenderItemLayer[static_cast<int>(RenderLayer::Reflected)].push_back(reflectedSkull.get());

	auto shadowedSkull = std::make_unique<RenderItem>();
	*shadowedSkull = *skull;
	shadowedSkull->ObjConstBuffIndex = 4;
	shadowedSkull->Mat = mMaterials["shadowMat"].get();
	mShadowedSkullRenderItem = shadowedSkull.get();
	mRenderItemLayer[static_cast<int>(RenderLayer::Shadow)].push_back(shadowedSkull.get());

	auto mirror = std::make_unique<RenderItem>();
	mirror->World = MathHelper::Identity4x4();
	mirror->TexTransform = MathHelper::Identity4x4();
	mirror->ObjConstBuffIndex = 5;
	mirror->Mat = mMaterials["iceMirror"].get();
	mirror->Geo = mGeometries["roomGeo"].get();
	mirror->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	mirror->IndexCount = mirror->Geo->DrawArgs["mirror"].IndexCount;
	mirror->StartIndexLocation = mirror->Geo->DrawArgs["mirror"].StartIndexLocation;
	mirror->BaseVertexLocation = mirror->Geo->DrawArgs["mirror"].BaseVertexLocation;
	mRenderItemLayer[static_cast<int>(RenderLayer::Mirrors)].push_back(mirror.get());
	mRenderItemLayer[static_cast<int>(RenderLayer::Transparent)].push_back(mirror.get());

	auto reflectedFloor = std::make_unique<RenderItem>();
	*reflectedFloor = *floor;
	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
	XMMATRIX reflect = XMMatrixReflect(mirrorPlane);
	XMStoreFloat4x4(&reflectedFloor->World, XMMatrixIdentity() * reflect);
	reflectedFloor->ObjConstBuffIndex = 6;
	mRenderItemLayer[static_cast<int>(RenderLayer::Reflected)].push_back(reflectedFloor.get());

	auto reflectedShadowedSkull = std::make_unique<RenderItem>();
	*reflectedShadowedSkull = *shadowedSkull;
	reflectedShadowedSkull->ObjConstBuffIndex = 7;
	mReflectedShadowedSkullRenderItem = reflectedShadowedSkull.get();
	mRenderItemLayer[static_cast<int>(RenderLayer::ReflectedTransparent)].push_back(reflectedShadowedSkull.get());

	mRenderItems.push_back(std::move(floor));
	mRenderItems.push_back(std::move(wall));
	mRenderItems.push_back(std::move(skull));
	mRenderItems.push_back(std::move(reflectedSkull));
	mRenderItems.push_back(std::move(shadowedSkull));
	mRenderItems.push_back(std::move(mirror));
	mRenderItems.push_back(std::move(reflectedFloor));
	mRenderItems.push_back(std::move(reflectedShadowedSkull));
}

void MyGame::BuildMaterials()
{
	using namespace DirectX;

	auto bricks = std::make_unique<Material>();
	bricks->Name                = "bricks";
	bricks->MatCbIndex          = 0;
	bricks->DiffuseSrvHeapIndex = 0;
	bricks->DiffuseAlbedo       = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks->FresnelR0           = XMFLOAT3(0.05f, 0.05f, 0.05f);
	bricks->Roughness           = 0.25f;

	auto checkerTile = std::make_unique<Material>();
	checkerTile->Name                = "checkerTile";
	checkerTile->MatCbIndex          = 1;
	checkerTile->DiffuseSrvHeapIndex = 1;
	checkerTile->DiffuseAlbedo       = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	checkerTile->FresnelR0           = XMFLOAT3(0.07f, 0.07f, 0.07f);
	checkerTile->Roughness           = 0.3f;

	auto iceMirror = std::make_unique<Material>();
	iceMirror->Name                = "iceMirror";
	iceMirror->MatCbIndex          = 2;
	iceMirror->DiffuseSrvHeapIndex = 2;
	iceMirror->DiffuseAlbedo       = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
	iceMirror->FresnelR0           = XMFLOAT3(0.1f, 0.1f, 0.1f);
	iceMirror->Roughness           = 0.5f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name                = "skullMat";
	skullMat->MatCbIndex          = 3;
	skullMat->DiffuseSrvHeapIndex = 3;
	skullMat->DiffuseAlbedo       = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skullMat->FresnelR0           = XMFLOAT3(0.05f, 0.05f, 0.05f);
	skullMat->Roughness           = 0.3f;

	auto shadowMat = std::make_unique<Material>();
	shadowMat->Name                = "shadowMat";
	shadowMat->MatCbIndex          = 4;
	shadowMat->DiffuseSrvHeapIndex = 3;
	shadowMat->DiffuseAlbedo       = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
	shadowMat->FresnelR0           = XMFLOAT3(0.001f, 0.001f, 0.001f);
	shadowMat->Roughness           = 0.0f;

	mMaterials[bricks->Name]      = std::move(bricks);
	mMaterials[checkerTile->Name] = std::move(checkerTile);
	mMaterials[iceMirror->Name]   = std::move(iceMirror);
	mMaterials[skullMat->Name]    = std::move(skullMat);
	mMaterials[shadowMat->Name]   = std::move(shadowMat);
}

void MyGame::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& renderItems) const
{
	UINT objCbByteSize = CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCbByteSize = CalcConstantBufferByteSize(sizeof(MaterialConstants));
	auto objectCb = mCurrFrameResource->ObjConstBuff->Resource();
	auto matCb = mCurrFrameResource->MatConstBuff->Resource();

	for (const auto & item : renderItems)
	{
		const D3D12_VERTEX_BUFFER_VIEW& vbv = item->Geo->VertexBufferView();
		const D3D12_INDEX_BUFFER_VIEW& ibv = item->Geo->IndexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vbv);
		cmdList->IASetIndexBuffer(&ibv);
		cmdList->IASetPrimitiveTopology(item->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(item->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCbvAdr = objectCb->GetGPUVirtualAddress();
		objCbvAdr += objCbByteSize * item->ObjConstBuffIndex;
		D3D12_GPU_VIRTUAL_ADDRESS matCbvAdr = matCb->GetGPUVirtualAddress();
		matCbvAdr += matCbByteSize * item->Mat->MatCbIndex;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		cmdList->SetGraphicsRootConstantBufferView(1, objCbvAdr);
		cmdList->SetGraphicsRootConstantBufferView(3, matCbvAdr);

		cmdList->DrawIndexedInstanced(item->IndexCount, 1,
			item->StartIndexLocation, item->BaseVertexLocation, 0);
	}
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
