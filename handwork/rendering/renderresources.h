// Rendering resources for pipeline useage.

#pragma once

#include "d3dutil.h"
#include "frameresource.h"
#include "ssao.h"
#include "shadowmap.h"
#include "deviceresources.h"
#include "camera.h"
#include "gametimer.h"

namespace handwork
{
	namespace rendering
	{
		// Divide rander items by pixel shader.
		enum class RenderLayer : int
		{
			Opaque = 0,
			WireFrame,
			OpaqueInst,
			WireFrameInst,
			Debug,
			Count
		};

		class RenderResources
		{
		public:
			RenderResources(const std::shared_ptr<DeviceResources>& deviceResource, const std::shared_ptr<Camera> camera,
				const std::shared_ptr<GameTimer> timer);
			void CreateDeviceDependentResources();
			void CreateWindowSizeDependentResources();
			void ReleaseDeviceDependentResources();
			void StartAddData();
			void FinishAddData();
			void Update();
			void Render();
			void SetLights(Light* lights);

			void AddMaterial(const Material& mat);
			void AddGeometryData(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices,
				const std::unordered_map<std::string, SubmeshGeometry>& drawArgs, const std::string& name);
			void AddRenderItem(const std::vector<RenderItemData>& renderItems, const RenderLayer layer);
			Material* GetMaterial(const std::string& name);
			MeshGeometry* GetMeshGeometry(const std::string& name);

		private:
			void BuildRootSignature();
			void BuildSsaoRootSignature();
			void BuildDescriptorHeaps();
			void BuildShadersAndInputLayout();
			void BuildPSOs();
			void BuildFrameResources();
			void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
			void DrawSceneToShadowMap();
			void DrawNormalsAndDepth();

			CD3DX12_CPU_DESCRIPTOR_HANDLE GetCpuSrv(int index) const;
			CD3DX12_GPU_DESCRIPTOR_HANDLE GetGpuSrv(int index) const;
			std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

			// Shared resources.
			std::shared_ptr<DeviceResources> mDeviceResources;
			std::shared_ptr<Camera> mCamera;
			std::shared_ptr<GameTimer> mGameTimer;

			// Cycled frame resources.
			std::vector<std::unique_ptr<FrameResource>> mFrameResources;
			FrameResource* mCurrFrameResource = nullptr;
			int mCurrFrameResourceIndex = 0;

			// Root signatures.
			Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
			Microsoft::WRL::ComPtr<ID3D12RootSignature> mSsaoRootSignature = nullptr;

			// Render resources.
			std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
			std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
			std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3DBlob>> mShaders;
			std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;

			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;
			std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

			// List of all the render items.
			std::vector<std::unique_ptr<RenderItem>> mAllRitems;
			// Render items divided by PSO.
			std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

			// Heap offset value.
			UINT mShadowMapHeapIndex = 0;
			UINT mSsaoHeapIndexStart = 0;
			UINT mSsaoAmbientMapIndex = 0;
			UINT mNullTexSrvIndex1 = 0;
			UINT mNullTexSrvIndex2 = 0;
			CD3DX12_GPU_DESCRIPTOR_HANDLE mNullSrv;

			PassConstants mMainPassCB;  // index 0 of pass cbuffer.
			PassConstants mShadowPassCB;// index 1 of pass cbuffer.

			// Shadow and ssao helpers.
			std::unique_ptr<ShadowMap> mShadowMap;
			std::unique_ptr<Ssao> mSsao;
			DirectX::BoundingSphere mSceneSphereBounds;
			DirectX::BoundingBox mSceneBoxBounds;

			// Light for shadow map.
			float mLightNearZ = 0.0f;
			float mLightFarZ = 0.0f;
			Vector3f mLightPosW;
			Matrix4x4 mLightView;
			Matrix4x4 mLightProj;
			Matrix4x4 mShadowTransform;

			// Current light
			Light mDirectLights[3];
			Vector4f mAmbientLight;

			// Manage new material and geometry.
			int mCurrentMatCBIndex;
			int mCurrentObjCBIndex;
			int mCurrentInstCBIndex;
		};

	}	// namespace rendering
}	// namespace handwork