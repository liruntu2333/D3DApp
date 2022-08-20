#include "Filter.h"

DX::Filter::Filter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format)
	: md3dDevice(device), mWidth(width), mHeight(height), mFormat(format)
{
}

void DX::Filter::OnResize(UINT width, UINT height)
{
	if (mWidth != width || mHeight != height)
	{
		mWidth = width;
		mHeight = height;

		BuildResources();
		BuildDescriptors();
	}
}
