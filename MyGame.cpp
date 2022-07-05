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

	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildSceneGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildConstantBufferViews();
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
	memcpy(rtClearValue.Color, Colors::Black, sizeof(float) * 4);

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
	UpdateObjectConstBuffs(gameTimer);
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
	mCommandList->ClearRenderTargetView(hRtv, Colors::Black, 0, nullptr);
	mCommandList->ClearDepthStencilView(hDsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1,&hRtv,
		true, &hDsv);

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	UINT passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	auto hCbv = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	hCbv.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, hCbv);

	DrawRenderItems(mCommandList.Get(), mOpaqueRenderItems);

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
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
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

	const auto currPassCb = mCurrFrameResource->PassConstBuff.get();
	currPassCb->CopyData(0, mMainPassConstBuff);
}

void MyGame::BuildDescriptorHeaps()
{
	const UINT objCount = static_cast<UINT>(mOpaqueRenderItems.size());
	const UINT numDescriptors = (objCount + 1) * FRAME_RESOURCES_NUM;

	mPassCbvOffset = objCount * FRAME_RESOURCES_NUM;

	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask       = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&cbvHeapDesc,
		IID_PPV_ARGS(mCbvHeap.GetAddressOf())));

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

void MyGame::BuildConstantBufferViews() const
{
	const UINT objCbByteSize = DX::CalcConstantBufferByteSize(sizeof(DX::ObjectConstants));
	const UINT objCnt = static_cast<UINT>(mOpaqueRenderItems.size());

	for (int frameIdx = 0; frameIdx < FRAME_RESOURCES_NUM; ++frameIdx)
	{
		const auto objCb = mFrameResources[frameIdx]->ObjConstBuff->Resource();

		for (UINT i = 0; i < objCnt; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objCb->GetGPUVirtualAddress();
			cbAddress += i * static_cast<UINT64>(objCbByteSize);

			const UINT heapIdx = frameIdx * objCnt + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIdx, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes    = objCbByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	const UINT passCbByteSize = DX::CalcConstantBufferByteSize(sizeof(DX::PassConstants));

	for (int frameIdx = 0; frameIdx < FRAME_RESOURCES_NUM; ++frameIdx)
	{
		const auto passCb = mFrameResources[frameIdx]->PassConstBuff->Resource();
		const D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCb->GetGPUVirtualAddress();

		const UINT heapIdx = mPassCbvOffset + frameIdx;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIdx, mCbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes    = passCbByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void MyGame::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

	const CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0,
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
	mShaders["standardVS"] = DX::CompileShader(L"shader/colorVS.hlsl", nullptr, "main", "vs_5_1");
	mShaders["opaquePS"] = DX::CompileShader(L"shader/colorPS.hlsl", nullptr, "main", "ps_5_1");

	mInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
}

void MyGame::BuildSceneGeometry()
{
	using namespace DirectX;

	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
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

	DX::SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = static_cast<UINT>(box.Indices32.size());
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = static_cast<INT>(boxVertexOffset);

	DX::SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = static_cast<UINT>(grid.Indices32.size());
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = static_cast<INT>(gridVertexOffset);

	DX::SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = static_cast<UINT>(sphere.Indices32.size());
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = static_cast<INT>(sphereVertexOffset);

	DX::SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = static_cast<UINT>(cylinder.Indices32.size());
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = static_cast<INT>(cylinderVertexOffset);

	auto totalVertexCount = box.Vertices.size() + grid.Vertices.size() +
		sphere.Vertices.size() + cylinder.Vertices.size();

	std::vector<DX::Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::DarkGreen);
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());
	indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());
	indices.insert(indices.end(), sphere.GetIndices16().begin(), sphere.GetIndices16().end());
	indices.insert(indices.end(), cylinder.GetIndices16().begin(), cylinder.GetIndices16().end());

	const UINT vbBytSize = static_cast<UINT>(vertices.size()) * sizeof(DX::Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

	auto geometry = std::make_unique<DX::MeshGeometry>();
	geometry->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbBytSize, &geometry->VertexBufferCPU));
	CopyMemory(geometry->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbBytSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geometry->IndexBufferCPU));
	CopyMemory(geometry->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geometry->VertexBufferGPU = CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbBytSize, geometry->VertexBufferUploader);

	geometry->IndexBufferGPU = DX::CreateDefaultBuffer(md3dDevice.Get(),
	mCommandList.Get(), indices.data(), ibByteSize, geometry->IndexBufferUploader);

	geometry->VertexByteStride = sizeof(DX::Vertex);
	geometry->VertexBufferByteSize = vbBytSize;
	geometry->IndexFormat = DXGI_FORMAT_R16_UINT;
	geometry->IndexBufferByteSize = ibByteSize;

	geometry->DrawArgs["box"] = boxSubmesh;
	geometry->DrawArgs["grid"] = gridSubmesh;
	geometry->DrawArgs["sphere"] = sphereSubmesh;
	geometry->DrawArgs["cylinder"] = cylinderSubmesh;

	mGeometries[geometry->Name] = std::move(geometry);
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
		mFrameResources.push_back(std::make_unique<DX::FrameResource>
			(md3dDevice.Get(), 1, static_cast<UINT>(mRenderItems.size())));
	}
}

