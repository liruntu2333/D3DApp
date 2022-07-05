#pragma once

#include "D3DApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "FrameResource.h"

static constexpr int FRAME_RESOURCES_NUM = 3;

struct RenderItem
{
	RenderItem()                           = default;
	DirectX::XMFLOAT4X4 World              = MathHelper::Identity4x4();
	int NumFrameDirty                      = FRAME_RESOURCES_NUM;
	UINT ObjConstBuffIndex                 = -1;
	DX::MeshGeometry* Geometry             = nullptr;
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	UINT IndexCount                        = 0;
	UINT StartIndexLocation                = 0;
	int BaseVertexLocation                 = 0;
};

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
	void Update(const GameTimer& gameTimer) override;
	void Draw(const GameTimer& gameTimer) override;

	void OnMouseDown(WPARAM btnState, int x, int y) override;
	void OnMouseMove(WPARAM btnState, int x, int y) override;
	void OnMouseUp  (WPARAM btnState, int x, int y) override;

	void OnKeyboardInput         (const GameTimer& gameTimer);
	void UpdateCamera            (const GameTimer& gameTimer);
	void UpdateObjectConstBuffs   (const GameTimer& gameTimer) const;
	void UpdateMainPassConstBuffs(const GameTimer& gameTimer);

	void BuildDescriptorHeaps();
	void BuildConstantBufferViews() const;
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildSceneGeometry();
	void BuildPipelineStateObjects();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, 
		const std::vector<RenderItem*>& renderItems) const;

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mMsaaRenderTarget;
	Microsoft::WRL::ComPtr<ID3D12Resource> mMsaaDepthStencil;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mMsaaRTVDescHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mMsaaDSVDescHeap;

	unsigned int mSampleCount = 0;

	std::vector<std::unique_ptr<DX::FrameResource>> mFrameResources{};
	DX::FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mCbvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap;

	std::unordered_map<std::string, std::unique_ptr<DX::MeshGeometry>> mGeometries;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPipelineStateObjects;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout{};

	std::vector<std::unique_ptr<RenderItem>> mRenderItems{};
	std::vector<RenderItem*> mOpaqueRenderItems{};

	DX::PassConstants mMainPassConstBuff{};
	UINT mPassCbvOffset = 0;

	bool mIsWireframe = false;

	DirectX::XMFLOAT3 mEyePos{};
	DirectX::XMFLOAT4X4 mView = MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = 0.2f * DirectX::XM_PI;
	float mRadius = 15.0f;

	POINT mLastMousePos{};

	static constexpr int SAMPLE_COUNT_MAX = 8;
};
