#include "SobelFilter.h"

DX::SobelFilter::SobelFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format):
	Filter(device, width, height, format)
{
	SobelFilter::BuildResources();
}

void DX::SobelFilter::BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDesc, UINT descSize)
{
	mhCpuSrv = hCpuDesc;
	mhCpuUav = hCpuDesc.Offset(1, descSize);
	mhGpuSrv = hGpuDesc;
	mhGpuUav = hGpuDesc.Offset(1, descSize);

	BuildDescriptors();
}

void DX::SobelFilter::Execute(
	ID3D12GraphicsCommandList* cmdList, 
	ID3D12RootSignature* rootSig, 
	ID3D12PipelineState* pso,
	const CD3DX12_GPU_DESCRIPTOR_HANDLE input) const
{
	cmdList->SetComputeRootSignature(rootSig);
	cmdList->SetPipelineState(pso);
	cmdList->SetComputeRootDescriptorTable(0, input);
	cmdList->SetComputeRootDescriptorTable(2, mhGpuUav);

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mOutput.Get(),
		D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmdList->ResourceBarrier(1, &barrier);

	const UINT groupNumX = static_cast<UINT>(ceilf(static_cast<float>(mWidth) / 16.0f));
	const UINT groupNumY = static_cast<UINT>(ceilf(static_cast<float>(mHeight) / 16.0f));
	cmdList->Dispatch(groupNumX, groupNumY, 1);

	barrier = CD3DX12_RESOURCE_BARRIER::Transition(mOutput.Get(),
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->ResourceBarrier(1, &barrier);
}

void DX::SobelFilter::BuildResources()
{
	auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(mFormat, mWidth, mHeight,
		1, 1);
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	const auto heapDefault = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(md3dDevice->CreateCommittedResource(&heapDefault, D3D12_HEAP_FLAG_NONE,
		&texDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(mOutput.GetAddressOf())));
}

void DX::SobelFilter::BuildDescriptors() const
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = mFormat;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	md3dDevice->CreateShaderResourceView(mOutput.Get(), &srvDesc, mhCpuSrv);
	md3dDevice->CreateUnorderedAccessView(mOutput.Get(), nullptr, &uavDesc, mhCpuUav);
}
