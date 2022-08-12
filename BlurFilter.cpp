#include "BlurFilter.h"

DX::BlurFilter::BlurFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format) :
	md3dDevice(device),
	mWidth(width),
	mHeight(height),
	mFormat(format)
{
	BuildResources();
}

std::vector<float> DX::BlurFilter::CalcGaussWeight(float sigma, int& radius)
{
	int blurR = static_cast<int>(ceil(2.0f * sigma));
	assert(blurR <= MAX_BLUR_RADIUS);

	// G(x) = exp(- x^2 / (2 * sigma ^ 2))
	const float twoSigma2 = 2.0f * sigma * sigma;
	std::vector<float> weights(2 * blurR + 1);
	float weightSum = 0.0f;

	for (int i = -blurR; i <= blurR; ++i)
	{
		const auto x = static_cast<float>(i);
		float weight = expf(-x * x / twoSigma2);
		weights[i + blurR] = weight;
		weightSum += weight;
	}

	for (auto& weight : weights)
	{
		weight /= weightSum;
	}

	radius = blurR;
	return weights;
}

void DX::BlurFilter::BuildDescriptors() const
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format                    = mFormat;
	srvDesc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels       = 1;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format             = mFormat;
	uavDesc.ViewDimension      = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	md3dDevice->CreateShaderResourceView(mBlurMap0.Get(), &srvDesc, mBlur0CpuSrv);
	md3dDevice->CreateUnorderedAccessView(mBlurMap0.Get(), nullptr, 
		&uavDesc, mBlur0CpuUav);

	md3dDevice->CreateShaderResourceView(mBlurMap1.Get(), &srvDesc, mBlur1CpuSrv);
	md3dDevice->CreateUnorderedAccessView(mBlurMap1.Get(), 
		nullptr, &uavDesc, mBlur1CpuUav);
}

void DX::BlurFilter::BuildResources()
{
	CD3DX12_RESOURCE_DESC texDesc
	(
		D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		0,
		mWidth,
		mHeight,
		1,
		1,
		mFormat,
		1,
		0,
		D3D12_TEXTURE_LAYOUT_UNKNOWN,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);

	const auto heapProperty = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	// initState	resolveDest			 copyDest			unorderedAcs	   unorderedAcs		  copyDest 
	// operation	backBuffer --copy--> blurMap0 --blur--> blurMap1 --blur--> blurMap0 --copy--> backBuffer
	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperty,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&mBlurMap0)));

	mBlurMap0->SetName(L"Blur Map 0");

	ThrowIfFailed(md3dDevice->CreateCommittedResource(
		&heapProperty,
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		nullptr,
		IID_PPV_ARGS(&mBlurMap1)));

	mBlurMap1->SetName(L"Blur Map 1");
}

void DX::BlurFilter::BuildDescriptors(
	CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc, 
	CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDesc, 
	UINT descSize)
{
	mBlur0CpuSrv = hCpuDesc;
	mBlur0CpuUav = hCpuDesc.Offset(1, descSize);
	mBlur1CpuSrv = hCpuDesc.Offset(1, descSize);
	mBlur1CpuUav = hCpuDesc.Offset(1, descSize);

	mBlur0GpuSrv = hGpuDesc;
	mBlur0GpuUav = hGpuDesc.Offset(1, descSize);
	mBlur1GpuSrv = hGpuDesc.Offset(1, descSize);
	mBlur1GpuUav = hGpuDesc.Offset(1, descSize);

	BuildDescriptors();
}

void DX::BlurFilter::OnResize(UINT width, UINT height)
{
	if (mWidth != width || mHeight != height)
	{
		mWidth = width;
		mHeight = height;

		BuildResources();
		BuildDescriptors();
	}
}

void DX::BlurFilter::Execute(
	ID3D12GraphicsCommandList* cmdList, 
	ID3D12RootSignature* rootSig, 
	ID3D12PipelineState* horzBlurPso, 
	ID3D12PipelineState* vertBlurPso, 
	ID3D12Resource* input, 
	const int blurCnt) const
{
	int blurRadius = 0;
	const auto weights = CalcGaussWeight(2.5f, blurRadius);

	cmdList->SetComputeRootSignature(rootSig);
	cmdList->SetComputeRoot32BitConstants(0, 1, &blurRadius, 0);
	cmdList->SetComputeRoot32BitConstants(0, static_cast<UINT>(weights.size()), weights.data(),1);

	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(input,
			D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
		cmdList->ResourceBarrier(1, &barrier);

		// BackBuffer --copy--> BlurMap0
		cmdList->CopyResource(mBlurMap0.Get(), input);

		barrier = CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ);
		cmdList->ResourceBarrier(1, &barrier);
	}

	for (int i = 0; i < blurCnt; ++i)
	{
		// horz blur pass
		// BlurMap0 --horzBlur--> BlurMap1
		cmdList->SetPipelineState(horzBlurPso);
		cmdList->SetComputeRootDescriptorTable(1, mBlur0GpuSrv);
		cmdList->SetComputeRootDescriptorTable(2, mBlur1GpuUav);

		// 1 thread = 1 pixel
		// 256 threads = 1 group (defined in CS)
		// X : ⌈num of width / 256⌉
		// Y : (num of height)
		// Z : 1
		// total : ⌈num of width / 256⌉ * (num of height) * 1 threads
		UINT nGroupX = static_cast<UINT>(ceilf(static_cast<float>(mWidth) / 256.0f));
		cmdList->Dispatch(nGroupX, mHeight, 1);

		{
			CD3DX12_RESOURCE_BARRIER barriers[] = 
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
				D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ),
			};
			cmdList->ResourceBarrier(_countof(barriers), barriers);
		}

		// vert blur pass
		// BlurMap1 --vertBlur--> BlurMap0
		cmdList->SetPipelineState(vertBlurPso);
		cmdList->SetComputeRootDescriptorTable(1, mBlur1GpuSrv);
		cmdList->SetComputeRootDescriptorTable(2, mBlur0GpuUav);

		UINT nGroupY = static_cast<UINT>(ceilf(static_cast<float>(mHeight) / 256.0f));
		cmdList->Dispatch(mWidth, nGroupY, 1);

		{
			CD3DX12_RESOURCE_BARRIER barriers[] = 
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ),
				CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap1.Get(),
				D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			cmdList->ResourceBarrier(_countof(barriers), barriers);
		}
	}

	{
		CD3DX12_RESOURCE_BARRIER barriers[] =
		{
			CD3DX12_RESOURCE_BARRIER::Transition(input,
			D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_COPY_SOURCE),
		};
		cmdList->ResourceBarrier(_countof(barriers), barriers);
	}
	// BlurMap0 --copy--> BackBuffer
	cmdList->CopyResource(input, mBlurMap0.Get());

	{
		CD3DX12_RESOURCE_BARRIER barriers[] =
		{
				
			CD3DX12_RESOURCE_BARRIER::Transition(mBlurMap0.Get(),
			D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST),
			CD3DX12_RESOURCE_BARRIER::Transition(input,
			D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT),
		};
		cmdList->ResourceBarrier(_countof(barriers), barriers);
	}
}
