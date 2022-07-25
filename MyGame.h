#pragma once

#include "D3DApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "FrameResource.h"

struct RenderItem
{
	RenderItem()                           = default;
	DirectX::XMFLOAT4X4 World              = DX::MathHelper::Identity4x4();
	int NumFrameDirty                      = DX::FRAME_RESOURCES_NUM;
	UINT ObjConstBuffIndex                 = -1;

	DX::Material* Mat					   = nullptr;
	DX::MeshGeometry* Geo                  = nullptr;
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	UINT IndexCount                        = 0;
	UINT StartIndexLocation                = 0;
	int BaseVertexLocation                 = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Count
};

class MyGame : public D3DApp
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

	void OnKeyboardInput          (const GameTimer& gameTimer);
	void UpdateCamera             (const GameTimer& gameTimer);
	void AnimateMaterials		  (const GameTimer& gameTimer);
	void UpdateObjectConstBuffs   (const GameTimer& gameTimer) const;
	void UpdateMaterialConstBuffs (const GameTimer& gameTimer) const;
	void UpdateMainPassConstBuffs (const GameTimer& gameTimer);

	void BuildDescriptorHeaps();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildSceneGeometry();
	void BuildSkullGeometry();
	void BuildPipelineStateObjects();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, 
		const std::vector<RenderItem*>& renderItems) const;

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mMsaaRenderTarget;
	Microsoft::WRL::ComPtr<ID3D12Resource> mMsaaDepthStencil;
	static const float RENDER_TARGET_CLEAN_VALUE[4];

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mMsaaRTVDescHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mMsaaDSVDescHeap;

	unsigned int mSampleCount = 0;

	std::vector<std::unique_ptr<DX::FrameResource>> mFrameResources{};
	DX::FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
	//Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mCbvHeap;
	//Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap;

	std::unordered_map<std::string, std::unique_ptr<DX::MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<DX::Material>> mMaterials;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPipelineStateObjects;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout{};

	std::vector<std::unique_ptr<RenderItem>> mRenderItems{};
	std::vector<RenderItem*> mRenderItemLayer[static_cast<int>(RenderLayer::Count)]{};

	DX::PassConstants mMainPassConstBuff{};

	bool mIsWireframe = false;

	DirectX::XMFLOAT3 mEyePos{};
	DirectX::XMFLOAT4X4 mView = DX::MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mProj = DX::MathHelper::Identity4x4();

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = DirectX::XM_PI * 0.2f;
	float mRadius = 15.0f;

	POINT mLastMousePos{};
};
