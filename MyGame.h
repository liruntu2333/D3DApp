#pragma once

#include "D3DApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"

struct ObjectConstants;

class MyGame final : public D3DApp
{
public:
	explicit MyGame(HINSTANCE hInstance);
	~MyGame() override;
	MyGame(const MyGame&) = delete;
	MyGame(MyGame&&) = delete;
	MyGame& operator=(const MyGame&) = delete;
	MyGame& operator=(MyGame&&) = delete;

	bool Initialize() override;

private:
	void OnResize() override;
	void Update(const GameTimer& gt) override;
	void Draw(const GameTimer& gt) override;

	void OnMouseDown(WPARAM btnState, int x, int y) override;
	void OnMouseMove(WPARAM btnState, int x, int y) override;
	void OnMouseUp(WPARAM btnState, int x, int y) override;

	void BuildDescriptorHeaps();
	void BuildConstantBuffers();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildSceneGeometry();
	void BuildPipelineStateObject();

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mMsaaRenderTarget;
	Microsoft::WRL::ComPtr<ID3D12Resource> mMsaaDepthStencil;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mMsaaRTVDescHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mMsaaDSVDescHeap;

	unsigned int mSampleCount = 0;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	std::unique_ptr<DX::UploadBuffer<ObjectConstants>> mObjConstBuff = nullptr;

	std::vector<std::unique_ptr<DX::MeshGeometry>> mScene{};

	Microsoft::WRL::ComPtr<ID3DBlob> mVSbyteCode = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob> mPSbyteCode = nullptr;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout{};
	Microsoft::WRL::ComPtr<ID3D12PipelineState> mPipelineStateObject{};

	DirectX::XMFLOAT4X4 mWorld = MathHelper::Identity4X4();
	DirectX::XMFLOAT4X4 mView = MathHelper::Identity4X4();
	DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4X4();

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = DirectX::XM_PIDIV4;
	float mRadius = 5.0f;

	POINT mLastMousePos{};
};
