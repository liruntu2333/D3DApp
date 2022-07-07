#include "FrameResource.h"

DX::FrameResource::FrameResource(ID3D12Device* device, UINT passCount, 
	UINT objectCount, UINT materialCount, UINT waveVertCount)
{
	ThrowIfFailed(device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

	PassConstBuff = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	
	ObjConstBuff = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);

	MatConstBuff = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);

	WaveVtxBuff = std::make_unique<UploadBuffer<DX::Vertex>>(device, waveVertCount, false);
}
