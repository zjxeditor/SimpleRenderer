// Customize this app class to specific requirements.

#include "app.h"

namespace handwork
{
	using Microsoft::WRL::ComPtr;
	using namespace DirectX;
	using namespace DirectX::PackedVector;

	App::App(HINSTANCE hInstance)
		: D3DApp(hInstance)
	{
		// Estimate the scene bounding sphere manually since we know how the scene was constructed.
		// The grid is the "widest object" with a width of 20 and depth of 30.0f, and centered at
		// the world space origin.  In general, you need to loop over every world space vertex
		// position and compute the bounding sphere.
		mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
		mSceneBounds.Radius = sqrtf(10.0f*10.0f + 15.0f*15.0f);
	}

	App::~App()
	{
		if (md3dDevice != nullptr)
			FlushCommandQueue();
	}

	bool App::Initialize()
	{
		if (!D3DApp::Initialize())
			return false;

		//Set4xMsaaState(true);

		// Reset the command list to prep for initialization commands.
		ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

		mCamera.SetPosition(0.0f, 2.0f, -15.0f);

		mShadowMap = std::make_unique<ShadowMap>(md3dDevice.Get(),
			2048, 2048);

		mSsao = std::make_unique<Ssao>(
			md3dDevice.Get(),
			mCommandList.Get(),
			mClientWidth, mClientHeight);

		BuildRootSignature();
		BuildSsaoRootSignature();
		BuildDescriptorHeaps();
		BuildShadersAndInputLayout();
		BuildShapeGeometry();
		BuildMaterials();
		BuildRenderItems();
		BuildFrameResources();
		BuildPSOs();

		mSsao->SetPSOs(mPSOs["ssao"].Get(), mPSOs["ssaoBlur"].Get());

		// Execute the initialization commands.
		ThrowIfFailed(mCommandList->Close());
		ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

		// Wait until initialization is complete.
		FlushCommandQueue();

		return true;
	}

	void App::CreateRtvAndDsvDescriptorHeaps()
	{
		// Add +1 for screen normal map, +2 for ambient maps.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
		rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 3;
		rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		rtvHeapDesc.NodeMask = 0;
		ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
			&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

		// Add +1 DSV for shadow map.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
		dsvHeapDesc.NumDescriptors = 3;
		dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		dsvHeapDesc.NodeMask = 0;
		ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
			&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
	}

	void App::OnResize()
	{
		D3DApp::OnResize();

		mCamera.SetLens(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

		if (mSsao != nullptr)
		{
			mSsao->OnResize(mClientWidth, mClientHeight);

			// Resources changed, so need to rebuild descriptors.
			mSsao->RebuildDescriptors(mDepthStencilBufferNMS.Get());
		}
	}

	void App::Update(const GameTimer& gt)
	{
		OnKeyboardInput(gt);

		// Cycle through the circular frame resource array.
		mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
		mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

		// Has the GPU finished processing the commands of the current frame resource?
		// If not, wait until the GPU has completed commands up to this fence point.
		if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
		{
			HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
			ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
			WaitForSingleObject(eventHandle, INFINITE);
			CloseHandle(eventHandle);
		}

		//
		// Animate the lights (and hence shadows).
		//

		mLightRotationAngle += 0.1f*gt.DeltaTime();

		XMMATRIX R = XMMatrixRotationY(mLightRotationAngle);
		for (int i = 0; i < 3; ++i)
		{
			XMVECTOR lightDir = XMLoadFloat3(&mBaseLightDirections[i]);
			lightDir = XMVector3TransformNormal(lightDir, R);
			XMStoreFloat3(&mRotatedLightDirections[i], lightDir);
		}

		UpdateObjectCBs(gt);
		UpdateMaterialBuffer(gt);
		UpdateShadowTransform(gt);
		UpdateMainPassCB(gt);
		UpdateShadowPassCB(gt);
		UpdateSsaoCB(gt);
	}

	void App::Draw(const GameTimer& gt)
	{
		auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

		// Reuse the memory associated with command recording.
		// We can only reset when the associated command lists have finished execution on the GPU.
		ThrowIfFailed(cmdListAlloc->Reset());

		// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
		// Reusing the command list reuses memory.
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

		ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
		mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

		//
		// Shadow map pass.
		//

		// Bind all the materials used in this scene.  For structured buffers, we can bypass the heap and 
		// set as a root descriptor.
		auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
		mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

		// Bind null SRV for shadow map pass.
		mCommandList->SetGraphicsRootDescriptorTable(3, mNullSrv);

		DrawSceneToShadowMap();

		//
		// Normal/depth pass.
		//

		DrawNormalsAndDepth();

		//
		// Compute SSAO.
		// 

		mCommandList->SetGraphicsRootSignature(mSsaoRootSignature.Get());
		mSsao->ComputeSsao(mCommandList.Get(), mCurrFrameResource, 3);

		//
		// Main rendering pass.
		//

		mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

		// Rebind state whenever graphics root signature changes.

		// Bind all the materials used in this scene. For structured buffers, we can bypass the heap and 
		// set as a root descriptor.
		matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
		mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());


		mCommandList->RSSetViewports(1, &mScreenViewport);
		mCommandList->RSSetScissorRects(1, &mScissorRect);

		// Indicate a state transition on the resource usage.
		if (!m4xMsaaState || m4xMsaaQuality <= 0)
		{
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
		}
		else
		{
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
				D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
			mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
		}
		

		// Clear the back buffer.
		mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);

