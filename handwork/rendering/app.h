// Customize this app class to specific requirements.

#pragma once

#include "d3dapp.h"
#include "mathhelper.h"
#include "uploadbuffer.h"
#include "geogenerator.h"
#include "camera.h"
#include "frameresource.h"
#include "shadowmap.h"
#include "ssao.h"

namespace handwork
{
	// Divide rander items by pixel shader.
	enum class RenderLayer : int
	{
		Opaque = 0,
		Debug,
		Count
	};
		
	class App : public D3DApp
	{
	public:
		App(HINSTANCE hInstance);
		App(const App& rhs) = delete;
		App& operator=(const App& rhs) = delete;
		~App();

		virtual bool Initialize()override;

	private:
		virtual void CreateRtvAndDsvDescriptorHeaps()override;
		virtual void OnResize()override;
		virtual void Update(const GameTimer& gt)override;
		virtual void Draw(const GameTimer& gt)override;

		virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
		virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
		virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

		void OnKeyboardInput(const GameTimer& gt);
		void UpdateObjectCBs(const GameTimer& gt);
		void UpdateMaterialBuffer(const GameTimer& gt);
		void UpdateShadowTransform(const GameTimer& gt);
		void UpdateMainPassCB(const GameTimer& gt);
		void UpdateShadowPassCB(const GameTimer& gt);
		void UpdateSsaoCB(const GameTimer& gt);

		void BuildRootSignature();
		void BuildSsaoRootSignature();
		void BuildDescriptorHeaps();
		void BuildShadersAndInputLayout();
		void BuildShapeGeometry();
		void BuildPSOs();
		void BuildFrameResources();
		void BuildMaterials();
		void BuildRenderItems();
		void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
		void DrawSceneToShadowMap();
		void DrawNormalsAndDepth();

		CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index)const;
		CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv(int index)const;
		CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv(int index)const;
		CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv(int index)const;

		std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

	private:
		std::vector<std::unique_ptr<FrameResource>> mFrameResources;
		FrameResource* mCurrFrameResource = nullptr;
		int mCurrFrameResourceIndex = 0;

		Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> mSsaoRootSignature = nullptr;

		Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

		std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
		std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;

		std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

		// List of all the render items.
		std::vector<std::unique_ptr<RenderItem>> mAllRitems;
		// Render items divided by PSO.
		std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

		UINT mShadowMapHeapIndex = 0;
		UINT mSsaoHeapIndexStart = 0;
		UINT mSsaoAmbientMapIndex = 0;
		UINT mNullTexSrvIndex1 = 0;
		UINT mNullTexSrvIndex2 = 0;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

		PassConstants mMainPassCB;  // index 0 of pass cbuffer.
		PassConstants mShadowPassCB;// index 1 of pass cbuffer.

		Camera mCamera;
		std::unique_ptr<ShadowMap> mShadowMap;
		std::unique_ptr<Ssao> mSsao;

		DirectX::BoundingSphere mSceneBounds;

		float mLightNearZ = 0.0f;
		float mLightFarZ = 0.0f;
		DirectX::XMFLOAT3 mLightPosW;
		DirectX::XMFLOAT4X4 mLightView = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 mLightProj = MathHelper::Identity4x4();
		DirectX::XMFLOAT4X4 mShadowTransform = MathHelper::Identity4x4();

		float mLightRotationAngle = 0.0f;
		DirectX::XMFLOAT3 mBaseLightDirections[3] = {
			DirectX::XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
			DirectX::XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
			DirectX::XMFLOAT3(0.0f, -0.707f, -0.707f)
		};
		DirectX::XMFLOAT3 mRotatedLightDirections[3];

		POINT mLastMousePos;
	};

}	// namespace handwork
