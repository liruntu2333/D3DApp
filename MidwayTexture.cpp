#include "MidwayTexture.h"

DX::MidwayTexture::MidwayTexture(ID3D12Device* device, const UINT width, const UINT height, const DXGI_FORMAT format)
	: md3dDevice(device), mWidth(width), mHeight(height), mFormat(format)
{
	BuildResource();
}

void DX::MidwayTexture::BuildDescriptors(const CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv, const CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv)
{
	mhCpuSrv = hCpuSrv;
	mhGpuSrv = hGpuSrv;

	BuildDescriptors();
}

void DX::MidwayTexture::OnResize(const UINT width, const UINT height)
{
	if ((mWidth != width) || (mHeight != height))
	{
		mWidth = width;
		mHeight = height;

		BuildResource();
		BuildDescriptors();
	}
}

void DX::MidwayTexture::BuildDescriptors() const
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = mFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	md3dDevice->CreateShaderResourceView(mOffscreenTex.Get(), &srvDesc, mhCpuSrv);
}

void DX::MidwayTexture::BuildResource()
{
	auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(mFormat, mWidth, mHeight, 1, 1,
		1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	auto heapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

	ThrowIfFailed(md3dDevice->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mOffscreenTex)));

	mOffscreenTex->SetName(L"Offscreen Texture");
}
