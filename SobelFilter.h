#pragma once
#include "Filter.h"

namespace DX
{
	class SobelFilter final : public Filter
	{
	public:
		SobelFilter(ID3D12Device* device,
 UINT width,
			UINT height, DXGI_FORMAT format);
		SobelFilter(const SobelFilter&) = delete;
		SobelFilter(const SobelFilter&&) = delete;
		SobelFilter& operator=(const SobelFilter&) = delete;
		SobelFilter& operator=(const SobelFilter&&) = delete;
		~SobelFilter() override = default;

		void BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc, 
			CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDesc, UINT descSize) override;

		void Execute(
			ID3D12GraphicsCommandList* cmdList,
			ID3D12RootSignature* rootSig,
			ID3D12PipelineState* pso,
			CD3DX12_GPU_DESCRIPTOR_HANDLE input) const;

		[[nodiscard]] CD3DX12_GPU_DESCRIPTOR_HANDLE GetOutputSrv() const { return mhGpuSrv; }

		static constexpr int SRV_UAV_COUNT = 2;

	private:

		void BuildResources() override;
		void BuildDescriptors() const override;

		CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUav;

		CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUav;

		Microsoft::WRL::ComPtr<ID3D12Resource> mOutput;
	};
}