void MyGame::BuildRenderItems()
{
	using namespace DirectX;

	auto box = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&box->World, 
		XMMatrixScaling(2.0f, 2.0f, 2.0f) * 
		XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	box->ObjConstBuffIndex  = 0;
	box->Geometry           = mGeometries["shapeGeo"].get();
	box->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	box->IndexCount         = box->Geometry->DrawArgs["box"].IndexCount;
	box->StartIndexLocation = box->Geometry->DrawArgs["box"].StartIndexLocation;
	box->BaseVertexLocation = box->Geometry->DrawArgs["box"].BaseVertexLocation;
	mRenderItems.push_back(std::move(box));

	auto grid = std::make_unique<RenderItem>();
	grid->World              = MathHelper::Identity4x4();
	grid->ObjConstBuffIndex  = 1;
	grid->Geometry           = mGeometries["shapeGeo"].get();
	grid->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	grid->IndexCount         = grid->Geometry->DrawArgs["grid"].IndexCount;
	grid->StartIndexLocation = grid->Geometry->DrawArgs["grid"].StartIndexLocation;
	grid->BaseVertexLocation = grid->Geometry->DrawArgs["grid"].BaseVertexLocation;
	mRenderItems.push_back(std::move(grid));

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
		lftCyl->ObjConstBuffIndex     = objConstBuffIndex++;
		lftCyl->Geometry              = mGeometries["shapeGeo"].get();
		lftCyl->PrimitiveType         = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lftCyl->IndexCount            = lftCyl->Geometry->DrawArgs["cylinder"].IndexCount;
		lftCyl->StartIndexLocation    = lftCyl->Geometry->DrawArgs["cylinder"].StartIndexLocation;
		lftCyl->BaseVertexLocation    = lftCyl->Geometry->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rhtCyl->World, rightCylWorld);
		rhtCyl->ObjConstBuffIndex     = objConstBuffIndex++;
		rhtCyl->Geometry              = mGeometries["shapeGeo"].get();
		rhtCyl->PrimitiveType         = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rhtCyl->IndexCount            = rhtCyl->Geometry->DrawArgs["cylinder"].IndexCount;
		rhtCyl->StartIndexLocation    = rhtCyl->Geometry->DrawArgs["cylinder"].StartIndexLocation;
		rhtCyl->BaseVertexLocation    = rhtCyl->Geometry->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&lftSphere->World, leftSphereWorld);
		lftSphere->ObjConstBuffIndex  = objConstBuffIndex++;
		lftSphere->Geometry           = mGeometries["shapeGeo"].get();
		lftSphere->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		lftSphere->IndexCount         = lftSphere->Geometry->DrawArgs["sphere"].IndexCount;
		lftSphere->StartIndexLocation = lftSphere->Geometry->DrawArgs["sphere"].StartIndexLocation;
		lftSphere->BaseVertexLocation = lftSphere->Geometry->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rhtSphere->World, rightSphereWorld);
		rhtSphere->ObjConstBuffIndex  = objConstBuffIndex++;
		rhtSphere->Geometry           = mGeometries["shapeGeo"].get();
		rhtSphere->PrimitiveType      = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rhtSphere->IndexCount         = rhtSphere->Geometry->DrawArgs["sphere"].IndexCount;
		rhtSphere->StartIndexLocation = rhtSphere->Geometry->DrawArgs["sphere"].StartIndexLocation;
		rhtSphere->BaseVertexLocation = rhtSphere->Geometry->DrawArgs["sphere"].BaseVertexLocation;

		mRenderItems.push_back(std::move(lftCyl));
		mRenderItems.push_back(std::move(rhtCyl));
		mRenderItems.push_back(std::move(lftSphere));
		mRenderItems.push_back(std::move(rhtSphere));
	}

	for (auto & item : mRenderItems)
		mOpaqueRenderItems.push_back(item.get());
}

void MyGame::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& renderItems) const
{
	UINT objCbByteSize = DX::CalcConstantBufferByteSize(sizeof(DX::ObjectConstants));
	auto objectCb = mCurrFrameResource->ObjConstBuff->Resource();

	for (const auto & item : renderItems)
	{
		const D3D12_VERTEX_BUFFER_VIEW& vbv = item->Geometry->VertexBufferView();
		const D3D12_INDEX_BUFFER_VIEW& ibv = item->Geometry->IndexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vbv);
		cmdList->IASetIndexBuffer(&ibv);
		cmdList->IASetPrimitiveTopology(item->PrimitiveType);

		const UINT cbvIndex = mCurrFrameResourceIndex * static_cast<UINT>(mOpaqueRenderItems.size()) + 
			item->ObjConstBuffIndex;
		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);
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
	catch (DX::DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}
