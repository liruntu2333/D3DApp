/*****************************************************************//**
 * \file   MyGame.cpp
 * \brief  draw a box
 *
 * \author LirRuntu liruntu2333@gmail.com
 * \date   June 2022
 *********************************************************************/

#include <DirectXColors.h>

#include "MyGame.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
using namespace DX;

struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT4 Color;
};

struct ObjectConstants
{
	XMFLOAT4X4 WorldViewProjection = MathHelper::Identity4X4();
};

namespace
{
	constexpr int SAMPLE_COUNT_MAX = 8;
}

MyGame::MyGame(HINSTANCE hInstance) : D3DApp(hInstance) {}

MyGame::~MyGame() = default;

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

	BuildDescriptorHeaps();
	BuildConstantBuffers();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildSceneGeometry();
	BuildPipelineStateObject();

	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* commandLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

	FlushCommandQueue();

	OnResize();
	return true;
}

void MyGame::OnResize()
{
	D3DApp::OnResize();

	CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_DEFAULT);

	// Create an MSAA render target.
	D3D12_RESOURCE_DESC msaaRTDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		mBackBufferFormat,
		mClientWidth, mClientHeight,
		1, 1, mSampleCount);
	msaaRTDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE rtClearValue = {};
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

void MyGame::Update(const GameTimer& gt)
{
	float x = mRadius * sinf(mPhi) * cosf(mTheta);
	float z = mRadius * sinf(mPhi) * sinf(mTheta);
	float y = mRadius * cosf(mPhi);

	XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);

	XMMATRIX world = XMLoadFloat4x4(&mWorld);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);
	XMMATRIX worldViewProj = world * view * proj;

	ObjectConstants objConstants;
	XMStoreFloat4x4(&objConstants.WorldViewProjection, XMMatrixTranspose(worldViewProj));
	mObjConstBuff->CopyData(0, objConstants);
}

void MyGame::Draw(const GameTimer& gt)
{
	ThrowIfFailed(mDirectCmdListAlloc->Reset());

	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), mPipelineStateObject.Get()));

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	{
		D3D12_RESOURCE_BARRIER barrier[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(mMsaaRenderTarget.Get(),
			D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)
		};

		mCommandList->ResourceBarrier(_countof(barrier), barrier);
	}

	auto msaaRTHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mMsaaRTVDescHeap->GetCPUDescriptorHandleForHeapStart());
	auto msaaDSHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		mMsaaDSVDescHeap->GetCPUDescriptorHandleForHeapStart());
	mCommandList->ClearRenderTargetView(msaaRTHandle,
		Colors::Black, 0, nullptr);
	mCommandList->ClearDepthStencilView(msaaDSHandle,
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);

	mCommandList->OMSetRenderTargets(1,
		&msaaRTHandle,
		true,
		&msaaDSHandle);

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	for (const auto& obj : mScene)
	{
		auto meshVbv = obj->VertexBufferView();
		auto meshIbv = obj->IndexBufferView();
		mCommandList->IASetVertexBuffers(0, 1, &meshVbv);
		mCommandList->IASetIndexBuffer(&meshIbv);
		mCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		mCommandList->SetGraphicsRootDescriptorTable(0, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

		mCommandList->DrawIndexedInstanced(
			obj->DrawArgs[obj->Name].IndexCount,
			1, 0, 0, 0);
	}

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

	FlushCommandQueue();
}

void MyGame::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void MyGame::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mTheta += dx;
		mPhi += dy;

		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
		float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

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

void MyGame::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc{};
	cbvHeapDesc.NumDescriptors = 1;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
		&cbvHeapDesc,
		IID_PPV_ARGS(mCbvHeap.GetAddressOf())));

	// Create descriptor heaps for MSAA render target views and depth stencil views.
	D3D12_DESCRIPTOR_HEAP_DESC rtvDescHeapDesc{};
	rtvDescHeapDesc.NumDescriptors = 1;
	rtvDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvDescHeapDesc,
		IID_PPV_ARGS(mMsaaRTVDescHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvDescHeapDesc{};
	dsvDescHeapDesc.NumDescriptors = 1;
	dsvDescHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dsvDescHeapDesc,
		IID_PPV_ARGS(mMsaaDSVDescHeap.GetAddressOf())));
}

