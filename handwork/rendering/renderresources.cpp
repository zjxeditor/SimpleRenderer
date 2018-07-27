// Rendering resources for pipeline useage.

#include "renderresources.h"
#include "geogenerator.h"

namespace handwork
{
	namespace rendering
	{
		using Microsoft::WRL::ComPtr;
		using namespace DirectX;
		using namespace DirectX::PackedVector;

		RenderResources::RenderResources(const std::shared_ptr<DeviceResources>& deviceResource, const std::shared_ptr<Camera> camera,
			const std::shared_ptr<GameTimer> timer, bool continousMode, bool depthOnlyMode) :
			mDeviceResources(deviceResource),
			mCamera(camera),
			mGameTimer(timer),
			mCurrentMatCBIndex(-1),
			mCurrentObjCBIndex(-1),
			mCurrentInstCBIndex(0),
			mContinousMode(continousMode),
			mDepthOnlyMode(depthOnlyMode)
		{
			mAmbientLight = { 0.4f, 0.4f, 0.6f, 1.0f };
			mDirectLights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
			mDirectLights[0].Strength = { 0.4f, 0.4f, 0.5f };
			mDirectLights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
			mDirectLights[1].Strength = { 0.1f, 0.1f, 0.1f };
			mDirectLights[2].Direction = { 0.0f, -0.707f, -0.707f };
			mDirectLights[2].Strength = { 0.0f, 0.0f, 0.0f };

			mSceneSphereBounds.Radius = -1.0f;
			mSceneBoxBounds.Extents = XMFLOAT3(-1.0f, -1.0f, -1.0f);

			CreateDeviceDependentResources();
		}

		void RenderResources::CreateDeviceDependentResources()
		{
			auto commandList = mDeviceResources->GetCommandList();
			auto commandQueue = mDeviceResources->GetCommandQueue();
			auto directCmdListAlloc = mDeviceResources->GetDirectCmdListAlloc();
			auto device = mDeviceResources->GetD3DDevice();
			auto renderSize = mDeviceResources->GetRenderTargetSize();

			// Reset the command list to prep for initialization commands.
			ThrowIfFailed(commandList->Reset(directCmdListAlloc, nullptr));

			mShadowMap.reset();
			mSsao.reset();
			mShadowMap = std::make_unique<ShadowMap>(device, 2048, 2048);
			mSsao = std::make_unique<Ssao>(device, commandList, renderSize.x, renderSize.y);

			BuildRootSignature();
			BuildSsaoRootSignature();
			BuildDescriptorHeaps();
			BuildShadersAndInputLayout();
			BuildPSOs();

			mSsao->SetPSOs(mPSOs["ssao"].Get(), mPSOs["ssaoBlur"].Get());

			// Execute the initialization commands.
			ThrowIfFailed(commandList->Close());
			ID3D12CommandList* cmdsLists[] = { commandList };
			commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

			// Wait until initialization is complete.
			mDeviceResources->FlushCommandQueue();
		}

		void RenderResources::CreateWindowSizeDependentResources()
		{
			Vector2i renderSize = mDeviceResources->GetRenderTargetSize();
			float aspect = (float)renderSize.x / renderSize.y;
			mCamera->SetLens(aspect);

			if (mSsao != nullptr)
			{
				mSsao->OnResize(renderSize.x, renderSize.y);
				// Resources changed, so need to rebuild descriptors.
				mSsao->RebuildDescriptors(mDeviceResources->DepthStencilBuffer());
			}
		}

		void RenderResources::StartAddData()
		{
			auto commandList = mDeviceResources->GetCommandList();
			auto directCmdListAlloc = mDeviceResources->GetDirectCmdListAlloc();
			// Reset the command list to prep for initialization commands.
			ThrowIfFailed(commandList->Reset(directCmdListAlloc, nullptr));
		}

		void RenderResources::FinishAddData()
		{
			auto commandList = mDeviceResources->GetCommandList();
			auto commandQueue = mDeviceResources->GetCommandQueue();

			BuildFrameResources();

			// Execute the initialization commands.
			ThrowIfFailed(commandList->Close());
			ID3D12CommandList* cmdsLists[] = { commandList };
			commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

			// Wait until initialization is complete.
			mDeviceResources->FlushCommandQueue();
		}

		void RenderResources::ReleaseDeviceDependentResources()
		{
			mFrameResources.clear();
			mCurrFrameResource = nullptr;
			mCurrFrameResourceIndex = 0;
			mRootSignature.Reset();
			mSsaoRootSignature.Reset();
			mGeometries.clear();
			mMaterials.clear();
			mShaders.clear();
			mPSOs.clear();
			mSrvDescriptorHeap.Reset();
			mAllRitems.clear();

			for (int i = 0; i < (int)RenderLayer::Count; ++i)
			{
				mRitemLayer[i].clear();
			}
			mShadowMap.reset();
			mSsao.reset();
		}

