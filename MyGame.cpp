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
	BuildSceneGeometry();
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
}

void MyGame::Draw(const GameTimer& gameTimer)
{
	using namespace DirectX;

	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());

	if (mIsWireframe)
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPipelineStateObjects["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPipelineStateObjects["opaque"].Get()));
	}

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

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

	auto passCb = mCurrFrameResource->PassConstBuff->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCb->GetGPUVirtualAddress());

	DrawRenderItems(mCommandList.Get(), mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)]);

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
		const float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
		const float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

		mRadius += dx - dy;

		mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
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
	//if (GetAsyncKeyState('1') & 0x8000)
	//	mIsWireframe = true;
	//else
	//	mIsWireframe = false;
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
	mMainPassConstBuff.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassConstBuff.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassConstBuff.Lights[0].Intensity = { 0.8f, 0.8f, 0.8f };
	mMainPassConstBuff.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassConstBuff.Lights[1].Intensity = { 0.4f, 0.4f, 0.4f };
	mMainPassConstBuff.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassConstBuff.Lights[2].Intensity = { 0.2f, 0.2f, 0.2f };

	const auto currPassCb = mCurrFrameResource->PassConstBuff.get();
	currPassCb->CopyData(0, mMainPassConstBuff);
}

void MyGame::LoadTextures()
{
	auto brick = std::make_unique<Texture>();
	brick->Name = "bricksTex";
	brick->FileName = L"Textures/bricks.dds";
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

	auto stone = std::make_unique<Texture>();
	stone->Name = "stoneTex";
	stone->FileName = L"Textures/stone.dds";
	{
		DirectX::ResourceUploadBatch upload(md3dDevice.Get());
		upload.Begin();
		ThrowIfFailed(CreateDDSTextureFromFile(
			md3dDevice.Get(), upload,
			stone->FileName.c_str(),
			stone->Resource.GetAddressOf()));
		auto finished = upload.End(mCommandQueue.Get());
		finished.wait();
	}

	auto tile = std::make_unique<Texture>();
	tile->Name = "tileTex";
	tile->FileName = L"Textures/tile.dds";
	{
		DirectX::ResourceUploadBatch upload(md3dDevice.Get());
		upload.Begin();
		ThrowIfFailed(CreateDDSTextureFromFile(
			md3dDevice.Get(), upload,
			tile->FileName.c_str(),
			tile->Resource.GetAddressOf()));
		auto finished = upload.End(mCommandQueue.Get());
		finished.wait();
	}

	mTextures[brick->Name] = std::move(brick);
	mTextures[stone->Name] = std::move(stone);
	mTextures[tile->Name] = std::move(tile);
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
	srvHeapDesc.NumDescriptors = 3;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	// Fill out the heap with actual descriptors.
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto brick = mTextures["bricksTex"]->Resource;
	auto stone = mTextures["stoneTex"]->Resource;
	auto tile = mTextures["tileTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = brick->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = brick->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(brick.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = stone->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = stone->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(stone.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = tile->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tile->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tile.Get(), &srvDesc, hDescriptor);
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

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void MyGame::BuildSceneGeometry()
{
	using namespace DirectX;

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = static_cast<UINT>(box.Vertices.size());
	UINT sphereVertexOffset = gridVertexOffset + static_cast<UINT>(grid.Vertices.size());
	UINT cylinderVertexOffset = sphereVertexOffset + static_cast<UINT>(sphere.Vertices.size());

	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = static_cast<UINT>(box.Indices32.size());
	UINT sphereIndexOffset = gridIndexOffset + static_cast<UINT>(grid.Indices32.size());
	UINT cylinderIndexOffset = sphereIndexOffset + static_cast<UINT>(sphere.Indices32.size());

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = static_cast<UINT>(box.Indices32.size());
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = static_cast<INT>(boxVertexOffset);

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = static_cast<UINT>(grid.Indices32.size());
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = static_cast<INT>(gridVertexOffset);

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = static_cast<UINT>(sphere.Indices32.size());
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = static_cast<INT>(sphereVertexOffset);

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = static_cast<UINT>(cylinder.Indices32.size());
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = static_cast<INT>(cylinderVertexOffset);

	auto totalVertexCount = box.Vertices.size() + grid.Vertices.size() +
		sphere.Vertices.size() + cylinder.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());
	indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());
	indices.insert(indices.end(), sphere.GetIndices16().begin(), sphere.GetIndices16().end());
	indices.insert(indices.end(), cylinder.GetIndices16().begin(), cylinder.GetIndices16().end());

	const UINT vbBytSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

	auto geometry = std::make_unique<MeshGeometry>();
	geometry->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbBytSize, &geometry->VertexBufferCPU));
	CopyMemory(geometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbBytSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geometry->IndexBufferCPU));
	CopyMemory(geometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geometry->VertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbBytSize, geometry->VertexBufferUploader);

	geometry->IndexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geometry->IndexBufferUploader);

	geometry->VertexByteStride = sizeof(Vertex);
	geometry->VertexBufferByteSize = vbBytSize;
	geometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->IndexBufferByteSize = ibByteSize;

	geometry->DrawArgs["box"] = boxSubmesh;
	geometry->DrawArgs["grid"] = gridSubmesh;
	geometry->DrawArgs["sphere"] = sphereSubmesh;
	geometry->DrawArgs["cylinder"] = cylinderSubmesh;

	mGeometries[geometry->Name] = std::move(geometry);
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
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaque{};
	opaque.InputLayout.pInputElementDescs = mInputLayout.data();
	opaque.InputLayout.NumElements        = static_cast<UINT>(mInputLayout.size());
	opaque.pRootSignature                 = mRootSignature.Get();

	opaque.VS.pShaderBytecode             = static_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer());
	opaque.VS.BytecodeLength              = mShaders["standardVS"]->GetBufferSize();
	opaque.PS.pShaderBytecode             = static_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer());
	opaque.PS.BytecodeLength              = mShaders["opaquePS"]->GetBufferSize();

	opaque.RasterizerState                = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaque.RasterizerState.FillMode       = D3D12_FILL_MODE_SOLID;
	opaque.BlendState                     = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaque.DepthStencilState              = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaque.SampleMask                     = UINT_MAX;
	opaque.PrimitiveTopologyType          = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaque.NumRenderTargets               = 1;
	opaque.RTVFormats[0]                  = mBackBufferFormat;

	opaque.SampleDesc.Count               = mSampleCount;
	opaque.SampleDesc.Quality             = 0;

	opaque.DSVFormat                      = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaque,
		IID_PPV_ARGS(mPipelineStateObjects["opaque"].GetAddressOf())));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframe = opaque;
	opaqueWireframe.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframe,
		IID_PPV_ARGS(mPipelineStateObjects["opaque_wireframe"].GetAddressOf())));
}

