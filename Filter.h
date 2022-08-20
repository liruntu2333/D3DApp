#pragma once

#include "D3DUtil.h"

namespace DX
{
	class Filter
	{
	public:

		Filter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format);
		Filter(const Filter&) = delete;
		Filter(const Filter&&) = delete;
		Filter& operator=(const Filter&) = delete;
		Filter& operator=(const Filter&&) = delete;
		virtual ~Filter() = default;

		virtual void BuildDescriptors(
			CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc,
			CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDesc,
			UINT descSize) = 0;

		void OnResize(UINT width, UINT height);

	protected:

		virtual void BuildDescriptors() const = 0;
		virtual void BuildResources() = 0;

		ID3D12Device* md3dDevice = nullptr;

		UINT mWidth = 0;
		UINT mHeight = 0;
		DXGI_FORMAT mFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	};
}

