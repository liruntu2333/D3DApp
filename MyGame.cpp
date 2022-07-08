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

// ReSharper disable once CppVariableCanBeMadeConstexpr
const float MyGame::RENDER_TARGET_CLEAN_VALUE[4] = {0.000000000f, 0.749019623f, 1.000000000f, 1.000000000f};

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
	for (mSampleCount = DX::SAMPLE_COUNT_MAX; mSampleCount > 1; mSampleCount--)
	{
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS levels = { mBackBufferFormat, mSampleCount };
		if (FAILED(md3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &levels, sizeof(levels))))
			continue;

		if (levels.NumQualityLevels > 0)
			break;
	}

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildLandGeometry();
	BuildWavesGeometryBuffers();
	BuildMaterials();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
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
	memcpy(rtClearValue.Color, RENDER_TARGET_CLEAN_VALUE, sizeof(float) * 4);

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
		0.25f * DX::MathHelper::Pi, AspectRatio(),
		1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, proj);
}

void MyGame::Update(const GameTimer& gameTimer)
{
	OnKeyboardInput(gameTimer);
	UpdateCamera(gameTimer);

	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % DX::FRAME_RESOURCES_NUM;
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
	UpdateObjectConstBuffs(gameTimer);
	UpdateMaterialConstBuffs(gameTimer);
	UpdateMainPassConstBuffs(gameTimer);
	UpdateWaves(gameTimer);
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
	mCommandList->ClearRenderTargetView(hRtv, RENDER_TARGET_CLEAN_VALUE, 0, nullptr);
	mCommandList->ClearDepthStencilView(hDsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1,&hRtv,
		true, &hDsv);

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

		mPhi = DX::MathHelper::Clamp(mPhi, 0.1f, DX::MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		const float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
		const float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

		mRadius += dx - dy;

		mRadius = DX::MathHelper::Clamp(mRadius, 3.0f, 15.0f);
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

	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;

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

void MyGame::UpdateObjectConstBuffs(const GameTimer& gameTimer) const
{
	using namespace DirectX;

	const auto currObjCb = mCurrFrameResource->ObjConstBuff.get();
	for (auto & ri : mRenderItems)
	{
		if (ri->NumFrameDirty > 0)
		{
			const XMMATRIX world = XMLoadFloat4x4(&ri->World);

			DX::ObjectConstants objConst;
			XMStoreFloat4x4(&objConst.world, XMMatrixTranspose(world));

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
		DX::Material* material = pair.second.get();
		assert(material && "material is nullptr");
		if (material->NumFrameDirty > 0)
		{
			DX::MaterialConstants matConst;
			matConst.DiffuseAlbedo = material->DiffuseAlbedo;
			matConst.FresnelR0 = material->FresnelR0;
			matConst.Roughness = material->Roughness;

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

	mMainPassConstBuff.AmbientLight = { 0.25f,0.25f,0.35f,1.0f };
	auto lightDir = -DX::MathHelper::SphericalToCartesian(1.0f, mSunTheta, mSunPhi);
	XMStoreFloat3(&mMainPassConstBuff.lights[0].Direction, lightDir);
	mMainPassConstBuff.lights[0].Intensity = { 1.0f,1.0f,0.9f };

	const auto currPassCb = mCurrFrameResource->PassConstBuff.get();
	currPassCb->CopyData(0, mMainPassConstBuff);
}

void MyGame::UpdateWaves(const GameTimer& gameTimer) const
{
	static float tBase = 0.0f;
	if ((mTimer.TotalTime() - tBase) >= 0.25f)
	{
		tBase += 0.25f;

		int i = DX::MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = DX::MathHelper::Rand(4, mWaves->RowCount() - 5);

		float r = DX::MathHelper::RandF(0.2f, 0.5f);

		mWaves->Disturb(i, j, r);
	}

	mWaves->Update(gameTimer.DeltaTime());

	auto currWavesVb = mCurrFrameResource->WaveVtxBuff.get();
	for (int i = 0; i < mWaves->VertexCount(); ++i)
	{
		DX::Vertex vtx;

		vtx.Pos = mWaves->Position(i);
		vtx.Normal = mWaves->Normal(i);

		currWavesVb->CopyData(i, vtx);
	}

	mWavesRenderItem->Geometry->VertexBufferGPU = currWavesVb->Resource();
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
}

void MyGame::BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[3];
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstantBufferView(1);
	slotRootParameter[2].InitAsConstantBufferView(2);

	const CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter, 0,
		nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
	mShaders["standardVS"] = DX::CompileShader(L"shader/defaultVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["opaquePS"] = DX::CompileShader(L"shader/defaultPS.hlsl", nullptr, "main", "ps_5_1");

	mInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
}

void MyGame::BuildLandGeometry()
{
	using namespace DirectX;

	DX::GeometryGenerator geoGen;
	DX::GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.f, 50, 50);

	std::vector<DX::Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		const auto& pos = grid.Vertices[i].Position;
		vertices[i].Pos = pos;
		vertices[i].Pos.y = GetHillsHeight(pos.x, pos.z);
		vertices[i].Normal = GetHillsNormal(pos.x, pos.z);
	}

	std::vector<uint16_t> indices = grid.GetIndices16();
	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(DX::Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(uint16_t);

	auto geo = std::make_unique<DX::MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, geo->VertexBufferCPU.GetAddressOf()));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSize, geo->IndexBufferCPU.GetAddressOf()));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = DX::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vbByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = DX::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(DX::Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	DX::SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);
}

