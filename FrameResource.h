#pragma once

#include "D3DUtil.h"
#include "MathHelper.h"
#include "UploadBuffer.h"

namespace DX
{
	struct ObjectConstants
	{
		DirectX::XMFLOAT4X4 world = MathHelper::Identity4x4();
	};

	struct PassConstants
	{
		DirectX::XMFLOAT4X4 View              = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 InvView           = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 Proj              = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 InvProj           = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 ViewProj          = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 InvViewProj       = MathHelper::Identity4x4();
		DirectX::XMFLOAT3 EyePosW             = { 0.0f, 0.0f, 0.0f };
		float cbPerObjectPad1                 = 0.0f;
		DirectX::XMFLOAT2 RenderTargetSize    = { 0.0f, 0.0f };
		DirectX::XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
		float NearZ                           = 0.0f;
		float FarZ                            = 0.0f;
		float TotalTime                       = 0.0f;
		float DeltaTime                       = 0.0f;
	};

	struct Vertex
	{
		DirectX::XMFLOAT3 Pos;
		DirectX::XMFLOAT4 Color;
	};

	struct FrameResource
	{
		FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT waveVertCount);
		FrameResource(const FrameResource&) = delete;
		FrameResource(FrameResource&&) = delete;
		FrameResource& operator=(const FrameResource&) = delete;
		FrameResource& operator=(FrameResource&&) = delete;
		~FrameResource() = default;

		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

		std::unique_ptr<UploadBuffer<PassConstants>> PassConstBuff{};
		std::unique_ptr<UploadBuffer<ObjectConstants>> ObjConstBuff{};

		std::unique_ptr<UploadBuffer<Vertex>> WaveVtxBuff{};

		UINT64 Fence = 0;
	};
}
