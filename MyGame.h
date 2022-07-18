#pragma once

#include "D3DApp.h"
#include "MathHelper.h"
#include "UploadBuffer.h"
#include "FrameResource.h"
#include  "Waves.h"

struct RenderItem
{
	RenderItem() = default;
	DirectX::XMFLOAT4X4 World              = DX::MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 TexTransform       = DX::MathHelper::Identity4x4();
	int NumFrameDirty                      = DX::FRAME_RESOURCES_NUM;
	UINT ObjConstBuffIndex                 = -1;

	DX::Material* Material                 = nullptr;
	DX::MeshGeometry* Geometry             = nullptr;
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
	void AnimateMaterials		 (const GameTimer& gameTimer);
	void UpdateObjectConstBuffs   (const GameTimer& gameTimer) const;
	void UpdateMaterialConstBuffs(const GameTimer& gameTimer) const;
	void UpdateMainPassConstBuffs(const GameTimer& gameTimer);
	void UpdateWaves(const GameTimer& gameTimer) const;

	void LoadTextures();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildDescriptorHeaps();
	void BuildLandGeometry();
	void BuildWavesGeometryBuffers();
	void BuildBoxGeometry();
	void BuildPipelineStateObjects();
	void BuildFrameResources();
	void BuildMaterials();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, 
		const std::vector<RenderItem*>& renderItems) const;

	static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	static float GetHillsHeight(float x, float z);
	static DirectX::XMFLOAT3 GetHillsNormal(float x, float z);

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
	// Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mCbvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap;

	std::unordered_map<std::string, std::unique_ptr<DX::MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<DX::Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<DX::Texture>> mTextures;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPipelineStateObjects;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout{};

	RenderItem* mWavesRenderItem = nullptr;
	std::vector<std::unique_ptr<RenderItem>> mRenderItems{};
	std::vector<RenderItem*> mRenderItemLayer[static_cast<int>(RenderLayer::Count)]{};
	std::unique_ptr<DX::Waves> mWaves{};

	DX::PassConstants mMainPassConstBuff;

	bool mIsWireframe = false;

	DirectX::XMFLOAT3 mEyePos{};
	DirectX::XMFLOAT4X4 mView = DX::MathHelper::Identity4x4();
	DirectX::XMFLOAT4X4 mProj = DX::MathHelper::Identity4x4();

	float mTheta = 1.5f * DirectX::XM_PI;
	float mPhi = DirectX::XM_PIDIV2 - 0.1f;
	float mRadius = 50.0f;

	float mSunTheta = 1.25f * DirectX::XM_PI;
	float mSunPhi = DirectX::XM_PIDIV4;

	POINT mLastMousePos{};
};