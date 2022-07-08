#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <DirectXCollision.h>
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <dxgi1_4.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <wrl.h>

#include "d3dx12.h"

namespace DX
{
	inline constexpr int SAMPLE_COUNT_MAX = 8;
	inline constexpr int FRAME_RESOURCES_NUM = 3;
	inline constexpr int LIGHT_COUNT_MAX = 16;

	class DxException
	{
	public:
		DxException() = default;
		DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

		[[nodiscard]] std::wstring ToString()const;

		HRESULT ErrorCode = S_OK;
		std::wstring FunctionName{};
		std::wstring Filename{};
		int LineNumber = -1;
	};

	inline UINT CalcConstantBufferByteSize(UINT byteSize)
	{
		// Constant buffers must be a multiple of the minimum hardware
		// allocation size (usually 256 bytes).  So round up to nearest
		// multiple of 256.  We do this by adding 255 and then masking off
		// the lower 2 bytes which store all bits < 256.
		// Example: Suppose byteSize = 300.
		// (300 + 255) & ~255
		// 555 & ~255
		// 0x022B & ~0x00ff
		// 0x022B & 0xff00
		// 0x0200
		// 512
		return (byteSize + 255) & ~255;
	}

	inline std::wstring AnsiToWString(const std::string& str)
	{
		WCHAR buffer[512];
		MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
		return std::wstring(buffer);
	}

	struct SubmeshGeometry
	{
		UINT IndexCount = 0;
		UINT StartIndexLocation = 0;
		INT BaseVertexLocation = 0;

		// DirectX::BoundingBox Bounds{};
	};

	struct MeshGeometry
	{
		std::string Name{};

		Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
		Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

		Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
		Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

		Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
		Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

		UINT VertexByteStride = 0;
		UINT VertexBufferByteSize = 0;
		DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
		UINT IndexBufferByteSize = 0;

		std::unordered_map<std::string, SubmeshGeometry> DrawArgs{};

		[[nodiscard]] D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const;

		[[nodiscard]] D3D12_INDEX_BUFFER_VIEW IndexBufferView() const;

		void DisposeUploaders();
	};

	struct Light
	{
		DirectX::XMFLOAT3 Intensity = { 0.5f,0.5f,0.5f };
		float AttenuationStart = 1.0f;	// point/spot
		DirectX::XMFLOAT3 Direction = { 0.0f,-1.0f,0.0f };	// spot/direction
		float AttenuationEnd = 10.0f; // point/spot
		DirectX::XMFLOAT3 Position = { 0.0f,0.0f,0.0f }; //point/spot
		float SpotPower = 64.0f; // spot
	};

	struct MaterialConstants
	{
		DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f,1.0f,1.0f,1.0f };
		DirectX::XMFLOAT3 FresnelR0 = { 0.01f,0.01f,0.1f };
		float Roughness = 0.25f;
		//DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
	};

	struct Material
	{
		std::string Name;

		int MatCbIndex = -1;
		int DiffuseSrvHeapIndex = -1;
		int NormalSrvHeapIndex = -1;
		int NumFrameDirty = FRAME_RESOURCES_NUM;

		DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
		float Roughness = 0.25f;
		//DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
	};

	Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entryPoint,
		const std::string& target);

	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		const void* initData,
		UINT64 byteSize,
		Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer);
}

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              	  \
{                                                                     	  \
    HRESULT hr__ = (x);                                               	  \
    std::wstring wfn = DX::AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DX::DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif