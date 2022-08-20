#pragma once

#include "Filter.h"

namespace DX
{
	class BlurFilter final : public Filter
	{
	public:
		BlurFilter(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format);
		BlurFilter(const BlurFilter&) = delete;
		BlurFilter(const BlurFilter&&) = delete;
		BlurFilter& operator=(const BlurFilter&) = delete;
		BlurFilter& operator=(const BlurFilter&&) = delete;
		~BlurFilter() override = default;

		//[[nodiscard]] ID3D12Resource* Output() const { return mBlurMap0.Get(); };

		void BuildDescriptors(
			CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc, 
			CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDesc,
			UINT descSize) override;

		void Execute(
			ID3D12GraphicsCommandList* cmdList,
			ID3D12RootSignature* rootSig,
			ID3D12PipelineState* horzBlurPso,
			ID3D12PipelineState* vertBlurPso,
			ID3D12Resource* input,
			int blurCnt, float sigmaSpace, float sigmaRange) const;

		static constexpr int SRV_UAV_COUNT = 4;

	private:
		static std::vector<float> CalcGaussWeight(float sigma, int& radius);

		void BuildDescriptors() const override;
		void BuildResources() override;

	private:
		static constexpr int MAX_BLUR_RADIUS = 5;

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
