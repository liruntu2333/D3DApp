#pragma once

#include "D3DUtil.h"
#include "GameTimer.h"

namespace DX
{
	class Waves
	{
	public:

		Waves(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, 
		     int m, int n, float dx, float dt, float speed, float damping);
		Waves(const Waves&) = delete;
		Waves(const Waves&&) = delete;
		Waves& operator=(const Waves&) = delete;
		Waves& operator=(const Waves&&) = delete;
		~Waves() = default;

		void BuildResource(ID3D12CommandQueue* cmdQueue);

		void BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDesc, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDesc,
			UINT descSize);

		[[nodiscard]] auto GetRowCount()        const { return mNumRows; }
		[[nodiscard]] auto GetColumnCount()     const { return mNumCols; }
		[[nodiscard]] auto GetVertexCount()     const { return mVertexCount; }
		[[nodiscard]] auto GetTriangleCount()   const { return mTriangleCount; }
		[[nodiscard]] auto GetWidth()           const { return static_cast<float>(mNumCols) * mSpatialStep; }
		[[nodiscard]] auto GetDepth()           const { return static_cast<float>(mNumRows) * mSpatialStep; }
		[[nodiscard]] auto GetSpatialStep()     const { return mSpatialStep; }
		[[nodiscard]] auto GetDisplacementMap() const { return mCurrSolSrv; }

		void Update(const GameTimer& gameTimer, ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig,
		            ID3D12PipelineState* pso);

		void Disturb(ID3D12GraphicsCommandList* cmdList, ID3D12RootSignature* rootSig, ID3D12PipelineState* pso,
					 UINT i, UINT j, float magnitude) const;

		static constexpr int DESCRIPTOR_COUNT = 6;

	private:
		UINT mNumRows;
		UINT mNumCols;
		UINT mVertexCount;
		UINT mTriangleCount;

		float mSimulationConstants[3]{};
		float mTimeStep;
		float mSpatialStep;

		ID3D12Device* md3dDevice;

		CD3DX12_GPU_DESCRIPTOR_HANDLE mPrevSolSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mCurrSolSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mNextSolSrv;

		CD3DX12_GPU_DESCRIPTOR_HANDLE mPrevSolUav;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mCurrSolUav;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mNextSolUav;

		Microsoft::WRL::ComPtr<ID3D12Resource> mPrevSol;
		Microsoft::WRL::ComPtr<ID3D12Resource> mCurrSol;
		Microsoft::WRL::ComPtr<ID3D12Resource> mNextSol;
	};
}