void MyGame::BuildWavesGeometryBuffers()
{
	mWaves = std::make_unique<DX::Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

	std::vector<uint16_t> indices(3 * mWaves->TriangleCount());
	assert(mWaves->VertexCount() < 0xffff);

	int m = mWaves->RowCount();
	int n = mWaves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1) * n + j;

			indices[k + 3] = (i + 1) * n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1) * n + j + 1;

			k += 6;
		}
	}

	UINT vbByteSize = mWaves->VertexCount() * sizeof(DX::Vertex);
	UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(uint16_t);

	auto geo = std::make_unique<DX::MeshGeometry>();

	geo->VertexBufferCPU      = nullptr;
	geo->VertexBufferGPU      = nullptr;
	geo->VertexBufferByteSize = vbByteSize;
	geo->VertexByteStride     = sizeof(DX::Vertex);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, geo->IndexBufferCPU.GetAddressOf()));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = DX::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
		indices.data(), ibByteSize, geo->IndexBufferUploader);
	geo->IndexFormat         = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	DX::SubmeshGeometry submesh;
	submesh.IndexCount         = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
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
	for (int i = 0; i < DX::FRAME_RESOURCES_NUM; ++i)
	{
		mFrameResources.push_back(std::make_unique<DX::FrameResource>(
			md3dDevice.Get(), 1, 
			static_cast<UINT>(mRenderItems.size()), 
			static_cast<UINT>(mWaves->VertexCount()),
			static_cast<UINT>(mWaves->VertexCount())
			));
	}
}

void MyGame::BuildMaterials()
{
	using namespace DirectX;

	auto grass = std::make_unique<DX::Material>();
	grass->Name          = "grass";
	grass->MatCbIndex    = 0;
	grass->DiffuseAlbedo = { 0.486274540f, 0.988235354f, 0.000000000f, 1.000000000 };
	grass->FresnelR0     = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness     = 0.7f;

	auto water = std::make_unique<DX::Material>();
	water->Name = "water";
	water->MatCbIndex = 1;
	water->DiffuseAlbedo = { 0.254901975f, 0.411764741f, 0.882353008f, 1.000000000f };
	water->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	water->Roughness = 0.0f;

	mMaterials[grass->Name] = std::move(grass);
	mMaterials[water->Name] = std::move(water);
}

void MyGame::BuildRenderItems()
{
	using namespace DirectX;

	auto wave = std::make_unique<RenderItem>();
	wave->World = DX::MathHelper::Identity4x4();
	wave->ObjConstBuffIndex = 0;
	wave->Mat = mMaterials["water"].get();
	wave->Geometry = mGeometries["waterGeo"].get();
	wave->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wave->IndexCount = wave->Geometry->DrawArgs["grid"].IndexCount;
	wave->StartIndexLocation = wave->Geometry->DrawArgs["grid"].StartIndexLocation;
	wave->BaseVertexLocation = wave->Geometry->DrawArgs["grid"].BaseVertexLocation;

	mWavesRenderItem = wave.get();

	mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(wave.get());

	auto grid = std::make_unique<RenderItem>();
	grid->World = DX::MathHelper::Identity4x4();
	grid->ObjConstBuffIndex = 1;
	grid->Mat = mMaterials["grass"].get();
	grid->Geometry = mGeometries["landGeo"].get();
	grid->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	grid->IndexCount = grid->Geometry->DrawArgs["grid"].IndexCount;
	grid->StartIndexLocation = grid->Geometry->DrawArgs["grid"].StartIndexLocation;
	grid->BaseVertexLocation = grid->Geometry->DrawArgs["grid"].BaseVertexLocation;

	mRenderItemLayer[static_cast<int>(RenderLayer::Opaque)].push_back(grid.get());

	mRenderItems.push_back(std::move(wave));
	mRenderItems.push_back(std::move(grid));
}

void MyGame::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& renderItems) const
{
	UINT objCbByteSize = DX::CalcConstantBufferByteSize(sizeof(DX::ObjectConstants));
	UINT matCbByteSize = DX::CalcConstantBufferByteSize(sizeof(DX::MaterialConstants));
	auto objectCb = mCurrFrameResource->ObjConstBuff->Resource();
	auto matCb = mCurrFrameResource->MatConstBuff->Resource();

	for (const auto & item : renderItems)
	{
		const D3D12_VERTEX_BUFFER_VIEW& vbv = item->Geometry->VertexBufferView();
		const D3D12_INDEX_BUFFER_VIEW& ibv = item->Geometry->IndexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vbv);
		cmdList->IASetIndexBuffer(&ibv);
		cmdList->IASetPrimitiveTopology(item->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCbvAdr = objectCb->GetGPUVirtualAddress();
		objCbvAdr += objCbByteSize * item->ObjConstBuffIndex;
		D3D12_GPU_VIRTUAL_ADDRESS matCbvAdr = matCb->GetGPUVirtualAddress();
		matCbvAdr += matCbByteSize * item->Mat->MatCbIndex;

		cmdList->SetGraphicsRootConstantBufferView(0, objCbvAdr);
		cmdList->SetGraphicsRootConstantBufferView(1, matCbvAdr);

		cmdList->DrawIndexedInstanced(item->IndexCount, 1, 
			item->StartIndexLocation, item->BaseVertexLocation, 0);
	}
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
	catch (DX::DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}