void MyGame::BuildConstantBuffers()
{
	mObjConstBuff = std::make_unique<UploadBuffer<ObjectConstants>>(md3dDevice.Get(), 1, true);

	UINT objConstBuffByteSize = CalcConstantBufferByteSize(sizeof(ObjectConstants));

	D3D12_GPU_VIRTUAL_ADDRESS constBuffAddress = mObjConstBuff->Resource()->GetGPUVirtualAddress();
	UINT64 boxConstBuffIndex = 0;
	constBuffAddress += boxConstBuffIndex * objConstBuffByteSize;

	D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
	cbvDesc.BufferLocation = constBuffAddress;
	cbvDesc.SizeInBytes = objConstBuffByteSize;

	md3dDevice->CreateConstantBufferView(
		&cbvDesc,
		mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void MyGame::BuildRootSignature()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[1];

	CD3DX12_DESCRIPTOR_RANGE constBuffViewTable;
	constBuffViewTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
	slotRootParameter[0].InitAsDescriptorTable(1, &constBuffViewTable);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0,
		nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
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
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void MyGame::BuildShadersAndInputLayout()
{
	HRESULT hr = S_OK;

	mVSbyteCode = CompileShader(L"shader/colorVS.hlsl", nullptr, "main", "vs_5_0");
	mPSbyteCode = CompileShader(L"shader/colorPS.hlsl", nullptr, "main", "ps_5_0");

	mInputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
	};
}

void MyGame::BuildSceneGeometry()
{
	std::array<Vertex, 8> vertices =
	{
		Vertex{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White)},
		Vertex{ XMFLOAT3(-1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Black)},
		Vertex{ XMFLOAT3(+1.0f, +1.0f, -1.0f), XMFLOAT4(Colors::Red)},
		Vertex{ XMFLOAT3(+1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::Green)},
		Vertex{ XMFLOAT3(-1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Blue)},
		Vertex{ XMFLOAT3(-1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Yellow)},
		Vertex{ XMFLOAT3(+1.0f, +1.0f, +1.0f), XMFLOAT4(Colors::Cyan)},
		Vertex{ XMFLOAT3(+1.0f, -1.0f, +1.0f), XMFLOAT4(Colors::Magenta)},
	};

	std::array<std::uint16_t, 36> indices =
	{
		// front face
		0, 1, 2,
		0, 2, 3,

		// back face
		4, 6, 5,
		4, 7, 6,

		// left face
		4, 5, 1,
		4, 1, 0,

		// right face
		3, 2, 6,
		3, 6, 7,

		// top face
		1, 5, 6,
		1, 6, 2,

		// bottom face
		4, 0, 3,
		4, 3, 7
	};

	constexpr UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
	constexpr UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(uint16_t);

	auto box = std::make_unique<MeshGeometry>();
	box->Name = "box";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, box->VertexBufferCPU.GetAddressOf()));
	CopyMemory(box->VertexBufferCPU->GetBufferPointer(),
		vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, box->IndexBufferCPU.GetAddressOf()));
	CopyMemory(box->IndexBufferCPU->GetBufferPointer(),
		indices.data(), ibByteSize);

	box->VertexBufferGPU = DX::CreateDefaultBuffer(
		md3dDevice.Get(), mCommandList.Get(),
		vertices.data(), vbByteSize,
		box->VertexBufferUploader);

	box->IndexBufferGPU = DX::CreateDefaultBuffer(
		md3dDevice.Get(), mCommandList.Get(),
		indices.data(), ibByteSize,
		box->IndexBufferUploader);

	box->VertexByteStride = sizeof(Vertex);
	box->VertexBufferByteSize = vbByteSize;
	box->IndexFormat = DXGI_FORMAT_R16_UINT;
	box->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	box->DrawArgs["box"] = submesh;

	mScene.push_back(std::move(box));
}

void MyGame::BuildPipelineStateObject()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};

	psoDesc.InputLayout.pInputElementDescs = mInputLayout.data();
	psoDesc.InputLayout.NumElements = static_cast<UINT>(mInputLayout.size());

	psoDesc.pRootSignature = mRootSignature.Get();

	psoDesc.VS.pShaderBytecode = mVSbyteCode->GetBufferPointer();
	psoDesc.VS.BytecodeLength = mVSbyteCode->GetBufferSize();

	psoDesc.PS.pShaderBytecode = mPSbyteCode->GetBufferPointer();
	psoDesc.PS.BytecodeLength = mPSbyteCode->GetBufferSize();

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = mBackBufferFormat;
	psoDesc.SampleDesc.Count = mSampleCount;
	psoDesc.SampleDesc.Quality = 0;
	psoDesc.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
		&psoDesc, IID_PPV_ARGS(mPipelineStateObject.GetAddressOf())));
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