		void RenderResources::Update()
		{
			Vector2i renderSize = mDeviceResources->GetRenderTargetSize();

			// Cycle through the circular frame resource array.
			mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
			mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

			// Has the GPU finished processing the commands of the current frame resource?
			// If not, wait until the GPU has completed commands up to this fence point.
			if (mCurrFrameResource->Fence != 0 && mDeviceResources->GetFence()->GetCompletedValue() < mCurrFrameResource->Fence)
			{
				HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
				ThrowIfFailed(mDeviceResources->GetFence()->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
				WaitForSingleObject(eventHandle, INFINITE);
				CloseHandle(eventHandle);
			}

			// Update obj constant buffer and instance data.
			auto currObjectCB = mCurrFrameResource->ObjectCB.get();
			auto currInstCB = mCurrFrameResource->InstanceBuffer.get();
			for (auto& e : mAllRitems)
			{
				// Only update the cbuffer data if the constants have changed.  
				// This needs to be tracked per frame resource.
				if (e->NumFramesDirty <= 0)
					continue;

				if (e->Instances.size() == 0)
				{
					// No instancing.
					ObjectConstants objConstants;
					objConstants.World = e->World;
					objConstants.MaterialIndex = e->Mat->MatCBIndex;
					currObjectCB->CopyData(e->ObjCBIndex, objConstants);
				}
				else
				{
					// Instancing.
					currInstCB->CopyContinuousData(e->InstCBIndex, e->Instances.size(), &(e->Instances[0]));
				}

				// Next FrameResource need to be updated too.
				e->NumFramesDirty--;
			}

			// Update material constant buffer.
			auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
			for (auto& e : mMaterials)
			{
				// Only update the cbuffer data if the constants have changed.  If the cbuffer
				// data changes, it needs to be updated for each FrameResource.
				Material* mat = e.second.get();
				if (mat->NumFramesDirty > 0)
				{
					MaterialData matData;
					matData.DiffuseAlbedo = mat->DiffuseAlbedo;
					matData.FresnelR0 = mat->FresnelR0;
					matData.Roughness = mat->Roughness;

					currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

					// Next FrameResource need to be updated too.
					mat->NumFramesDirty--;
				}
			}

			// Update shadow transform.
			// Only the first "main" light casts a shadow.
			Vector3f lightDir = mDirectLights[0].Direction;
			Vector3f targetPos = Vector3f(mSceneSphereBounds.Center.x, mSceneSphereBounds.Center.y, mSceneSphereBounds.Center.z);
			Vector3f lightPos = targetPos - 2.0f* mSceneSphereBounds.Radius * lightDir;
			Vector3f lightUp = Vector3f(0.0f, 1.0f, 0.0f);
			Matrix4x4 lightView = d3dUtil::CameraLookAt(lightPos, targetPos, lightUp);
			mLightPosW = lightPos;

			// Transform bounding sphere to light space.
			Vector3f sphereCenterLS = Transform(lightView, Matrix4x4())(targetPos, VectorType::Point);

			// Ortho frustum in light space encloses scene.
			float l = sphereCenterLS.x - mSceneSphereBounds.Radius * 1.5f;
			float b = sphereCenterLS.y - mSceneSphereBounds.Radius * 1.5f;
			float n = sphereCenterLS.z - mSceneSphereBounds.Radius * 1.5f;
			float r = sphereCenterLS.x + mSceneSphereBounds.Radius * 1.5f;
			float t = sphereCenterLS.y + mSceneSphereBounds.Radius * 1.5f;
			float f = sphereCenterLS.z + mSceneSphereBounds.Radius * 1.5f;

			mLightNearZ = n;
			mLightFarZ = f;
			Matrix4x4 lightProj = OrthographicOffCenter(l, r, b, t, n, f).GetMatrix();

			// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
			Matrix4x4 T(
				0.5f, 0.0f, 0.0f, 0.5f,
				0.0f, -0.5f, 0.0f, 0.5f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f);

			Matrix4x4 S = Matrix4x4::Mul(Matrix4x4::Mul(T, lightProj), lightView);
			mLightView = lightView;
			mLightProj = lightProj;
			mShadowTransform = S;

			// Update main pass constant buffer.
			Matrix4x4 view = mCamera->GetView();
			Matrix4x4 proj = mCamera->GetProj();
			Matrix4x4 viewProj = Matrix4x4::Mul(proj, view);
			Matrix4x4 invView = Inverse(view);
			Matrix4x4 invProj = Inverse(proj);
			Matrix4x4 invViewProj = Inverse(viewProj);
			// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
			T = Matrix4x4(
				0.5f, 0.0f, 0.0f, 0.5f,
				0.0f, -0.5f, 0.0f, 0.5f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f);
			Matrix4x4 viewProjTex = Matrix4x4::Mul(T, viewProj);
			Matrix4x4 shadowTransform = mShadowTransform;

			mMainPassCB.View = view;
			mMainPassCB.InvView = invView;
			mMainPassCB.Proj = proj;
			mMainPassCB.InvProj = invProj;
			mMainPassCB.ViewProj = viewProj;
			mMainPassCB.InvViewProj = invViewProj;
			mMainPassCB.ViewProjTex = viewProjTex;
			mMainPassCB.ShadowTransform = shadowTransform;
			mMainPassCB.EyePosW = mCamera->GetPosition();
			mMainPassCB.RenderTargetSize = Vector2f((float)renderSize.x, (float)renderSize.y);
			mMainPassCB.InvRenderTargetSize = Vector2f(1.0f / renderSize.x, 1.0f / renderSize.y);
			mMainPassCB.NearZ = 1.0f;
			mMainPassCB.FarZ = 1000.0f;
			mMainPassCB.TotalTime = mGameTimer->TotalTime();
			mMainPassCB.DeltaTime = mGameTimer->DeltaTime();
			mMainPassCB.AmbientLight = mAmbientLight;
			mMainPassCB.Lights[0] = mDirectLights[0];
			mMainPassCB.Lights[1] = mDirectLights[1];
			mMainPassCB.Lights[2] = mDirectLights[2];

			auto currPassCB = mCurrFrameResource->PassCB.get();
			currPassCB->CopyData(0, mMainPassCB);

			// Update shadow pass constant buffer.
			view = mLightView;
			proj = mLightProj;
			viewProj = Matrix4x4::Mul(proj, view);
			invView = Inverse(view);
			invProj = Inverse(proj);
			invViewProj = Inverse(viewProj);
			UINT w = mShadowMap->Width();
			UINT h = mShadowMap->Height();
			mShadowPassCB.View = view;
			mShadowPassCB.InvView = invView;
			mShadowPassCB.Proj = proj;
			mShadowPassCB.InvProj = invProj;
			mShadowPassCB.ViewProj = viewProj;
			mShadowPassCB.InvViewProj = invViewProj;
			mShadowPassCB.EyePosW = mLightPosW;
			mShadowPassCB.RenderTargetSize = Vector2f((float)w, (float)h);
			mShadowPassCB.InvRenderTargetSize = Vector2f(1.0f / w, 1.0f / h);
			mShadowPassCB.NearZ = mLightNearZ;
			mShadowPassCB.FarZ = mLightFarZ;

			currPassCB = mCurrFrameResource->PassCB.get();
			currPassCB->CopyData(1, mShadowPassCB);

			// Update ssao constant buffer.
			SsaoConstants ssaoCB;
			Matrix4x4 P = mCamera->GetProj();
			// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
			T = Matrix4x4(
				0.5f, 0.0f, 0.0f, 0.5f,
				0.0f, -0.5f, 0.0f, 0.5f,
				0.0f, 0.0f, 1.0f, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f);
			ssaoCB.Proj = mMainPassCB.Proj;
			ssaoCB.InvProj = mMainPassCB.InvProj;
			ssaoCB.ProjTex = Matrix4x4::Mul(T, P);
			mSsao->GetOffsetVectors(ssaoCB.OffsetVectors);
			auto blurWeights = mSsao->CalcGaussWeights(2.5f);
			for (int i = 0; i < (int)blurWeights.size(); ++i)
			{
				int blurRow = i / 4;
				int blurCol = i - blurRow * 4;
				if (blurRow > 2 || blurCol > 3)
					continue;
				ssaoCB.BlurWeights[blurRow][blurCol] = blurWeights[i];
			}
			ssaoCB.InvRenderTargetSize = Vector2f(1.0f / mSsao->SsaoMapWidth(), 1.0f / mSsao->SsaoMapHeight());
			// Coordinates given in view space.
			ssaoCB.OcclusionRadius = 0.5f;
			ssaoCB.OcclusionFadeStart = 0.2f;
			ssaoCB.OcclusionFadeEnd = 1.0f;
			ssaoCB.SurfaceEpsilon = 0.05f;

			auto currSsaoCB = mCurrFrameResource->SsaoCB.get();
			currSsaoCB->CopyData(0, ssaoCB);
		}

		void RenderResources::Render()
		{
			auto commandList = mDeviceResources->GetCommandList();
			auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

			// Reuse the memory associated with command recording.
			// We can only reset when the associated command lists have finished execution on the GPU.
			ThrowIfFailed(cmdListAlloc->Reset());
			// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
			// Reusing the command list reuses memory.
			ThrowIfFailed(commandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

			ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
			commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

			ID3D12Resource* matBuffer;
			ID3D12Resource* instBuffer;
			if(!mDepthOnlyMode)
			{
				// Set base root signature.
				commandList->SetGraphicsRootSignature(mRootSignature.Get());
				// Bind all the materials used in this scene.  For structured buffers, we can bypass the heap and 
				// set as a root descriptor.
				matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
				commandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());
				instBuffer = mCurrFrameResource->InstanceBuffer->Resource();
				commandList->SetGraphicsRootShaderResourceView(3, instBuffer->GetGPUVirtualAddress());
				// Bind null SRV for shadow map pass.
				commandList->SetGraphicsRootDescriptorTable(4, mNullSrv);

				//
				// Shadow map pass.
				//

				DrawSceneToShadowMap();

				//
				// Normal/depth pass.
				//

				DrawNormalsAndDepth();

				//
				// Compute SSAO.
				// 

				commandList->SetGraphicsRootSignature(mSsaoRootSignature.Get());
				mSsao->ComputeSsao(commandList, mCurrFrameResource, 3);
			}

			//
			// Main rendering pass.
			//

			// Rebind state whenever graphics root signature changes.
			// Bind all the materials used in this scene. For structured buffers, we can bypass the heap and 
			// set as a root descriptor.
			commandList->SetGraphicsRootSignature(mRootSignature.Get());
			matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
			commandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());
			instBuffer = mCurrFrameResource->InstanceBuffer->Resource();
			commandList->SetGraphicsRootShaderResourceView(3, instBuffer->GetGPUVirtualAddress());
			auto passCB = mCurrFrameResource->PassCB->Resource();
			commandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());
			if(mDepthOnlyMode)
			{
				// Bind null SRV for shadow map pass.
				commandList->SetGraphicsRootDescriptorTable(4, mNullSrv);
			}
			else
			{
				CD3DX12_GPU_DESCRIPTOR_HANDLE shadowMapDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
				shadowMapDescriptor.Offset(mShadowMapHeapIndex, mDeviceResources->GetCbvSrvUavSize());
				commandList->SetGraphicsRootDescriptorTable(4, shadowMapDescriptor);
			}

			mDeviceResources->PreparePresent(mDepthOnlyMode);

			if(mDepthOnlyMode)
			{
				if (mRitemLayer[(int)RenderLayer::Opaque].size() != 0)
				{
					commandList->SetPipelineState(mPSOs["depth_opaque"].Get());
					DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::Opaque]);
				}
				if (mRitemLayer[(int)RenderLayer::OpaqueInst].size() != 0)
				{
					commandList->SetPipelineState(mPSOs["depthInst_opaque"].Get());
					DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::OpaqueInst]);
				}
			}
			else
			{
				if (mRitemLayer[(int)RenderLayer::Opaque].size() != 0)
				{
					commandList->SetPipelineState(mPSOs["opaque"].Get());
					DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::Opaque]);
				}
				if (mRitemLayer[(int)RenderLayer::OpaqueInst].size() != 0)
				{
					commandList->SetPipelineState(mPSOs["opaqueInst"].Get());
					DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::OpaqueInst]);
				}
				if (mRitemLayer[(int)RenderLayer::WireFrame].size() != 0)
				{
					commandList->SetPipelineState(mPSOs["opaque_wireframe"].Get());
					DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::WireFrame]);
				}
				if (mRitemLayer[(int)RenderLayer::WireFrameInst].size() != 0)
				{
					commandList->SetPipelineState(mPSOs["opaqueInst_wireframe"].Get());
					DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::WireFrameInst]);
				}
				if (mRitemLayer[(int)RenderLayer::Debug].size() != 0)
				{
					commandList->SetPipelineState(mPSOs["debug"].Get());
					DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::Debug]);
				}
			}

			if (mContinousMode)
				mDeviceResources->Present(mCurrFrameResource->Fence);
			else
			{
				// Indicate a state transition on the resource usage.
				if (mDeviceResources->GetMsaaQuality() <= 0)
				{
					commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDeviceResources->CurrentBackBuffer(),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
				}
				else
				{
					D3D12_RESOURCE_BARRIER barriers[2] =
					{
						CD3DX12_RESOURCE_BARRIER::Transition(mDeviceResources->CurrentOffScreenBuffer(),
						D3D12_RESOURCE_STATE_RENDER_TARGET,	D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
						CD3DX12_RESOURCE_BARRIER::Transition(mDeviceResources->CurrentBackBuffer(),
						D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST)
					};
					commandList->ResourceBarrier(2, barriers);

					commandList->ResolveSubresource(mDeviceResources->CurrentBackBuffer(), 0,
						mDeviceResources->CurrentOffScreenBuffer(), 0, mDeviceResources->GetBackBufferFormat());

					D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						mDeviceResources->CurrentBackBuffer(),
						D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PRESENT);
					commandList->ResourceBarrier(1, &barrier);
				}

				// Done recording commands.
				ThrowIfFailed(commandList->Close());
				// Add the command list to the queue for execution.
				ID3D12CommandList* cmdsLists[] = { commandList };
				mDeviceResources->GetCommandQueue()->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

				mDeviceResources->ManualSwapBackBuffers();
				mCurrFrameResource->Fence++;

				// Wait to finish.
				mDeviceResources->FlushCommandQueue();
			}
		}

		void RenderResources::SetLights(Light* lights)
		{
			mDirectLights[0] = lights[0];
			mDirectLights[1] = lights[1];
			mDirectLights[2] = lights[2];
		}

		void RenderResources::AddMaterial(const Material& mat)
		{
			auto matPtr = std::make_unique<Material>();
			matPtr->Name = mat.Name;
			matPtr->DiffuseAlbedo = mat.DiffuseAlbedo;
			matPtr->FresnelR0 = mat.FresnelR0;
			matPtr->Roughness = mat.Roughness;

			if (mMaterials.find(matPtr->Name) != mMaterials.end())
			{
				// Replace existing material.
				matPtr->MatCBIndex = mMaterials[matPtr->Name]->MatCBIndex;
				mMaterials.erase(matPtr->Name);
				mMaterials[matPtr->Name] = std::move(matPtr);
			}
			else
			{
				// Add new material.
				matPtr->MatCBIndex = ++mCurrentMatCBIndex;
				mMaterials[matPtr->Name] = std::move(matPtr);
			}
		}

		void RenderResources::AddGeometryData(
			const std::vector<Vertex>& vertices,
			const std::vector<std::uint32_t>& indices,
			const std::unordered_map<std::string, SubmeshGeometry>& drawArgs,
			const std::string& name)
		{
			bool useIndices16 = false;
			if (vertices.size() <= 65536)
				useIndices16 = true;
			std::vector<std::uint16_t> indices16;
			if (useIndices16)
			{
				indices16.resize(indices.size());
				for (size_t i = 0; i < indices.size(); ++i)
					indices16[i] = static_cast<std::uint16_t>(indices[i]);
			}

			const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
			const UINT ibByteSize = useIndices16 ? (UINT)indices16.size() * sizeof(std::uint16_t) : (UINT)indices.size() * sizeof(std::uint32_t);

			auto geo = std::make_unique<MeshGeometry>();
			geo->Name = name;

			ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
			CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
			ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
			if (useIndices16)
				CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices16.data(), ibByteSize);
			else
				CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

			ID3D12Device* device = mDeviceResources->GetD3DDevice();
			ID3D12GraphicsCommandList* commandList = mDeviceResources->GetCommandList();

			geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
				commandList, vertices.data(), vbByteSize, geo->VertexBufferUploader);
			if (useIndices16)
				geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
					commandList, indices16.data(), ibByteSize, geo->IndexBufferUploader);
			else
				geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(device,
					commandList, indices.data(), ibByteSize, geo->IndexBufferUploader);

			geo->VertexByteStride = sizeof(Vertex);
			geo->VertexBufferByteSize = vbByteSize;
			geo->IndexFormat = useIndices16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
			geo->IndexBufferByteSize = ibByteSize;

			// Calculate submesh bounds.
			geo->DrawArgs = drawArgs;
			for (auto& e : geo->DrawArgs)
			{
				BoundingBox::CreateFromPoints(e.second.BoxBounds, e.second.VertexCount, reinterpret_cast<const XMFLOAT3*>(&vertices[e.second.BaseVertexLocation].Pos), sizeof(Vertex));
				BoundingSphere::CreateFromPoints(e.second.SphereBounds, e.second.VertexCount, reinterpret_cast<const XMFLOAT3*>(&vertices[e.second.BaseVertexLocation].Pos), sizeof(Vertex));
			}

			if (mGeometries.find(geo->Name) != mGeometries.end())
				mGeometries.erase(geo->Name);
			mGeometries[geo->Name] = std::move(geo);
		}

		void RenderResources::AddRenderItem(const std::vector<RenderItemData>& renderItems, const RenderLayer layer)
		{
			BoundingBox tempBox;
			BoundingSphere tempSphere;
			XMFLOAT4X4 tempCache;
			for (auto& e : renderItems)
			{
				auto item = std::make_unique<RenderItem>();
				item->Name = e.Name;
				item->Geo = mGeometries[e.GeoName].get();
				item->SubMesh = &(item->Geo->DrawArgs[e.DrawArgName]);
				item->PrimitiveType = e.PrimitiveType;
				item->IndexCount = item->SubMesh->IndexCount;
				item->StartIndexLocation = item->SubMesh->StartIndexLocation;
				item->BaseVertexLocation = item->SubMesh->BaseVertexLocation;
				if (e.Instances.size() == 0)
				{
					// No instancing.
					item->World = e.World;
					item->ObjCBIndex = ++mCurrentObjCBIndex;
					item->Mat = mMaterials[e.MatName].get();
				}
				else
				{
					// Instancing.
					int numInst = (int)e.Instances.size();
					item->Instances.resize(numInst);
					for (int i = 0; i < numInst; ++i)
					{
						item->Instances[i].World = e.Instances[i].World;
						item->Instances[i].MaterialIndex = mMaterials[e.Instances[i].MatName]->MatCBIndex;
					}
					item->InstCBIndex = mCurrentInstCBIndex;
					mCurrentInstCBIndex += numInst;
				}

				// Update scene bounds.
				if (layer != RenderLayer::Debug && layer != RenderLayer::Count)
				{
					if (item->Instances.size() == 0)
					{
						// No instancing.
						tempCache = d3dUtil::ConvertToXMFLOAT4x4(item->World);
						const XMMATRIX transform = XMLoadFloat4x4(&tempCache);
						BoundingBox box = item->SubMesh->BoxBounds;
						box.Transform(tempBox, transform);
						BoundingSphere sphere = item->SubMesh->SphereBounds;
						sphere.Transform(tempSphere, transform);

						if (mSceneSphereBounds.Radius <= 0.0f)
						{
							mSceneBoxBounds = tempBox;
							mSceneSphereBounds = tempSphere;
						}
						else
						{
							BoundingBox::CreateMerged(mSceneBoxBounds, mSceneBoxBounds, tempBox);
							BoundingSphere::CreateMerged(mSceneSphereBounds, mSceneSphereBounds, tempSphere);
						}
					}
					else
					{
						// Instancing.
						for (auto& inst : item->Instances)
						{
							tempCache = d3dUtil::ConvertToXMFLOAT4x4(inst.World);
							const XMMATRIX transform = XMLoadFloat4x4(&tempCache);
							BoundingBox box = item->SubMesh->BoxBounds;
							box.Transform(tempBox, transform);
							BoundingSphere sphere = item->SubMesh->SphereBounds;
							sphere.Transform(tempSphere, transform);

							if (mSceneSphereBounds.Radius <= 0.0f)
							{
								mSceneBoxBounds = tempBox;
								mSceneSphereBounds = tempSphere;
							}
							else
							{
								BoundingBox::CreateMerged(mSceneBoxBounds, mSceneBoxBounds, tempBox);
								BoundingSphere::CreateMerged(mSceneSphereBounds, mSceneSphereBounds, tempSphere);
							}
						}
					}
				}

				mRitemLayer[(int)layer].push_back(item.get());
				mAllRitems.push_back(std::move(item));
			}
		}

		Material* RenderResources::GetMaterial(const std::string& name)
		{
			if (name.empty())
				return nullptr;
			if (mMaterials.find(name) != mMaterials.end())
				return mMaterials[name].get();
			else
				return nullptr;
		}

		MeshGeometry* RenderResources::GetMeshGeometry(const std::string& name)
		{
			if (name.empty())
				return nullptr;
			if (mGeometries.find(name) != mGeometries.end())
				return mGeometries[name].get();
			else
				return nullptr;
		}

		RenderItem* RenderResources::GetRenderItem(const std::string& name, const RenderLayer layer)
		{
			if (name.empty())
				return nullptr;
			for (auto& item : mRitemLayer[(int)layer])
			{
				if (item->Name == name)
					return item;
			}
			return nullptr;
		}

		void RenderResources::BuildRootSignature()
		{
			CD3DX12_DESCRIPTOR_RANGE texTable0;
			texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 2, 0);

			// Root parameter can be a table, root descriptor or root constants.
			CD3DX12_ROOT_PARAMETER slotRootParameter[5];

			// Perfomance TIP: Order from most frequent to least frequent.
			slotRootParameter[0].InitAsConstantBufferView(0);
			slotRootParameter[1].InitAsConstantBufferView(1);
			slotRootParameter[2].InitAsShaderResourceView(0);
			slotRootParameter[3].InitAsShaderResourceView(1);
			slotRootParameter[4].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);

			auto staticSamplers = GetStaticSamplers(); 

			// A root signature is an array of root parameters.
			CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter,
				(UINT)staticSamplers.size(), staticSamplers.data(),
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
			ComPtr<ID3DBlob> serializedRootSig = nullptr;
			ComPtr<ID3DBlob> errorBlob = nullptr;
			HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
				serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

			if (errorBlob != nullptr)
			{
				::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
			}
			ThrowIfFailed(hr);

			ThrowIfFailed(mDeviceResources->GetD3DDevice()->CreateRootSignature(
				0,
				serializedRootSig->GetBufferPointer(),
				serializedRootSig->GetBufferSize(),
				IID_PPV_ARGS(mRootSignature.GetAddressOf())));
		}

		void RenderResources::BuildSsaoRootSignature()
		{
			CD3DX12_DESCRIPTOR_RANGE texTable0;
			texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

			CD3DX12_DESCRIPTOR_RANGE texTable1;
			texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

			// Root parameter can be a table, root descriptor or root constants.
			CD3DX12_ROOT_PARAMETER slotRootParameter[4];

			// Perfomance TIP: Order from most frequent to least frequent.
			slotRootParameter[0].InitAsConstantBufferView(0);
			slotRootParameter[1].InitAsConstants(1, 1);
			slotRootParameter[2].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
			slotRootParameter[3].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

			const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
				0, // shaderRegister
				D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

			const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
				1, // shaderRegister
				D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

			const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
				2, // shaderRegister
				D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
				D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
				0.0f,
				0,
				D3D12_COMPARISON_FUNC_LESS_EQUAL,
				D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

			const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
				3, // shaderRegister
				D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

			std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
			{
				pointClamp, linearClamp, depthMapSam, linearWrap
			};

			// A root signature is an array of root parameters.
			CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
				(UINT)staticSamplers.size(), staticSamplers.data(),
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

			// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
			ComPtr<ID3DBlob> serializedRootSig = nullptr;
			ComPtr<ID3DBlob> errorBlob = nullptr;
			HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
				serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

			if (errorBlob != nullptr)
			{
				::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
			}
			ThrowIfFailed(hr);

			ThrowIfFailed(mDeviceResources->GetD3DDevice()->CreateRootSignature(
				0,
				serializedRootSig->GetBufferPointer(),
				serializedRootSig->GetBufferSize(),
				IID_PPV_ARGS(mSsaoRootSignature.GetAddressOf())));
		}

		void RenderResources::BuildDescriptorHeaps()
		{
			//
			// Create the SRV heap.
			//
			ID3D12Device* device = mDeviceResources->GetD3DDevice();
			UINT cbvSrvUavSize = mDeviceResources->GetCbvSrvUavSize();
			UINT rtvSize = mDeviceResources->GetRtvSize();

			// 1 for shadow, 3 for ssao, 2 for ssao ambient, 2 for null place holder.
			D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
			srvHeapDesc.NumDescriptors = 8;
			srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

			mShadowMapHeapIndex = 0;
			mSsaoHeapIndexStart = mShadowMapHeapIndex + 1;
			mSsaoAmbientMapIndex = mSsaoHeapIndexStart + 3;
			mNullTexSrvIndex1 = mSsaoHeapIndexStart + 5;
			mNullTexSrvIndex2 = mNullTexSrvIndex1 + 1;

			auto nullSrv = GetCpuSrv(mNullTexSrvIndex1);
			mNullSrv = GetGpuSrv(mNullTexSrvIndex1);

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
			device->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
			nullSrv.Offset(1, cbvSrvUavSize);
			device->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

			mShadowMap->BuildDescriptors(
				GetCpuSrv(mShadowMapHeapIndex),
				GetGpuSrv(mShadowMapHeapIndex),
				mDeviceResources->GetDsv(2));

			mSsao->BuildDescriptors(
				mDeviceResources->DepthStencilBuffer(),
				GetCpuSrv(mSsaoHeapIndexStart),
				GetGpuSrv(mSsaoHeapIndexStart),
				mDeviceResources->GetRtv(mDeviceResources->GetSwapChainBufferCount()),
				cbvSrvUavSize,
				rtvSize);
		}

		void RenderResources::BuildShadersAndInputLayout()
		{
			mShaders["standardVS"] = d3dUtil::CompileShader(L"shaders\\default.hlsl", nullptr, "VS", "vs_5_1");
			mShaders["opaquePS"] = d3dUtil::CompileShader(L"shaders\\default.hlsl", nullptr, "PS", "ps_5_1");
			mShaders["standardInstVS"] = d3dUtil::CompileShader(L"shaders\\default.hlsl", nullptr, "VSInst", "vs_5_1");
			mShaders["opaqueInstPS"] = d3dUtil::CompileShader(L"shaders\\default.hlsl", nullptr, "PSInst", "ps_5_1");

			mShaders["shadowVS"] = d3dUtil::CompileShader(L"shaders\\shadows.hlsl", nullptr, "VS", "vs_5_1");
			mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"shaders\\shadows.hlsl", nullptr, "PS", "ps_5_1");
			mShaders["shadowInstVS"] = d3dUtil::CompileShader(L"shaders\\shadows.hlsl", nullptr, "VSInst", "vs_5_1");

			mShaders["depthVS"] = d3dUtil::CompileShader(L"shaders\\depth.hlsl", nullptr, "VS", "vs_5_1");
			mShaders["depthOpaquePS"] = d3dUtil::CompileShader(L"shaders\\depth.hlsl", nullptr, "PS", "ps_5_1");
			mShaders["depthInstVS"] = d3dUtil::CompileShader(L"shaders\\depth.hlsl", nullptr, "VSInst", "vs_5_1");

			mShaders["debugVS"] = d3dUtil::CompileShader(L"shaders\\debug.hlsl", nullptr, "VS", "vs_5_1");
			mShaders["debugPS"] = d3dUtil::CompileShader(L"shaders\\debug.hlsl", nullptr, "PS", "ps_5_1");

			mShaders["drawNormalsVS"] = d3dUtil::CompileShader(L"shaders\\drawnormals.hlsl", nullptr, "VS", "vs_5_1");
			mShaders["drawNormalsPS"] = d3dUtil::CompileShader(L"shaders\\drawnormals.hlsl", nullptr, "PS", "ps_5_1");
			mShaders["drawNormalsInstVS"] = d3dUtil::CompileShader(L"shaders\\drawnormals.hlsl", nullptr, "VSInst", "vs_5_1");

			mShaders["ssaoVS"] = d3dUtil::CompileShader(L"shaders\\ssao.hlsl", nullptr, "VS", "vs_5_1");
			mShaders["ssaoPS"] = d3dUtil::CompileShader(L"shaders\\ssao.hlsl", nullptr, "PS", "ps_5_1");

			mShaders["ssaoBlurVS"] = d3dUtil::CompileShader(L"shaders\\ssaoBlur.hlsl", nullptr, "VS", "vs_5_1");
			mShaders["ssaoBlurPS"] = d3dUtil::CompileShader(L"shaders\\ssaoBlur.hlsl", nullptr, "PS", "ps_5_1");

			mInputLayout =
			{
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			};
		}

		void RenderResources::BuildPSOs()
		{
			UINT msaaCount = mDeviceResources->GetMsaaCount();
			UINT msaaQuality = mDeviceResources->GetMsaaQuality();
			ID3D12Device* device = mDeviceResources->GetD3DDevice();

			D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

			ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
			basePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
			basePsoDesc.pRootSignature = mRootSignature.Get();
			basePsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
				mShaders["standardVS"]->GetBufferSize()
			};
			basePsoDesc.PS =
			{
				reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
				mShaders["opaquePS"]->GetBufferSize()
			};
			basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
			basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
			basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
			basePsoDesc.SampleMask = UINT_MAX;
			basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			basePsoDesc.NumRenderTargets = 1;
			basePsoDesc.RTVFormats[0] = mDeviceResources->GetBackBufferFormat();
			basePsoDesc.SampleDesc.Count = 1;
			basePsoDesc.SampleDesc.Quality = 0;
			basePsoDesc.DSVFormat = mDeviceResources->GetDepthStencilFormat();

			//
			// PSO for opaque objects.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = basePsoDesc;
			if (msaaQuality > 0)
			{
				opaquePsoDesc.RasterizerState.MultisampleEnable = true;
				opaquePsoDesc.SampleDesc.Count = msaaCount;
				opaquePsoDesc.SampleDesc.Quality = msaaQuality - 1;
			}
			else
			{
				opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
				opaquePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			}
			ThrowIfFailed(device->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));
			opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
			ThrowIfFailed(device->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));

			//
			// PSO for opaque instance objects.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueInstPsoDesc = opaquePsoDesc;
			opaqueInstPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
			opaqueInstPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["standardInstVS"]->GetBufferPointer()),
				mShaders["standardInstVS"]->GetBufferSize()
			};
			opaqueInstPsoDesc.PS =
			{
				reinterpret_cast<BYTE*>(mShaders["opaqueInstPS"]->GetBufferPointer()),
				mShaders["opaqueInstPS"]->GetBufferSize()
			};
			ThrowIfFailed(device->CreateGraphicsPipelineState(&opaqueInstPsoDesc, IID_PPV_ARGS(&mPSOs["opaqueInst"])));
			opaqueInstPsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
			ThrowIfFailed(device->CreateGraphicsPipelineState(&opaqueInstPsoDesc, IID_PPV_ARGS(&mPSOs["opaqueInst_wireframe"])));

			//
			// PSO for depth opaque objects.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC depthPsoDesc = basePsoDesc;
			depthPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["depthVS"]->GetBufferPointer()),
				mShaders["depthVS"]->GetBufferSize()
			};
			depthPsoDesc.PS =
			{
				reinterpret_cast<BYTE*>(mShaders["depthOpaquePS"]->GetBufferPointer()),
				mShaders["depthOpaquePS"]->GetBufferSize()
			};
			if (msaaQuality > 0)
			{
				depthPsoDesc.RasterizerState.MultisampleEnable = true;
				depthPsoDesc.SampleDesc.Count = msaaCount;
				depthPsoDesc.SampleDesc.Quality = msaaQuality - 1;
			}
			ThrowIfFailed(device->CreateGraphicsPipelineState(&depthPsoDesc, IID_PPV_ARGS(&mPSOs["depth_opaque"])));

			//
			// PSO for depth instance objects.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC depthInstPsoDesc = depthPsoDesc;
			depthInstPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["depthInstVS"]->GetBufferPointer()),
				mShaders["depthInstVS"]->GetBufferSize()
			};
			ThrowIfFailed(device->CreateGraphicsPipelineState(&depthInstPsoDesc, IID_PPV_ARGS(&mPSOs["depthInst_opaque"])));


			//
			// PSO for shadow map pass.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC smapPsoDesc = basePsoDesc;
			smapPsoDesc.RasterizerState.DepthBias = 100000;
			smapPsoDesc.RasterizerState.DepthBiasClamp = 0.0f;
			smapPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
			smapPsoDesc.pRootSignature = mRootSignature.Get();
			smapPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["shadowVS"]->GetBufferPointer()),
				mShaders["shadowVS"]->GetBufferSize()
			};
			smapPsoDesc.PS =
			{
				reinterpret_cast<BYTE*>(mShaders["shadowOpaquePS"]->GetBufferPointer()),
				mShaders["shadowOpaquePS"]->GetBufferSize()
			};
			// Shadow map pass does not have a render target.
			smapPsoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
			smapPsoDesc.NumRenderTargets = 0;
			ThrowIfFailed(device->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));

			//
			// PSO for instance shadow map pass.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC smapInstPsoDesc = smapPsoDesc;
			smapInstPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["shadowInstVS"]->GetBufferPointer()),
				mShaders["shadowInstVS"]->GetBufferSize()
			};
			ThrowIfFailed(device->CreateGraphicsPipelineState(&smapInstPsoDesc, IID_PPV_ARGS(&mPSOs["shadowInst_opaque"])));

			//
			// PSO for debug layer.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = basePsoDesc;
			debugPsoDesc.pRootSignature = mRootSignature.Get();
			debugPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["debugVS"]->GetBufferPointer()),
				mShaders["debugVS"]->GetBufferSize()
			};
			debugPsoDesc.PS =
			{
				reinterpret_cast<BYTE*>(mShaders["debugPS"]->GetBufferPointer()),
				mShaders["debugPS"]->GetBufferSize()
			};
			if (msaaQuality > 0)
			{
				debugPsoDesc.RasterizerState.MultisampleEnable = true;
				debugPsoDesc.SampleDesc.Count = msaaCount;
				debugPsoDesc.SampleDesc.Quality = msaaQuality - 1;
			}
			ThrowIfFailed(device->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));

			//
			// PSO for drawing normals.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC drawNormalsPsoDesc = basePsoDesc;
			drawNormalsPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["drawNormalsVS"]->GetBufferPointer()),
				mShaders["drawNormalsVS"]->GetBufferSize()
			};
			drawNormalsPsoDesc.PS =
			{
				reinterpret_cast<BYTE*>(mShaders["drawNormalsPS"]->GetBufferPointer()),
				mShaders["drawNormalsPS"]->GetBufferSize()
			};
			drawNormalsPsoDesc.RTVFormats[0] = Ssao::NormalMapFormat;
			drawNormalsPsoDesc.SampleDesc.Count = 1;
			drawNormalsPsoDesc.SampleDesc.Quality = 0;
			drawNormalsPsoDesc.DSVFormat = mDeviceResources->GetDepthStencilFormat();
			ThrowIfFailed(device->CreateGraphicsPipelineState(&drawNormalsPsoDesc, IID_PPV_ARGS(&mPSOs["drawNormals"])));

			//
			// PSO for instance drawing normals.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC drawNormalsInstPsoDesc = drawNormalsPsoDesc;
			drawNormalsInstPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["drawNormalsInstVS"]->GetBufferPointer()),
				mShaders["drawNormalsInstVS"]->GetBufferSize()
			};
			ThrowIfFailed(device->CreateGraphicsPipelineState(&drawNormalsInstPsoDesc, IID_PPV_ARGS(&mPSOs["drawNormalsInst"])));

			//
			// PSO for SSAO.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoPsoDesc = basePsoDesc;
			ssaoPsoDesc.InputLayout = { nullptr, 0 };
			ssaoPsoDesc.pRootSignature = mSsaoRootSignature.Get();
			ssaoPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["ssaoVS"]->GetBufferPointer()),
				mShaders["ssaoVS"]->GetBufferSize()
			};
			ssaoPsoDesc.PS =
			{
				reinterpret_cast<BYTE*>(mShaders["ssaoPS"]->GetBufferPointer()),
				mShaders["ssaoPS"]->GetBufferSize()
			};
			// SSAO effect does not need the depth buffer.
			ssaoPsoDesc.DepthStencilState.DepthEnable = false;
			ssaoPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
			ssaoPsoDesc.RTVFormats[0] = Ssao::AmbientMapFormat;
			ssaoPsoDesc.SampleDesc.Count = 1;
			ssaoPsoDesc.SampleDesc.Quality = 0;
			ssaoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
			ThrowIfFailed(device->CreateGraphicsPipelineState(&ssaoPsoDesc, IID_PPV_ARGS(&mPSOs["ssao"])));

			//
			// PSO for SSAO blur.
			//

			D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoBlurPsoDesc = ssaoPsoDesc;
			ssaoBlurPsoDesc.VS =
			{
				reinterpret_cast<BYTE*>(mShaders["ssaoBlurVS"]->GetBufferPointer()),
				mShaders["ssaoBlurVS"]->GetBufferSize()
			};
			ssaoBlurPsoDesc.PS =
			{
				reinterpret_cast<BYTE*>(mShaders["ssaoBlurPS"]->GetBufferPointer()),
				mShaders["ssaoBlurPS"]->GetBufferSize()
			};
			ThrowIfFailed(device->CreateGraphicsPipelineState(&ssaoBlurPsoDesc, IID_PPV_ARGS(&mPSOs["ssaoBlur"])));
		}

		void RenderResources::BuildFrameResources()
		{
			for (int i = 0; i < gNumFrameResources; ++i)
			{
				mFrameResources.push_back(std::make_unique<FrameResource>(mDeviceResources->GetD3DDevice(),
					2, mCurrentObjCBIndex + 1, mCurrentMatCBIndex + 1, mCurrentInstCBIndex));
			}
		}

		void RenderResources::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
		{
			UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
			UINT instElementSize = sizeof(InstanceData);

			auto objectCB = mCurrFrameResource->ObjectCB->Resource();
			auto instanceBuffer = mCurrFrameResource->InstanceBuffer->Resource();

			D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress();
			D3D12_GPU_VIRTUAL_ADDRESS instBufferAddress = instanceBuffer->GetGPUVirtualAddress();

			// For each render item...
			for (size_t i = 0; i < ritems.size(); ++i)
			{
				auto ri = ritems[i];

				cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
				cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
				cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

				if (ri->Instances.size() == 0)
				{
					// No instancing.
					cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress + ri->ObjCBIndex * objCBByteSize);
					cmdList->SetGraphicsRootShaderResourceView(3, instBufferAddress);
					cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
				}
				else
				{
					// Instancing.
					cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);
					cmdList->SetGraphicsRootShaderResourceView(3, instBufferAddress + ri->InstCBIndex * instElementSize);
					cmdList->DrawIndexedInstanced(ri->IndexCount, ri->Instances.size(), ri->StartIndexLocation, ri->BaseVertexLocation, 0);
				}
			}
		}

		void RenderResources::DrawSceneToShadowMap()
		{
			auto commandList = mDeviceResources->GetCommandList();

			// Bind the pass constant buffer for the shadow map pass.
			UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
			auto passCB = mCurrFrameResource->PassCB->Resource();
			D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
			commandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

			commandList->RSSetViewports(1, &mShadowMap->Viewport());
			commandList->RSSetScissorRects(1, &mShadowMap->ScissorRect());
			// Change to DEPTH_WRITE.
			commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
				D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));
			// Clear the back buffer and depth buffer.
			commandList->ClearDepthStencilView(mShadowMap->Dsv(),
				D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
			// Specify the buffers we are going to render to.
			commandList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

			if (mRitemLayer[(int)RenderLayer::Opaque].size() != 0)
			{
				commandList->SetPipelineState(mPSOs["shadow_opaque"].Get());
				DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::Opaque]);
			}
			if (mRitemLayer[(int)RenderLayer::OpaqueInst].size() != 0)
			{
				commandList->SetPipelineState(mPSOs["shadowInst_opaque"].Get());
				DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::OpaqueInst]);
			}

			// Change back to GENERIC_READ so we can read the texture in a shader.
			commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
				D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
		}

		void RenderResources::DrawNormalsAndDepth()
		{
			auto commandList = mDeviceResources->GetCommandList();

			// Bind the constant buffer for this pass.
			auto passCB = mCurrFrameResource->PassCB->Resource();
			commandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

			auto normalMap = mSsao->NormalMap();
			auto normalMapRtv = mSsao->NormalMapRtv();

			commandList->RSSetViewports(1, &mDeviceResources->GetScreenViewport());
			commandList->RSSetScissorRects(1, &mDeviceResources->GetScissorRect());
			// Change to RENDER_TARGET.
			commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
				D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

			// Clear the screen normal map and depth buffer.
			float clearValue[] = { 0.0f, 0.0f, 1.0f, 0.0f };
			commandList->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);
			commandList->ClearDepthStencilView(mDeviceResources->Dsv(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
			// Specify the buffers we are going to render to.
			commandList->OMSetRenderTargets(1, &normalMapRtv, true, &mDeviceResources->Dsv());

			if (mRitemLayer[(int)RenderLayer::Opaque].size() != 0)
			{
				commandList->SetPipelineState(mPSOs["drawNormals"].Get());
				DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::Opaque]);
			}
			if (mRitemLayer[(int)RenderLayer::OpaqueInst].size() != 0)
			{
				commandList->SetPipelineState(mPSOs["drawNormalsInst"].Get());
				DrawRenderItems(commandList, mRitemLayer[(int)RenderLayer::OpaqueInst]);
			}

			// Change back to GENERIC_READ so we can read the texture in a shader.
			commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE RenderResources::GetCpuSrv(int index) const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
				index,
				mDeviceResources->GetCbvSrvUavSize());
		}

		CD3DX12_GPU_DESCRIPTOR_HANDLE RenderResources::GetGpuSrv(int index) const
		{
			return CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
				index,
				mDeviceResources->GetCbvSrvUavSize());
		}

		std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> RenderResources::GetStaticSamplers()
		{
			// Applications usually only need a handful of samplers.  So just define them all up front
			// and keep them available as part of the root signature.  

			const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
				0, // shaderRegister
				D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

			const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
				1, // shaderRegister
				D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

			const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
				2, // shaderRegister
				D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

			const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
				3, // shaderRegister
				D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

			const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
				4, // shaderRegister
				D3D12_FILTER_ANISOTROPIC, // filter
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
				0.0f,                             // mipLODBias
				8);                               // maxAnisotropy

			const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
				5, // shaderRegister
				D3D12_FILTER_ANISOTROPIC, // filter
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
				0.0f,                              // mipLODBias
				8);                                // maxAnisotropy

			const CD3DX12_STATIC_SAMPLER_DESC shadow(
				6, // shaderRegister
				D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
				D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressU
				D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressV
				D3D12_TEXTURE_ADDRESS_MODE_BORDER,  // addressW
				0.0f,                               // mipLODBias
				16,                                 // maxAnisotropy
				D3D12_COMPARISON_FUNC_LESS_EQUAL,
				D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

			return {
				pointWrap, pointClamp,
				linearWrap, linearClamp,
				anisotropicWrap, anisotropicClamp,
				shadow
			};
		}

	}	// namespace rendering
}	// namespace handwork