void MyGame::BuildFrameResources()
{
	for (int i = 0; i < FRAME_RESOURCES_NUM; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, 
			static_cast<UINT>(mRenderItems.size()), static_cast<UINT>(mMaterials.size())));
	}
}

void MyGame::BuildRenderItems()
{
	using namespace DirectX;

	auto box = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&box->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * 
		XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	XMStoreFloat4x4(&box->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	box->ObjConstBuffIndex  = 0;
	box->Mat				= mMaterials["stone0"].get();
	box->Geo				= mGeometries["shapeGeo"].get();
	box->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	box->IndexCount         = box->Geo->DrawArgs["box"].IndexCount;
	box->StartIndexLocation = box->Geo->DrawArgs["box"].StartIndexLocation;
	box->BaseVertexLocation = box->Geo->DrawArgs["box"].BaseVertexLocation;

	mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(box.get());
	mRenderItems.push_back(std::move(box));

	auto grid = std::make_unique<RenderItem>();
	grid->World              = MathHelper::Identity4x4();
	XMStoreFloat4x4(&grid->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	grid->ObjConstBuffIndex  = 1;
	grid->Mat				 = mMaterials["tile0"].get();
	grid->Geo                = mGeometries["shapeGeo"].get();
	grid->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	grid->IndexCount         = grid->Geo->DrawArgs["grid"].IndexCount;
	grid->StartIndexLocation = grid->Geo->DrawArgs["grid"].StartIndexLocation;
	grid->BaseVertexLocation = grid->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(grid.get());
	mRenderItems.push_back(std::move(grid));

	XMMATRIX brickTexTrans = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	UINT objConstBuffIndex = 2;
	for (int i = 0; i < 5; ++i)
	{
		auto lftCyl = std::make_unique<RenderItem>();
		auto rhtCyl = std::make_unique<RenderItem>();
		auto lftSphere = std::make_unique<RenderItem>();
		auto rhtSphere = std::make_unique<RenderItem>();

		const XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		const XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		const XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		const XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&lftCyl->World, leftCylWorld);
		XMStoreFloat4x4(&lftCyl->TexTransform, brickTexTrans);
		lftCyl->ObjConstBuffIndex        = objConstBuffIndex++;
		lftCyl->Mat                      = mMaterials["bricks0"].get();
		lftCyl->Geo                      =	mGeometries["shapeGeo"].get();
		lftCyl->PrimitiveType            = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lftCyl->IndexCount               = lftCyl->Geo->DrawArgs["cylinder"].IndexCount;
		lftCyl->StartIndexLocation       = lftCyl->Geo->DrawArgs["cylinder"].StartIndexLocation;
		lftCyl->BaseVertexLocation       = lftCyl->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rhtCyl->World, rightCylWorld);
		XMStoreFloat4x4(&rhtCyl->TexTransform, brickTexTrans);
		rhtCyl->ObjConstBuffIndex        = objConstBuffIndex++;
		rhtCyl->Mat                      = mMaterials["bricks0"].get();
		rhtCyl->Geo                      = mGeometries["shapeGeo"].get();
		rhtCyl->PrimitiveType            = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rhtCyl->IndexCount               = rhtCyl->Geo->DrawArgs["cylinder"].IndexCount;
		rhtCyl->StartIndexLocation       = rhtCyl->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rhtCyl->BaseVertexLocation       = rhtCyl->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&lftSphere->World, leftSphereWorld);
		lftSphere->TexTransform          = MathHelper::Identity4x4();
		lftSphere->ObjConstBuffIndex     = objConstBuffIndex++;
		lftSphere->Mat                   = mMaterials["stone0"].get();
		lftSphere->Geo                   = mGeometries["shapeGeo"].get();
		lftSphere->PrimitiveType         = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lftSphere->IndexCount            = lftSphere->Geo->DrawArgs["sphere"].IndexCount;
		lftSphere->StartIndexLocation    = lftSphere->Geo->DrawArgs["sphere"].StartIndexLocation;
		lftSphere->BaseVertexLocation    = lftSphere->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rhtSphere->World, rightSphereWorld);
		rhtSphere->TexTransform          = MathHelper::Identity4x4();
		rhtSphere->ObjConstBuffIndex     = objConstBuffIndex++;
		rhtSphere->Mat                   = mMaterials["stone0"].get();
		rhtSphere->Geo                   = mGeometries["shapeGeo"].get();
		rhtSphere->PrimitiveType         = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rhtSphere->IndexCount            = rhtSphere->Geo->DrawArgs["sphere"].IndexCount;
		rhtSphere->StartIndexLocation    = rhtSphere->Geo->DrawArgs["sphere"].StartIndexLocation;
		rhtSphere->BaseVertexLocation    = rhtSphere->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(lftCyl.get());
		mRenderItems.push_back(std::move(lftCyl));
		mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(rhtCyl.get());
		mRenderItems.push_back(std::move(rhtCyl));
		mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(lftSphere.get());
		mRenderItems.push_back(std::move(lftSphere));
		mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(rhtSphere.get());
		mRenderItems.push_back(std::move(rhtSphere));
	}
}

void MyGame::BuildMaterials()
{
	using namespace DirectX;

	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCbIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(Colors::White);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCbIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(Colors::White);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCbIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(Colors::White);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.2f;

	mMaterials[bricks0->Name] = std::move(bricks0);
	mMaterials[stone0->Name] = std::move(stone0);
	mMaterials[tile0->Name] = std::move(tile0);
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