		// WE ALREADY WROTE THE DEPTH INFO TO THE DEPTH BUFFER IN DrawNormalsAndDepth,
		// SO DO NOT CLEAR DEPTH.

		// Specify the buffers we are going to render to.
		mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

		auto passCB = mCurrFrameResource->PassCB->Resource();
		mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

		CD3DX12_GPU_DESCRIPTOR_HANDLE shadowMapDescriptor(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		shadowMapDescriptor.Offset(mShadowMapHeapIndex, mCbvSrvUavDescriptorSize);
		mCommandList->SetGraphicsRootDescriptorTable(3, shadowMapDescriptor);

		mCommandList->SetPipelineState(mPSOs["opaque"].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

		mCommandList->SetPipelineState(mPSOs["debug"].Get());
		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Debug]);

		// Indicate a state transition on the resource usage.
		if (!m4xMsaaState || m4xMsaaQuality <= 0)
		{
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
				D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
		}
		else
		{
			D3D12_RESOURCE_BARRIER barriers[2] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mSwapChainBuffer[mCurrBackBuffer].Get(),
				D3D12_RESOURCE_STATE_PRESENT,
				D3D12_RESOURCE_STATE_RESOLVE_DEST)
			};
			mCommandList->ResourceBarrier(2, barriers);

			mCommandList->ResolveSubresource(mSwapChainBuffer[mCurrBackBuffer].Get(), 0,
				CurrentBackBuffer(), 0, mBackBufferFormat);

			D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				mSwapChainBuffer[mCurrBackBuffer].Get(),
				D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PRESENT);
			mCommandList->ResourceBarrier(1, &barrier);
		}

		// Done recording commands.
		ThrowIfFailed(mCommandList->Close());

		// Add the command list to the queue for execution.
		ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
		mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

		// Swap the back and front buffers
		ThrowIfFailed(mSwapChain->Present(0, 0));
		mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

		// Advance the fence value to mark commands up to this fence point.
		mCurrFrameResource->Fence = ++mCurrentFence;

		// Add an instruction to the command queue to set a new fence point. 
		// Because we are on the GPU timeline, the new fence point won't be 
		// set until the GPU finishes processing all the commands prior to this Signal().
		mCommandQueue->Signal(mFence.Get(), mCurrentFence);
	}

	void App::OnMouseDown(WPARAM btnState, int x, int y)
	{
		mLastMousePos.x = x;
		mLastMousePos.y = y;

		SetCapture(mhMainWnd);
	}

	void App::OnMouseUp(WPARAM btnState, int x, int y)
	{
		ReleaseCapture();
	}

	void App::OnMouseMove(WPARAM btnState, int x, int y)
	{
		if ((btnState & MK_LBUTTON) != 0)
		{
			// Make each pixel correspond to a quarter of a degree.
			float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
			float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

			mCamera.Pitch(dy);
			mCamera.RotateY(dx);
		}

		mLastMousePos.x = x;
		mLastMousePos.y = y;
	}

	void App::OnKeyboardInput(const GameTimer& gt)
	{
		const float dt = gt.DeltaTime();

		if (GetAsyncKeyState('W') & 0x8000)
			mCamera.Walk(10.0f*dt);

		if (GetAsyncKeyState('S') & 0x8000)
			mCamera.Walk(-10.0f*dt);

		if (GetAsyncKeyState('A') & 0x8000)
			mCamera.Strafe(-10.0f*dt);

		if (GetAsyncKeyState('D') & 0x8000)
			mCamera.Strafe(10.0f*dt);

		mCamera.UpdateViewMatrix();
	}

	void App::UpdateObjectCBs(const GameTimer& gt)
	{
		auto currObjectCB = mCurrFrameResource->ObjectCB.get();
		for (auto& e : mAllRitems)
		{
			// Only update the cbuffer data if the constants have changed.  
			// This needs to be tracked per frame resource.
			if (e->NumFramesDirty > 0)
			{
				XMMATRIX world = XMLoadFloat4x4(&e->World);
				ObjectConstants objConstants;
				XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
				objConstants.MaterialIndex = e->Mat->MatCBIndex;

				currObjectCB->CopyData(e->ObjCBIndex, objConstants);

				// Next FrameResource need to be updated too.
				e->NumFramesDirty--;
			}
		}
	}

	void App::UpdateMaterialBuffer(const GameTimer& gt)
	{
		auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
		for (auto& e : mMaterials)
		{
			// Only update the cbuffer data if the constants have changed.  If the cbuffer
			// data changes, it needs to be updated for each FrameResource.
			Material* mat = e.second.get();
			if (mat->NumFramesDirty > 0)
			{
				XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

				MaterialData matData;
				matData.DiffuseAlbedo = mat->DiffuseAlbedo;
				matData.FresnelR0 = mat->FresnelR0;
				matData.Roughness = mat->Roughness;

				currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

				// Next FrameResource need to be updated too.
				mat->NumFramesDirty--;
			}
		}
	}

	void App::UpdateShadowTransform(const GameTimer& gt)
	{
		// Only the first "main" light casts a shadow.
		XMVECTOR lightDir = XMLoadFloat3(&mRotatedLightDirections[0]);
		XMVECTOR lightPos = -2.0f*mSceneBounds.Radius*lightDir;
		XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
		XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

		XMStoreFloat3(&mLightPosW, lightPos);

		// Transform bounding sphere to light space.
		XMFLOAT3 sphereCenterLS;
		XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

		// Ortho frustum in light space encloses scene.
		float l = sphereCenterLS.x - mSceneBounds.Radius;
		float b = sphereCenterLS.y - mSceneBounds.Radius;
		float n = sphereCenterLS.z - mSceneBounds.Radius;
		float r = sphereCenterLS.x + mSceneBounds.Radius;
		float t = sphereCenterLS.y + mSceneBounds.Radius;
		float f = sphereCenterLS.z + mSceneBounds.Radius;

		mLightNearZ = n;
		mLightFarZ = f;
		XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

		// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
		XMMATRIX T(
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f);

		XMMATRIX S = lightView * lightProj*T;
		XMStoreFloat4x4(&mLightView, lightView);
		XMStoreFloat4x4(&mLightProj, lightProj);
		XMStoreFloat4x4(&mShadowTransform, S);
	}

	void App::UpdateMainPassCB(const GameTimer& gt)
	{
		XMMATRIX view = mCamera.GetView();
		XMMATRIX proj = mCamera.GetProj();

		XMMATRIX viewProj = XMMatrixMultiply(view, proj);
		XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
		XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
		XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

		// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
		XMMATRIX T(
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f);

		XMMATRIX viewProjTex = XMMatrixMultiply(viewProj, T);
		XMMATRIX shadowTransform = XMLoadFloat4x4(&mShadowTransform);

		XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
		XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
		XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
		XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
		XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
		XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
		XMStoreFloat4x4(&mMainPassCB.ViewProjTex, XMMatrixTranspose(viewProjTex));
		XMStoreFloat4x4(&mMainPassCB.ShadowTransform, XMMatrixTranspose(shadowTransform));
		mMainPassCB.EyePosW = mCamera.GetPosition3f();
		mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
		mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
		mMainPassCB.NearZ = 1.0f;
		mMainPassCB.FarZ = 1000.0f;
		mMainPassCB.TotalTime = gt.TotalTime();
		mMainPassCB.DeltaTime = gt.DeltaTime();
		mMainPassCB.AmbientLight = { 0.4f, 0.4f, 0.6f, 1.0f };
		mMainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
		mMainPassCB.Lights[0].Strength = { 0.4f, 0.4f, 0.5f };
		mMainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
		mMainPassCB.Lights[1].Strength = { 0.1f, 0.1f, 0.1f };
		mMainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
		mMainPassCB.Lights[2].Strength = { 0.0f, 0.0f, 0.0f };

		auto currPassCB = mCurrFrameResource->PassCB.get();
		currPassCB->CopyData(0, mMainPassCB);
	}

	void App::UpdateShadowPassCB(const GameTimer& gt)
	{
		XMMATRIX view = XMLoadFloat4x4(&mLightView);
		XMMATRIX proj = XMLoadFloat4x4(&mLightProj);

		XMMATRIX viewProj = XMMatrixMultiply(view, proj);
		XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
		XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
		XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

		UINT w = mShadowMap->Width();
		UINT h = mShadowMap->Height();

		XMStoreFloat4x4(&mShadowPassCB.View, XMMatrixTranspose(view));
		XMStoreFloat4x4(&mShadowPassCB.InvView, XMMatrixTranspose(invView));
		XMStoreFloat4x4(&mShadowPassCB.Proj, XMMatrixTranspose(proj));
		XMStoreFloat4x4(&mShadowPassCB.InvProj, XMMatrixTranspose(invProj));
		XMStoreFloat4x4(&mShadowPassCB.ViewProj, XMMatrixTranspose(viewProj));
		XMStoreFloat4x4(&mShadowPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
		mShadowPassCB.EyePosW = mLightPosW;
		mShadowPassCB.RenderTargetSize = XMFLOAT2((float)w, (float)h);
		mShadowPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / w, 1.0f / h);
		mShadowPassCB.NearZ = mLightNearZ;
		mShadowPassCB.FarZ = mLightFarZ;

		auto currPassCB = mCurrFrameResource->PassCB.get();
		currPassCB->CopyData(1, mShadowPassCB);
	}

	void App::UpdateSsaoCB(const GameTimer& gt)
	{
		SsaoConstants ssaoCB;

		XMMATRIX P = mCamera.GetProj();

		// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
		XMMATRIX T(
			0.5f, 0.0f, 0.0f, 0.0f,
			0.0f, -0.5f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.5f, 0.5f, 0.0f, 1.0f);

		ssaoCB.Proj = mMainPassCB.Proj;
		ssaoCB.InvProj = mMainPassCB.InvProj;
		XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P*T));

		mSsao->GetOffsetVectors(ssaoCB.OffsetVectors);

		auto blurWeights = mSsao->CalcGaussWeights(2.5f);
		ssaoCB.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
		ssaoCB.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
		ssaoCB.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);

		ssaoCB.InvRenderTargetSize = XMFLOAT2(1.0f / mSsao->SsaoMapWidth(), 1.0f / mSsao->SsaoMapHeight());

		// Coordinates given in view space.
		ssaoCB.OcclusionRadius = 0.5f;
		ssaoCB.OcclusionFadeStart = 0.2f;
		ssaoCB.OcclusionFadeEnd = 1.0f;
		ssaoCB.SurfaceEpsilon = 0.05f;

		auto currSsaoCB = mCurrFrameResource->SsaoCB.get();
		currSsaoCB->CopyData(0, ssaoCB);
	}

	void App::BuildRootSignature()
	{
		CD3DX12_DESCRIPTOR_RANGE texTable0;
		texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0);

		// Root parameter can be a table, root descriptor or root constants.
		CD3DX12_ROOT_PARAMETER slotRootParameter[4];

		// Perfomance TIP: Order from most frequent to least frequent.
		slotRootParameter[0].InitAsConstantBufferView(0);
		slotRootParameter[1].InitAsConstantBufferView(1);
		slotRootParameter[2].InitAsShaderResourceView(0);
		slotRootParameter[3].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);

		auto staticSamplers = GetStaticSamplers();

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

		ThrowIfFailed(md3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mRootSignature.GetAddressOf())));
	}

	void App::BuildSsaoRootSignature()
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

		ThrowIfFailed(md3dDevice->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(mSsaoRootSignature.GetAddressOf())));
	}

	void App::BuildDescriptorHeaps()
	{
		//
		// Create the SRV heap.
		//
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = 8;
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

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
		md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);
		nullSrv.Offset(1, mCbvSrvUavDescriptorSize);
		md3dDevice->CreateShaderResourceView(nullptr, &srvDesc, nullSrv);

		mShadowMap->BuildDescriptors(
			GetCpuSrv(mShadowMapHeapIndex),
			GetGpuSrv(mShadowMapHeapIndex),
			GetDsv(2));

		mSsao->BuildDescriptors(
			mDepthStencilBufferNMS.Get(),
			GetCpuSrv(mSsaoHeapIndexStart),
			GetGpuSrv(mSsaoHeapIndexStart),
			GetRtv(SwapChainBufferCount),
			mCbvSrvUavDescriptorSize,
			mRtvDescriptorSize);
	}

	void App::BuildShadersAndInputLayout()
	{
		mShaders["standardVS"] = d3dUtil::CompileShader(L"shaders\\default.hlsl", nullptr, "VS", "vs_5_1");
		mShaders["opaquePS"] = d3dUtil::CompileShader(L"shaders\\default.hlsl", nullptr, "PS", "ps_5_1");

		mShaders["shadowVS"] = d3dUtil::CompileShader(L"shaders\\shadows.hlsl", nullptr, "VS", "vs_5_1");
		mShaders["shadowOpaquePS"] = d3dUtil::CompileShader(L"shaders\\shadows.hlsl", nullptr, "PS", "ps_5_1");

		mShaders["debugVS"] = d3dUtil::CompileShader(L"shaders\\debug.hlsl", nullptr, "VS", "vs_5_1");
		mShaders["debugPS"] = d3dUtil::CompileShader(L"shaders\\debug.hlsl", nullptr, "PS", "ps_5_1");

		mShaders["drawNormalsVS"] = d3dUtil::CompileShader(L"shaders\\drawnormals.hlsl", nullptr, "VS", "vs_5_1");
		mShaders["drawNormalsPS"] = d3dUtil::CompileShader(L"shaders\\drawnormals.hlsl", nullptr, "PS", "ps_5_1");

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

	void App::BuildShapeGeometry()
	{
		GeometryGenerator geoGen;
		GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
		GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
		GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
		GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
		GeometryGenerator::MeshData quad = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);

		//
		// We are concatenating all the geometry into one big vertex/index buffer.  So
		// define the regions in the buffer each submesh covers.
		//

		// Cache the vertex offsets to each object in the concatenated vertex buffer.
		UINT boxVertexOffset = 0;
		UINT gridVertexOffset = (UINT)box.Vertices.size();
		UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
		UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
		UINT quadVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

		// Cache the starting index for each object in the concatenated index buffer.
		UINT boxIndexOffset = 0;
		UINT gridIndexOffset = (UINT)box.Indices32.size();
		UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
		UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
		UINT quadIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();

		SubmeshGeometry boxSubmesh;
		boxSubmesh.IndexCount = (UINT)box.Indices32.size();
		boxSubmesh.StartIndexLocation = boxIndexOffset;
		boxSubmesh.BaseVertexLocation = boxVertexOffset;

		SubmeshGeometry gridSubmesh;
		gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
		gridSubmesh.StartIndexLocation = gridIndexOffset;
		gridSubmesh.BaseVertexLocation = gridVertexOffset;

		SubmeshGeometry sphereSubmesh;
		sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
		sphereSubmesh.StartIndexLocation = sphereIndexOffset;
		sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

		SubmeshGeometry cylinderSubmesh;
		cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
		cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
		cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

		SubmeshGeometry quadSubmesh;
		quadSubmesh.IndexCount = (UINT)quad.Indices32.size();
		quadSubmesh.StartIndexLocation = quadIndexOffset;
		quadSubmesh.BaseVertexLocation = quadVertexOffset;

		//
		// Extract the vertex elements we are interested in and pack the
		// vertices of all the meshes into one vertex buffer.
		//

		auto totalVertexCount =
			box.Vertices.size() +
			grid.Vertices.size() +
			sphere.Vertices.size() +
			cylinder.Vertices.size() +
			quad.Vertices.size();

		std::vector<Vertex> vertices(totalVertexCount);

		UINT k = 0;
		for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
		{
			vertices[k].Pos = box.Vertices[i].Position;
			vertices[k].Normal = box.Vertices[i].Normal;
			vertices[k].TangentU = box.Vertices[i].TangentU;
		}

		for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
		{
			vertices[k].Pos = grid.Vertices[i].Position;
			vertices[k].Normal = grid.Vertices[i].Normal;
			vertices[k].TangentU = grid.Vertices[i].TangentU;
		}

		for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
		{
			vertices[k].Pos = sphere.Vertices[i].Position;
			vertices[k].Normal = sphere.Vertices[i].Normal;
			vertices[k].TangentU = sphere.Vertices[i].TangentU;
		}

		for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
		{
			vertices[k].Pos = cylinder.Vertices[i].Position;
			vertices[k].Normal = cylinder.Vertices[i].Normal;
			vertices[k].TangentU = cylinder.Vertices[i].TangentU;
		}

		for (int i = 0; i < quad.Vertices.size(); ++i, ++k)
		{
			vertices[k].Pos = quad.Vertices[i].Position;
			vertices[k].Normal = quad.Vertices[i].Normal;
			vertices[k].TangentU = quad.Vertices[i].TangentU;
		}

		std::vector<std::uint16_t> indices;
		indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
		indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
		indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
		indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
		indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));

		const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
		const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "shapeGeo";

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
			mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

		geo->VertexByteStride = sizeof(Vertex);
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R16_UINT;
		geo->IndexBufferByteSize = ibByteSize;

		geo->DrawArgs["box"] = boxSubmesh;
		geo->DrawArgs["grid"] = gridSubmesh;
		geo->DrawArgs["sphere"] = sphereSubmesh;
		geo->DrawArgs["cylinder"] = cylinderSubmesh;
		geo->DrawArgs["quad"] = quadSubmesh;

		mGeometries[geo->Name] = std::move(geo);
	}

	void App::BuildPSOs()
	{
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
		basePsoDesc.RTVFormats[0] = mBackBufferFormat;
		basePsoDesc.SampleDesc.Count = 1;
		basePsoDesc.SampleDesc.Quality = 0;
		basePsoDesc.DSVFormat = mDepthStencilFormat;

		//
		// PSO for opaque objects.
		//

		D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = basePsoDesc;
		if (m4xMsaaState && m4xMsaaQuality > 0)
		{
			opaquePsoDesc.RasterizerState.MultisampleEnable = true;
			opaquePsoDesc.SampleDesc.Count = 4;
			opaquePsoDesc.SampleDesc.Quality = m4xMsaaQuality - 1;
		}
		else
		{
			opaquePsoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
			opaquePsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		}
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

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
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&smapPsoDesc, IID_PPV_ARGS(&mPSOs["shadow_opaque"])));

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
		if (m4xMsaaState && m4xMsaaQuality > 0)
		{
			debugPsoDesc.RasterizerState.MultisampleEnable = true;
			debugPsoDesc.SampleDesc.Count = 4;
			debugPsoDesc.SampleDesc.Quality = m4xMsaaQuality - 1;
		}
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));

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
		drawNormalsPsoDesc.DSVFormat = mDepthStencilFormat;
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&drawNormalsPsoDesc, IID_PPV_ARGS(&mPSOs["drawNormals"])));

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
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ssaoPsoDesc, IID_PPV_ARGS(&mPSOs["ssao"])));

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
		ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&ssaoBlurPsoDesc, IID_PPV_ARGS(&mPSOs["ssaoBlur"])));
	}

	void App::BuildFrameResources()
	{
		for (int i = 0; i < gNumFrameResources; ++i)
		{
			mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
				2, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
		}
	}

	void App::BuildMaterials()
	{
		auto bricks0 = std::make_unique<Material>();
		bricks0->Name = "bricks0";
		bricks0->MatCBIndex = 0;
		bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		bricks0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
		bricks0->Roughness = 0.3f;

		auto tile0 = std::make_unique<Material>();
		tile0->Name = "tile0";
		tile0->MatCBIndex = 1;
		tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
		tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
		tile0->Roughness = 0.1f;

		auto mirror0 = std::make_unique<Material>();
		mirror0->Name = "mirror0";
		mirror0->MatCBIndex = 2;
		mirror0->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		mirror0->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
		mirror0->Roughness = 0.1f;

		auto skullMat = std::make_unique<Material>();
		skullMat->Name = "skullMat";
		skullMat->MatCBIndex = 3;
		skullMat->DiffuseAlbedo = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
		skullMat->FresnelR0 = XMFLOAT3(0.6f, 0.6f, 0.6f);
		skullMat->Roughness = 0.2f;

		mMaterials["bricks0"] = std::move(bricks0);
		mMaterials["tile0"] = std::move(tile0);
		mMaterials["mirror0"] = std::move(mirror0);
		mMaterials["skullMat"] = std::move(skullMat);
	}

	void App::BuildRenderItems()
	{
		auto quadRitem = std::make_unique<RenderItem>();
		quadRitem->World = MathHelper::Identity4x4();
		quadRitem->ObjCBIndex = 0;
		quadRitem->Mat = mMaterials["bricks0"].get();
		quadRitem->Geo = mGeometries["shapeGeo"].get();
		quadRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		quadRitem->IndexCount = quadRitem->Geo->DrawArgs["quad"].IndexCount;
		quadRitem->StartIndexLocation = quadRitem->Geo->DrawArgs["quad"].StartIndexLocation;
		quadRitem->BaseVertexLocation = quadRitem->Geo->DrawArgs["quad"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Debug].push_back(quadRitem.get());
		mAllRitems.push_back(std::move(quadRitem));

		auto boxRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 1.0f, 2.0f)*XMMatrixTranslation(0.0f, 0.5f, 0.0f));
		boxRitem->ObjCBIndex = 1;
		boxRitem->Mat = mMaterials["bricks0"].get();
		boxRitem->Geo = mGeometries["shapeGeo"].get();
		boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
		boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
		boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
		mAllRitems.push_back(std::move(boxRitem));

		auto gridRitem = std::make_unique<RenderItem>();
		gridRitem->World = MathHelper::Identity4x4();
		gridRitem->ObjCBIndex = 2;
		gridRitem->Mat = mMaterials["tile0"].get();
		gridRitem->Geo = mGeometries["shapeGeo"].get();
		gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
		gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
		gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

		mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());
		mAllRitems.push_back(std::move(gridRitem));

		XMMATRIX brickTexTransform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
		UINT objCBIndex = 3;
		for (int i = 0; i < 5; ++i)
		{
			auto leftCylRitem = std::make_unique<RenderItem>();
			auto rightCylRitem = std::make_unique<RenderItem>();
			auto leftSphereRitem = std::make_unique<RenderItem>();
			auto rightSphereRitem = std::make_unique<RenderItem>();

			XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
			XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

			XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
			XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

			XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
			leftCylRitem->ObjCBIndex = objCBIndex++;
			leftCylRitem->Mat = mMaterials["bricks0"].get();
			leftCylRitem->Geo = mGeometries["shapeGeo"].get();
			leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
			leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
			leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

			XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
			rightCylRitem->ObjCBIndex = objCBIndex++;
			rightCylRitem->Mat = mMaterials["bricks0"].get();
			rightCylRitem->Geo = mGeometries["shapeGeo"].get();
			rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
			rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
			rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

			XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
			leftSphereRitem->ObjCBIndex = objCBIndex++;
			leftSphereRitem->Mat = mMaterials["mirror0"].get();
			leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
			leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
			leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
			leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

			XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
			rightSphereRitem->ObjCBIndex = objCBIndex++;
			rightSphereRitem->Mat = mMaterials["mirror0"].get();
			rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
			rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
			rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
			rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

			mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCylRitem.get());
			mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCylRitem.get());
			mRitemLayer[(int)RenderLayer::Opaque].push_back(leftSphereRitem.get());
			mRitemLayer[(int)RenderLayer::Opaque].push_back(rightSphereRitem.get());

			mAllRitems.push_back(std::move(leftCylRitem));
			mAllRitems.push_back(std::move(rightCylRitem));
			mAllRitems.push_back(std::move(leftSphereRitem));
			mAllRitems.push_back(std::move(rightSphereRitem));
		}
	}

	void App::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
	{
		UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

		auto objectCB = mCurrFrameResource->ObjectCB->Resource();

		// For each render item...
		for (size_t i = 0; i < ritems.size(); ++i)
		{
			auto ri = ritems[i];

			cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
			cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
			cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

			D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;

			cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

			cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
		}
	}

	void App::DrawSceneToShadowMap()
	{
		mCommandList->RSSetViewports(1, &mShadowMap->Viewport());
		mCommandList->RSSetScissorRects(1, &mShadowMap->ScissorRect());

		// Change to DEPTH_WRITE.
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_DEPTH_WRITE));

		// Clear the back buffer and depth buffer.
		mCommandList->ClearDepthStencilView(mShadowMap->Dsv(),
			D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		// Specify the buffers we are going to render to.
		mCommandList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

		// Bind the pass constant buffer for the shadow map pass.
		UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
		auto passCB = mCurrFrameResource->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
		mCommandList->SetGraphicsRootConstantBufferView(1, passCBAddress);

		mCommandList->SetPipelineState(mPSOs["shadow_opaque"].Get());

		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

		// Change back to GENERIC_READ so we can read the texture in a shader.
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_GENERIC_READ));
	}

	void App::DrawNormalsAndDepth()
	{
		mCommandList->RSSetViewports(1, &mScreenViewport);
		mCommandList->RSSetScissorRects(1, &mScissorRect);

		auto normalMap = mSsao->NormalMap();
		auto normalMapRtv = mSsao->NormalMapRtv();

		// Change to RENDER_TARGET.
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
			D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

		// Clear the screen normal map and depth buffer.
		float clearValue[] = { 0.0f, 0.0f, 1.0f, 0.0f };
		mCommandList->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);
		mCommandList->ClearDepthStencilView(DepthStencilViewNMS(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		// Specify the buffers we are going to render to.
		mCommandList->OMSetRenderTargets(1, &normalMapRtv, true, &DepthStencilViewNMS());

		// Bind the constant buffer for this pass.
		auto passCB = mCurrFrameResource->PassCB->Resource();
		mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

		mCommandList->SetPipelineState(mPSOs["drawNormals"].Get());

		DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

		// Change back to GENERIC_READ so we can read the texture in a shader.
		mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
			D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE App::GetCpuSrv(int index) const
	{
		auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		srv.Offset(index, mCbvSrvUavDescriptorSize);
		return srv;
	}

	CD3DX12_GPU_DESCRIPTOR_HANDLE App::GetGpuSrv(int index) const
	{
		auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		srv.Offset(index, mCbvSrvUavDescriptorSize);
		return srv;
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE App::GetDsv(int index) const
	{
		auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
		dsv.Offset(index, mDsvDescriptorSize);
		return dsv;
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE App::GetRtv(int index) const
	{
		auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
		rtv.Offset(index, mRtvDescriptorSize);
		return rtv;
	}

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> App::GetStaticSamplers()
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

}	// namespace handwork