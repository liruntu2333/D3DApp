#include "Waves.h"
#include "3rdparty/DirectXTK12/Inc/ResourceUploadBatch.h"

DX::Waves::Waves(ID3D12Device* device, ID3D12CommandQueue* cmdQueue,
                 const int m, const int n, const float dx, const float dt, const float speed, const float damping) :
	mNumRows(m),
	mNumCols(n),
	mVertexCount(m * n),
	mTriangleCount((m - 1) * (n - 1) * 2),
	mTimeStep(dt),
	mSpatialStep(dx),
	md3dDevice(device)
{
	assert((m * n) % 256 == 0);

	const float d = damping * dt + 2.0f;
	const float e = (speed * speed) * (dt * dt) / (dx * dx);
	mSimulationConstants[0] = (damping * dt - 2.0f) / d;
	mSimulationConstants[1] = (4.0f - 8.0f * e) / d;
	mSimulationConstants[2] = (2.0f * e) / d;

	BuildResource(cmdQueue);
}

void DX::Waves::BuildResource(ID3D12CommandQueue* cmdQueue)
{
	const auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_FLOAT, 
		mNumCols, mNumRows);
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	ThrowIfFailed(md3dDevice->CreateCommittedResource(&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mPrevSol)));

	ThrowIfFailed(md3dDevice->CreateCommittedResource(&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mCurrSol)));

	ThrowIfFailed(md3dDevice->CreateCommittedResource(&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mNextSol)));

	mPrevSol->SetName(L"Simulation Solution0");
	mCurrSol->SetName(L"Simulation Solution1");
	mNextSol->SetName(L"Simulation Solution2");

	std::vector heights(mNumRows * mNumCols, 0.0f);
	D3D12_SUBRESOURCE_DATA initData;
	initData.pData      = heights.data();
	initData.RowPitch   = static_cast<LONG_PTR>(mNumCols * sizeof(float));
	initData.SlicePitch = initData.RowPitch * mNumRows;

	DirectX::ResourceUploadBatch upload1(md3dDevice);
	upload1.Begin();
	upload1.Upload(mPrevSol.Get(), 0, &initData, 1);
	upload1.Transition(mPrevSol.Get(), 
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	auto finish1 = upload1.End(cmdQueue);

	DirectX::ResourceUploadBatch upload2(md3dDevice);
	upload2.Begin();
	upload2.Upload(mCurrSol.Get(), 0, &initData, 1);
	upload2.Transition(mCurrSol.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
	auto finish2 = upload2.End(cmdQueue);

	finish1.wait();
	finish2.wait();
}

void DX::Waves::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDesc,
                                 UINT descSize)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format                    = DXGI_FORMAT_R32_FLOAT;
	srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels       = 1;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format             = DXGI_FORMAT_R32_FLOAT;
	uavDesc.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	md3dDevice->CreateShaderResourceView(mPrevSol.Get(), &srvDesc, hCpuDesc);
	md3dDevice->CreateShaderResourceView(mCurrSol.Get(), &srvDesc, hCpuDesc.Offset(1, descSize));
	md3dDevice->CreateShaderResourceView(mNextSol.Get(), &srvDesc, hCpuDesc.Offset(1, descSize));

	md3dDevice->CreateUnorderedAccessView(mPrevSol.Get(), nullptr, &uavDesc, hCpuDesc.Offset(1, descSize));
	md3dDevice->CreateUnorderedAccessView(mCurrSol.Get(), nullptr, &uavDesc, hCpuDesc.Offset(1, descSize));
	md3dDevice->CreateUnorderedAccessView(mNextSol.Get(), nullptr, &uavDesc, hCpuDesc.Offset(1, descSize));

	mPrevSolSrv = hGpuDesc;
	mCurrSolSrv = hGpuDesc.Offset(1, descSize);
	mNextSolSrv = hGpuDesc.Offset(1, descSize);
	mPrevSolUav = hGpuDesc.Offset(1, descSize);
	mCurrSolUav = hGpuDesc.Offset(1, descSize);
	mNextSolUav = hGpuDesc.Offset(1, descSize);
}

void DX::Waves::Update(const GameTimer& gameTimer, ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig,
                       ID3D12PipelineState* pso)
{
	static float t = 0.0f;
	t += gameTimer.DeltaTime();

	if (t >= mTimeStep)
	{
		// prev(unordered access) curr(generic read) next(unordered access)
		{
			const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mCurrSol.Get(),
				D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			cmdList->ResourceBarrier(1, &barrier);
		}

		cmdList->SetPipelineState(pso);
		cmdList->SetComputeRootSignature(rootSig);
		cmdList->SetComputeRoot32BitConstants(0, 3, mSimulationConstants, 0);
		cmdList->SetComputeRootDescriptorTable(1, mPrevSolUav);
		cmdList->SetComputeRootDescriptorTable(2, mCurrSolUav);
		cmdList->SetComputeRootDescriptorTable(3, mNextSolUav);

		const UINT groupNumX = mNumCols / 16;
		const UINT groupNumY = mNumRows / 16;

		cmdList->Dispatch(groupNumX, groupNumY, 1);

		std::swap(mPrevSol, mCurrSol);
		std::swap(mCurrSol, mNextSol);

		std::swap(mPrevSolSrv, mCurrSolSrv);
		std::swap(mCurrSolSrv, mNextSolSrv);

		std::swap(mPrevSolUav, mCurrSolUav);
		std::swap(mCurrSolUav, mNextSolUav);

		t = 0.0f;

		{
			const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mCurrSol.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
			cmdList->ResourceBarrier(1, &barrier);
		}
	}
}

void DX::Waves::Disturb(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig, ID3D12PipelineState* pso,
                        const UINT i, const UINT j, const float magnitude) const
{
	cmdList->SetPipelineState(pso);
	cmdList->SetComputeRootSignature(rootSig);

	const UINT disturbIdx[2] = { j, i };
	cmdList->SetComputeRoot32BitConstants(0, 1, &magnitude, 3);
	cmdList->SetComputeRoot32BitConstants(0, 2, disturbIdx, 4);
	cmdList->SetComputeRootDescriptorTable(3, mCurrSolUav);

	{
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mCurrSol.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cmdList->ResourceBarrier(1, &barrier);
	}

	cmdList->Dispatch(1, 1, 1);

	{
		const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mCurrSol.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
		cmdList->ResourceBarrier(1, &barrier);
	}
}
