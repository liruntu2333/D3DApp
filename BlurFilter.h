#pragma once

#include "D3DUtil.h"

namespace DX
{
	class BlurFilter
	{
	public:
		BlurFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format);
		BlurFilter(const BlurFilter&) = delete;
		BlurFilter(const BlurFilter&&) = delete;
		BlurFilter& operator=(const BlurFilter&) = delete;
		BlurFilter& operator=(const BlurFilter&&) = delete;
		~BlurFilter() = default;

		//[[nodiscard]] ID3D12Resource* Output() const { return mBlurMap0.Get(); };

		void BuildDescriptors(
			CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc, 
			CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDesc,
			UINT descSize);

		void OnResize(UINT width, UINT height);

		void Execute(
			ID3D12GraphicsCommandList* cmdList,
			ID3D12RootSignature* rootSig,
			ID3D12PipelineState* horzBlurPso,
			ID3D12PipelineState* vertBlurPso,
			ID3D12Resource* input,
			int blurCnt, float sigmaSpace, float sigmaRange) const;

		static constexpr int DESCRIPTOR_COUNT = 4;

	private:
		static std::vector<float> CalcGaussWeight(float sigma, int& radius);

		void BuildDescriptors() const;
		void BuildResources();

	private:
		static constexpr int MAX_BLUR_RADIUS = 5;

		ID3D12Device* md3dDevice = nullptr;

		UINT mWidth = 0;
		UINT mHeight = 0;
		DXGI_FORMAT mFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

		CD3DX12_CPU_DESCRIPTOR_HANDLE mBlur0CpuSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mBlur0GpuSrv;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mBlur0CpuUav;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mBlur0GpuUav;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mBlur1CpuSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mBlur1GpuSrv;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mBlur1CpuUav;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mBlur1GpuUav;

		Microsoft::WRL::ComPtr<ID3D12Resource> mBlurMap0;
		Microsoft::WRL::ComPtr<ID3D12Resource> mBlurMap1;
	};
}
