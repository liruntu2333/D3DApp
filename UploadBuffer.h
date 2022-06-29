#pragma once

#include "D3DUtil.h"

namespace DX
{
	template<typename T>
	class UploadBuffer
	{
	public:
		UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer);

		UploadBuffer(const UploadBuffer& rhs) = delete;
		UploadBuffer(UploadBuffer&& rhs) = delete;
		UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
		UploadBuffer& operator=(UploadBuffer&& rhs) = delete;

		~UploadBuffer();

		[[nodiscard]] ID3D12Resource* Resource() const
		{
			return mUploadBuffer.Get();
		}

		void CopyData(int elementIndex, const T& data);

	private:
		Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer = nullptr;
		BYTE* mMappedData = nullptr;

		UINT mElementByteSize = 0;
		bool mIsConstantBuffer = false;
	};

	template <typename T>
	UploadBuffer<T>::UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
		mIsConstantBuffer(isConstantBuffer)
	{
		mElementByteSize = sizeof(T);

		if (isConstantBuffer)
		{
			mElementByteSize = CalcConstantBufferByteSize(sizeof(T));
		}

		auto uploadHeapType = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
		auto buffDesc = CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * static_cast<UINT64>(elementCount));
		ThrowIfFailed(device->CreateCommittedResource(
			&uploadHeapType,
			D3D12_HEAP_FLAG_NONE,
			&buffDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(mUploadBuffer.GetAddressOf())));

		ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));
	}

	template <typename T>
	UploadBuffer<T>::~UploadBuffer()
	{
		if (mUploadBuffer != nullptr)
		{
			mUploadBuffer->Unmap(0, nullptr);
		}

		mMappedData = nullptr;
	}

	template <typename T>
	void UploadBuffer<T>::CopyData(int elementIndex, const T& data)
	{
		memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
	}
}
