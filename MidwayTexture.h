#pragma once
#include "D3DUtil.h"

namespace DX
{
	/**
	 * \brief Textures used in the midway of rendering, e.g. resolving dest,
	 * input & output of post effect. Generate no RTV(s).
	 */
	class MidwayTexture
	{
	public:

		MidwayTexture(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format);
		MidwayTexture(const MidwayTexture&) = delete;
		MidwayTexture(const MidwayTexture&&) = delete;
		MidwayTexture& operator=(const MidwayTexture&) = delete;
		MidwayTexture& operator=(const MidwayTexture&&) = delete;
		~MidwayTexture() = default;

		[[nodiscard]] auto GetResource() const { return mOffscreenTex.Get(); }
		[[nodiscard]] auto GetSrv() const { return mhGpuSrv; }

		void BuildDescriptors(
			CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
			CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv);

		void OnResize(UINT width, UINT height);

		static constexpr int SRV_COUNT = 1;

	private:

		void BuildDescriptors() const;
		void BuildResource();

		ID3D12Device* md3dDevice = nullptr;

		UINT mWidth = 0;
		UINT mHeight = 0;
		DXGI_FORMAT mFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

		CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;

		Microsoft::WRL::ComPtr<ID3D12Resource> mOffscreenTex = nullptr;
	};
}
