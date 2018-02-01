// Manage device related resources for DirectX.

#include "deviceresources.h"
#include <wincodec.h>


namespace DirectX
{
	IWICImagingFactory2* _GetWIC()
	{
		static INIT_ONCE s_initOnce = INIT_ONCE_STATIC_INIT;

		IWICImagingFactory2* factory = nullptr;
		(void)InitOnceExecuteOnce(&s_initOnce,
			[](PINIT_ONCE, PVOID, PVOID *factory) -> BOOL
		{
			return SUCCEEDED(CoCreateInstance(
				CLSID_WICImagingFactory2,
				nullptr,
				CLSCTX_INPROC_SERVER,
				__uuidof(IWICImagingFactory2),
				factory)) ? TRUE : FALSE;
		}, nullptr, reinterpret_cast<LPVOID*>(&factory));

		return factory;
	}

	class auto_delete_file_wic
	{
	public:
		auto_delete_file_wic(Microsoft::WRL::ComPtr<IWICStream>& hFile, LPCWSTR szFile) : m_filename(szFile), m_handle(hFile) {}

		auto_delete_file_wic(const auto_delete_file_wic&) = delete;
		auto_delete_file_wic& operator=(const auto_delete_file_wic&) = delete;

		~auto_delete_file_wic()
		{
			if (m_filename)
			{
				m_handle.Reset();
				DeleteFileW(m_filename);
			}
		}

		void clear() { m_filename = 0; }

	private:
		LPCWSTR m_filename;
		Microsoft::WRL::ComPtr<IWICStream>& m_handle;
	};

} // namespace DirectX


namespace handwork
{
	namespace rendering
	{
		using Microsoft::WRL::ComPtr;

		DeviceResources::DeviceResources(MSAATYPE msaaType, UINT maxWidth, UINT maxHeight) :
			mDeviceNotify(nullptr), mMsaaQuality(0), mMaxWidth(maxWidth), mMaxHeight(maxHeight)
		{
			mMsaaCount = (UINT)pow(2, (int)msaaType);

			CreateDeviceIndependentResources();
			CreateDeviceResources();
		}

		DeviceResources::~DeviceResources()
		{
			if (md3dDevice != nullptr)
				FlushCommandQueue();
		}

		// Configures resources that don't depend on the Direct3D device.
		void DeviceResources::CreateDeviceIndependentResources()
		{
		}

		// Configures the Direct3D device, and stores handles related resources.
		void DeviceResources::CreateDeviceResources()
		{
#if defined(DEBUG) || defined(_DEBUG) 
			// Enable the D3D12 debug layer.
			{
				ComPtr<ID3D12Debug> debugController;
				ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
				debugController->EnableDebugLayer();
			}
#endif

			ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&mdxgiFactory)));

			// Try to create hardware device.
			// Be sure to pick up the first adapter that supports D3D12.
			GetHardwareAdapter(mdxgiFactory.Get(), mHardwareAdapter.GetAddressOf());
			HRESULT hardwareResult = D3D12CreateDevice(
				mHardwareAdapter.Get(),	// default adapter
				mMinFeatureLevel,
				IID_PPV_ARGS(&md3dDevice));

			// Fallback to WARP device.
			if (FAILED(hardwareResult))
			{
				ComPtr<IDXGIAdapter> pWarpAdapter;
				ThrowIfFailed(mdxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

				ThrowIfFailed(D3D12CreateDevice(
					pWarpAdapter.Get(),
					mMinFeatureLevel,
					IID_PPV_ARGS(&md3dDevice)));
			}

			ThrowIfFailed(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));

			mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
			mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			// Check MSAA quality support for the back buffer format.
			if (mMsaaCount > 1)
			{
				D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
				msQualityLevels.Format = mBackBufferFormat;
				msQualityLevels.SampleCount = mMsaaCount;
				msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
				msQualityLevels.NumQualityLevels = 0;
				ThrowIfFailed(md3dDevice->CheckFeatureSupport(
					D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
					&msQualityLevels,
					sizeof(msQualityLevels)));

				mMsaaQuality = msQualityLevels.NumQualityLevels;
				assert(mMsaaQuality > 0 && "Unexpected MSAA quality level.");
			}

#ifdef _DEBUG
			LogAdapters();
#endif

			// Create command objects.
			D3D12_COMMAND_QUEUE_DESC queueDesc = {};
			queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			ThrowIfFailed(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

			ThrowIfFailed(md3dDevice->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf())));

			ThrowIfFailed(md3dDevice->CreateCommandList(
				0,
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				mDirectCmdListAlloc.Get(), // Associated command allocator
				nullptr,                   // Initial PipelineStateObject
				IID_PPV_ARGS(mCommandList.GetAddressOf())));

			// Start off in a closed state.  This is because the first time we refer 
			// to the command list we will Reset it, and it needs to be closed before
			// calling Reset.
			mCommandList->Close();

			// Create rtv and dsv heap
			// 2 for swap chain,  1 for screen normal map, 2 for ambient maps.
			D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
			rtvHeapDesc.NumDescriptors = SwapChainBufferCount + 3;
			rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			rtvHeapDesc.NodeMask = 0;
			ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
				&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

			// 1 for common dsv, 1 for msaa dsv, 1 for shadow map dsv.
			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
			dsvHeapDesc.NumDescriptors = 3;
			dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			dsvHeapDesc.NodeMask = 0;
			ThrowIfFailed(md3dDevice->CreateDescriptorHeap(
				&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));
		}

		// These resources need to be recreated every time the window size is changed.
		void DeviceResources::CreateWindowSizeDependentResources()
		{
			assert(md3dDevice);
			assert(mDirectCmdListAlloc);

			// Flush before changing any resources.
			FlushCommandQueue();
			ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

			// Clear the previous window size specific context.
			// Release the previous resources we will be recreating.
			for (int i = 0; i < SwapChainBufferCount; ++i)
			{
				mSwapChainBuffer[i].Reset();
				mOffScreenBuffer[i].Reset();
			}
			mDepthStencilBuffer.Reset();
			mDepthStencilBufferMS.Reset();

			UpdateRenderTargetSize();
#ifdef _DEBUG
			std::wostringstream wos;
			wos << L"Render target size: " << mRenderTargetSize.x << L"x" << mRenderTargetSize.y << L"\n";
			OutputDebugString(wos.str().c_str());
#endif

			// Create or resize the swap chain.
			if (mSwapChain)
			{
				// Resize the swap chain.
				HRESULT hr = mSwapChain->ResizeBuffers(
					SwapChainBufferCount,
					mRenderTargetSize.x, mRenderTargetSize.y,
					mBackBufferFormat,
					DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
				if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
				{
					// If the device was removed for any reason, a new device and swap chain will need to be created.
					HandleDeviceLost();

					// Everything is set up now. Do not continue execution of this method. HandleDeviceLost will reenter this method 
					// and correctly set up the new device.
					return;
				}
				else
				{
					ThrowIfFailed(hr);
				}
			}
			else
			{
				DXGI_SWAP_CHAIN_DESC sd;
				sd.BufferDesc.Width = mRenderTargetSize.x;
				sd.BufferDesc.Height = mRenderTargetSize.y;
				sd.BufferDesc.RefreshRate.Numerator = 60;
				sd.BufferDesc.RefreshRate.Denominator = 1;
				sd.BufferDesc.Format = mBackBufferFormat;
				sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
				sd.BufferDesc.Scaling = DXGI_MODE_SCALING_STRETCHED;
				sd.SampleDesc.Count = 1;	// Don't use multi-sampling.
				sd.SampleDesc.Quality = 0;
				sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				sd.BufferCount = SwapChainBufferCount;
				sd.OutputWindow = mhMainWnd;
				sd.Windowed = true;
				sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

				// Note: Swap chain uses queue to perform flush.
				ThrowIfFailed(mdxgiFactory->CreateSwapChain(
					mCommandQueue.Get(),
					&sd,
					mSwapChain.GetAddressOf()));
			}

			mCurrBackBuffer = 0;

			if (mMsaaQuality <= 0)
			{
				OutputDebugString(L"MSAA is off.\n");

				// Get swap chain back buffers and create rtvs.
				CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
				for (UINT i = 0; i < SwapChainBufferCount; ++i)
				{
					ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
					md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
					rtvHeapHandle.Offset(1, mRtvDescriptorSize);
				}

				// Crate depth stencil buffer and dsv.
				D3D12_RESOURCE_DESC depthStencilDesc;
				depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				depthStencilDesc.Alignment = 0;
				depthStencilDesc.Width = mRenderTargetSize.x;
				depthStencilDesc.Height = mRenderTargetSize.y;
				depthStencilDesc.DepthOrArraySize = 1;
				depthStencilDesc.MipLevels = 1;
				// Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from 
				// the depth buffer.  Therefore, because we need to create two views to the same resource:
				//   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
				//   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
				// we need to create the depth buffer resource with a typeless format.  
				depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
				depthStencilDesc.SampleDesc.Count = 1;
				depthStencilDesc.SampleDesc.Quality = 0;
				depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

				D3D12_CLEAR_VALUE optClear;
				optClear.Format = mDepthStencilFormat;
				optClear.DepthStencil.Depth = 1.0f;
				optClear.DepthStencil.Stencil = 0;
				ThrowIfFailed(md3dDevice->CreateCommittedResource(
					&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
					D3D12_HEAP_FLAG_NONE,
					&depthStencilDesc,
					D3D12_RESOURCE_STATE_COMMON,
					&optClear,
					IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

				// No depth stencil for msaa.
				mDepthStencilBufferMS.Reset();

				// Create descriptor to mip level 0 of entire resource using the format of the resource.
				D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
				dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				dsvDesc.Format = mDepthStencilFormat;
				dsvDesc.Texture2D.MipSlice = 0;
				md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, Dsv());

				// Transition the resource from its initial state to be used as a depth buffer.
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
					D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));
			}
			else
			{
				OutputDebugString(L"MSAA is on.\n");

				// Create off screen buffers
				D3D12_RESOURCE_DESC offScreenDesc;
				offScreenDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				offScreenDesc.Alignment = 0;
				offScreenDesc.Width = mRenderTargetSize.x;
				offScreenDesc.Height = mRenderTargetSize.y;
				offScreenDesc.DepthOrArraySize = 1;
				offScreenDesc.MipLevels = 1;
				offScreenDesc.Format = mBackBufferFormat;
				offScreenDesc.SampleDesc.Count = mMsaaCount;
				offScreenDesc.SampleDesc.Quality = mMsaaQuality - 1;
				offScreenDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				offScreenDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

				D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
				rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
				rtvDesc.Format = mBackBufferFormat;
				rtvDesc.Texture2D.MipSlice = 0;
				rtvDesc.Texture2D.PlaneSlice = 0;

				D3D12_CLEAR_VALUE ofsClear;
				ofsClear.Format = mBackBufferFormat;
				memcpy(ofsClear.Color, DirectX::Colors::Black, sizeof(float) * 4);

				CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
				for (UINT i = 0; i < SwapChainBufferCount; ++i)
				{
					ThrowIfFailed(md3dDevice->CreateCommittedResource(
						&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
						D3D12_HEAP_FLAG_NONE,
						&offScreenDesc,
						D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
						&ofsClear,
						IID_PPV_ARGS(mOffScreenBuffer[i].GetAddressOf())));
					ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
					md3dDevice->CreateRenderTargetView(mOffScreenBuffer[i].Get(), &rtvDesc, rtvHeapHandle);
					rtvHeapHandle.Offset(1, mRtvDescriptorSize);
				}

				// Create the depth/stencil buffer and view.
				D3D12_RESOURCE_DESC depthStencilDesc;
				depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
				depthStencilDesc.Alignment = 0;
				depthStencilDesc.Width = mRenderTargetSize.x;
				depthStencilDesc.Height = mRenderTargetSize.y;
				depthStencilDesc.DepthOrArraySize = 1;
				depthStencilDesc.MipLevels = 1;
				// Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from 
				// the depth buffer.  Therefore, because we need to create two views to the same resource:
				//   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
				//   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
				// we need to create the depth buffer resource with a typeless format.  
				depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
				depthStencilDesc.SampleDesc.Count = mMsaaCount;
				depthStencilDesc.SampleDesc.Quality = mMsaaQuality - 1;
				depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
				depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

				D3D12_CLEAR_VALUE optClear;
				optClear.Format = mDepthStencilFormat;
				optClear.DepthStencil.Depth = 1.0f;
				optClear.DepthStencil.Stencil = 0;
				ThrowIfFailed(md3dDevice->CreateCommittedResource(
					&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
					D3D12_HEAP_FLAG_NONE,
					&depthStencilDesc,
					D3D12_RESOURCE_STATE_COMMON,
					&optClear,
					IID_PPV_ARGS(mDepthStencilBufferMS.GetAddressOf())));

				depthStencilDesc.SampleDesc.Count = 1;
				depthStencilDesc.SampleDesc.Quality = 0;
				ThrowIfFailed(md3dDevice->CreateCommittedResource(
					&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
					D3D12_HEAP_FLAG_NONE,
					&depthStencilDesc,
					D3D12_RESOURCE_STATE_COMMON,
					&optClear,
					IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())));

				// Create descriptor to mip level 0 of entire resource using the format of the resource.
				D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
				dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
				dsvDesc.Format = mDepthStencilFormat;
				dsvDesc.Texture2D.MipSlice = 0;
				md3dDevice->CreateDepthStencilView(mDepthStencilBufferMS.Get(), &dsvDesc, DsvMS());

				dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
				md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, Dsv());

				// Transition the resource from its initial state to be used as a depth buffer.
				D3D12_RESOURCE_BARRIER barriers[2] =
				{
					CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBufferMS.Get(),
					D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE),
					CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
					D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE)
				};
				mCommandList->ResourceBarrier(2, barriers);
			}

			// Create read back buffers. Readback resources must be buffers.
			D3D12_RESOURCE_DESC referdesc = mSwapChainBuffer[0]->GetDesc();
			UINT64 totalResourceSize = 0;
			UINT64 fpRowPitch = 0;
			UINT fpRowCount = 0;
			md3dDevice->GetCopyableFootprints(	// Get the rowcount, pitch and size of the top mip
				&referdesc,
				0,
				1,
				0,
				nullptr,
				&fpRowCount,
				&fpRowPitch,
				&totalResourceSize);
			UINT64 dstRowPitch = (fpRowPitch + 255) & ~0xFF;	// Round up the srcPitch to multiples of 256
			mReadBackRowPitch = dstRowPitch;
			// Create read back buffer for render target.
			D3D12_RESOURCE_DESC readBackDesc;
			readBackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			readBackDesc.Alignment = referdesc.Alignment;
			readBackDesc.Width = dstRowPitch * referdesc.Height;
			readBackDesc.Height = 1;
			readBackDesc.DepthOrArraySize = 1;
			readBackDesc.MipLevels = 1;
			readBackDesc.Format = DXGI_FORMAT_UNKNOWN;
			readBackDesc.SampleDesc.Count = 1;
			readBackDesc.SampleDesc.Quality = 0;
			readBackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			readBackDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			ThrowIfFailed(md3dDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
				D3D12_HEAP_FLAG_NONE,
				&readBackDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(mReadBackBuffer.GetAddressOf())));

			referdesc = mDepthStencilBuffer->GetDesc();
			totalResourceSize = 0;
			fpRowPitch = 0;
			fpRowCount = 0;
			md3dDevice->GetCopyableFootprints(	// Get the rowcount, pitch and size of the top mip
				&referdesc,
				0,
				1,
				0,
				nullptr,
				&fpRowCount,
				&fpRowPitch,
				&totalResourceSize);
			dstRowPitch = (fpRowPitch + 255) & ~0xFF;	// Round up the srcPitch to multiples of 256
			mReadBackDepthRowPitch = dstRowPitch;
			// Create read back buffer for depth buffer.
			D3D12_RESOURCE_DESC readBackDepthDesc;
			readBackDepthDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
			readBackDepthDesc.Alignment = referdesc.Alignment;
			readBackDepthDesc.Width = dstRowPitch * referdesc.Height;
			readBackDepthDesc.Height = 1;
			readBackDepthDesc.DepthOrArraySize = 1;
			readBackDepthDesc.MipLevels = 1;
			readBackDepthDesc.Format = DXGI_FORMAT_UNKNOWN;
			readBackDepthDesc.SampleDesc.Count = 1;
			readBackDepthDesc.SampleDesc.Quality = 0;
			readBackDepthDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
			readBackDepthDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
			ThrowIfFailed(md3dDevice->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
				D3D12_HEAP_FLAG_NONE,
				&readBackDepthDesc,
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(mReadBackDepthBuffer.GetAddressOf())));

			// Update the viewport transform to cover the client area.
			mScreenViewport.TopLeftX = 0;
			mScreenViewport.TopLeftY = 0;
			mScreenViewport.Width = static_cast<float>(mRenderTargetSize.x);
			mScreenViewport.Height = static_cast<float>(mRenderTargetSize.y);
			mScreenViewport.MinDepth = 0.0f;
			mScreenViewport.MaxDepth = 1.0f;
			mScissorRect = { 0, 0, mRenderTargetSize.x, mRenderTargetSize.y };

			mCommandList->RSSetViewports(1, &mScreenViewport);
			mCommandList->RSSetScissorRects(1, &mScissorRect);

			// Execute the resize commands.
			ThrowIfFailed(mCommandList->Close());
			ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
			mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

			// Wait until resize is complete.
			FlushCommandQueue();
		}

		// This method is called when the window is created (or re-created).
		void DeviceResources::SetWindow(HINSTANCE app, HWND window)
		{
			mhAppInst = app;
			mhMainWnd = window;

			RECT rect;
			GetClientRect(mhMainWnd, &rect);
			mWindowSize.x = rect.right - rect.left;
			mWindowSize.y = rect.bottom - rect.top;

			CreateWindowSizeDependentResources();
		}

		// This method is called when the window size changed.
		void DeviceResources::SetWindowSize(Vector2i windowSize)
		{
			if (mWindowSize.x != windowSize.x || mWindowSize.y != windowSize.y)
			{
				mWindowSize = windowSize;
				CreateWindowSizeDependentResources();
			}
		}

		void DeviceResources::FlushCommandQueue()
		{
			// Advance the fence value to mark commands up to this fence point.
			mCurrentFence++;

			// Add an instruction to the command queue to set a new fence point.  Because we 
			// are on the GPU timeline, the new fence point won't be set until the GPU finishes
			// processing all the commands prior to this Signal().
			ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

			// Wait until the GPU has completed commands up to this fence point.
			if (mFence->GetCompletedValue() < mCurrentFence)
			{
				HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

				// Fire event when GPU hits current fence.  
				ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

				// Wait until the GPU hits current fence event is fired.
				WaitForSingleObject(eventHandle, INFINITE);
				CloseHandle(eventHandle);
			}
		}

		// Determine the dimensions of the render target and whether it will be scaled down.
		void DeviceResources::UpdateRenderTargetSize()
		{
			int width = mWindowSize.x;
			int height = mWindowSize.y;

			if (mMaxWidth <= 0 || mMaxHeight <= 0)
			{
				mRenderTargetSize.x = width;
				mRenderTargetSize.y = height;
				return;
			}

			float aspect = (float)width / height;
			int maxWidth = std::min(width, (int)mMaxWidth);
			int maxheight = std::min(height, (int)mMaxHeight);

			mRenderTargetSize.x = aspect > 1.0f ? maxWidth : (int)(maxheight * aspect);
			mRenderTargetSize.y = aspect > 1.0f ? (int)(maxWidth / aspect) : maxheight;
		}

		// Recreate all device resources and set them back to the current state.
		void DeviceResources::HandleDeviceLost()
		{
			mSwapChain.Reset();
			mCommandQueue.Reset();
			mCommandList.Reset();
			mDirectCmdListAlloc.Reset();
			mFence.Reset();
			mCurrentFence = 0;
			mRtvHeap.Reset();
			mDsvHeap.Reset();

			for (int i = 0; i < SwapChainBufferCount; ++i)
			{
				mSwapChainBuffer[i].Reset();
				mOffScreenBuffer[i].Reset();
			}
			mDepthStencilBuffer.Reset();
			mDepthStencilBufferMS.Reset();
			mMsaaQuality = 0;

			if (mDeviceNotify != nullptr)
			{
				mDeviceNotify->OnDeviceLost();
			}

			CreateDeviceResources();
			CreateWindowSizeDependentResources();

			if (mDeviceNotify != nullptr)
			{
				mDeviceNotify->OnDeviceRestored();
			}
		}

		// Register our DeviceNotify to be informed on device lost and creation.
		void DeviceResources::RegisterDeviceNotify(IDeviceNotify* deviceNotify)
		{
			mDeviceNotify = deviceNotify;
		}

		// Call this method when the app suspends. It provides a hint to the driver that the app 
		// is entering an idle state and that temporary buffers can be reclaimed for use by other apps.
		void DeviceResources::Trim()
		{
			ComPtr<IDXGIDevice3> dxgiDevice;
			md3dDevice.As(&dxgiDevice);

			dxgiDevice->Trim();
		}

		// Prepare presentation work.
		void DeviceResources::PreparePresent()
		{
			mCommandList->RSSetViewports(1, &mScreenViewport);
			mCommandList->RSSetScissorRects(1, &mScissorRect);

			// Indicate a state transition on the resource usage.
			if (mMsaaQuality <= 0)
			{
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
					D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
				// WE ALREADY WROTE THE DEPTH INFO TO THE DEPTH BUFFER IN DrawNormalsAndDepth,
				// SO DO NOT CLEAR DEPTH.
			}
			else
			{
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentOffScreenBuffer(),
					D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET));
				mCommandList->ClearDepthStencilView(DsvMS(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
			}

			// Clear the back buffer.
			mCommandList->ClearRenderTargetView(CurrentRtv(), DirectX::Colors::Black, 0, nullptr);

			// Specify the buffers we are going to render to.
			if (mMsaaQuality <= 0)
			{
				mCommandList->OMSetRenderTargets(1, &CurrentRtv(), true, &Dsv());
			}
			else
			{
				mCommandList->OMSetRenderTargets(1, &CurrentRtv(), true, &DsvMS());
			}
		}

		// Present the contents of the swap chain to the screen.
		void DeviceResources::Present(UINT64& currentFrameFence)
		{
			// Indicate a state transition on the resource usage.
			if (mMsaaQuality <= 0)
			{
				mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
			}
			else
			{
				D3D12_RESOURCE_BARRIER barriers[2] =
				{
					CD3DX12_RESOURCE_BARRIER::Transition(CurrentOffScreenBuffer(),
					D3D12_RESOURCE_STATE_RENDER_TARGET,	D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
					CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
					D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST)
				};
				mCommandList->ResourceBarrier(2, barriers);

				mCommandList->ResolveSubresource(CurrentBackBuffer(), 0,
					CurrentOffScreenBuffer(), 0, mBackBufferFormat);

				D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
					CurrentBackBuffer(),
					D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PRESENT);
				mCommandList->ResourceBarrier(1, &barrier);
			}

			// Done recording commands.
			ThrowIfFailed(mCommandList->Close());

			// Add the command list to the queue for execution.
			ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
			mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

			// The first argument instructs DXGI to block until VSync, putting the application
			// to sleep until the next VSync. This ensures we don't waste any cycles rendering
			// frames that will never be displayed to the screen.
			HRESULT hr = mSwapChain->Present(1, 0);
			// Swap the back and front buffers.
			mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

			// Advance the fence value to mark commands up to this fence point.
			currentFrameFence = ++mCurrentFence;

			// Add an instruction to the command queue to set a new fence point. 
			// Because we are on the GPU timeline, the new fence point won't be 
			// set until the GPU finishes processing all the commands prior to this Signal().
			mCommandQueue->Signal(mFence.Get(), mCurrentFence);

			// If the device was removed either by a disconnection or a driver upgrade, we 
			// must recreate all device resources.
			if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
			{
				HandleDeviceLost();
			}
			else
			{
				ThrowIfFailed(hr);
			}
		}

		// Read back the render target buffer.
		RetrieveImageData* DeviceResources::RetrieveRenderTargetBuffer()
		{
			// Flush before changing any resources.
			FlushCommandQueue();
			ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

			// Note that the swap chain has already been swapped. So what we actually need to fetch is the previous back buffer.
			ID3D12Resource* sourceBuffer = mSwapChainBuffer[(mCurrBackBuffer + SwapChainBufferCount - 1) % SwapChainBufferCount].Get();

			// Transition the resource.
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(sourceBuffer,
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE));
			// Get the copy target location.
			D3D12_RESOURCE_DESC referdesc = sourceBuffer->GetDesc();
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
			bufferFootprint.Footprint.Width = static_cast<UINT>(referdesc.Width);
			bufferFootprint.Footprint.Height = referdesc.Height;
			bufferFootprint.Footprint.Depth = 1;
			bufferFootprint.Footprint.RowPitch = static_cast<UINT>(mReadBackRowPitch);
			bufferFootprint.Footprint.Format = referdesc.Format;

			CD3DX12_TEXTURE_COPY_LOCATION copyDest(mReadBackBuffer.Get(), bufferFootprint);
			CD3DX12_TEXTURE_COPY_LOCATION copySrc(sourceBuffer, 0);

			// Copy the texture
			mCommandList->CopyTextureRegion(&copyDest, 0, 0, 0, &copySrc, nullptr);

			// Transition the resource.
			mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(sourceBuffer,
				D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT));

			// Wait to finish.
			ThrowIfFailed(mCommandList->Close());
			ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
			mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
			FlushCommandQueue();

			RetrieveImageData* res = new RetrieveImageData();
			res->Width = static_cast<UINT>(referdesc.Width);
			res->Height = static_cast<UINT>(referdesc.Height);
			res->Pitch = static_cast<UINT>(mReadBackRowPitch);
			res->Data.resize(res->Pitch * res->Height);
			BYTE* mMappedData = nullptr;
			ThrowIfFailed(mReadBackBuffer->Map(0, &CD3DX12_RANGE(0, mReadBackRowPitch * referdesc.Height), reinterpret_cast<void**>(&mMappedData)));
			memcpy(&res->Data[0], mMappedData, res->Data.size());
			mReadBackBuffer->Unmap(0, &CD3DX12_RANGE(0, 0));

			return res;
		}

		// Read back the depth buffer.
		RetrieveImageData* DeviceResources::RetrieveDepthBufferBuffer()
		{
			return nullptr;
		}

		// Save a read back texture buffer to local image using WIC.
		void DeviceResources::SaveToLocalImage(RetrieveImageData* data, const std::string& file)
		{
			std::wstring lname = AnsiToWString(file);
			auto fileName = lname.c_str();

			// The source data format is always treated as 32RGBA. We use bmp natie codec. It will encode as 32BGRA format. So we should do a conversion.
			WICPixelFormatGUID pfGuid = GUID_WICPixelFormat32bppRGBA;
			WICPixelFormatGUID targetGuid = GUID_WICPixelFormat32bppBGRA;

			auto pWIC = DirectX::_GetWIC();
			if (!pWIC)
			{
				LOG(ERROR, "Cannot get the WIC factory.");
				return;
			}

			ComPtr<IWICStream> stream;
			ThrowIfFailed(pWIC->CreateStream(stream.GetAddressOf()));
			ThrowIfFailed(stream->InitializeFromFilename(fileName, GENERIC_WRITE));
			DirectX::auto_delete_file_wic delonfail(stream, fileName);
			ComPtr<IWICBitmapEncoder> encoder;
			ThrowIfFailed(pWIC->CreateEncoder(GUID_ContainerFormatBmp, nullptr, encoder.GetAddressOf()));
			ThrowIfFailed(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache));
			ComPtr<IWICBitmapFrameEncode> frame;
			ComPtr<IPropertyBag2> props;
			ThrowIfFailed(encoder->CreateNewFrame(frame.GetAddressOf(), props.GetAddressOf()));

			// Opt-in to the WIC2 support for writing 32-bit Windows BMP files with an alpha channel
			PROPBAG2 option = {};
			option.pstrName = const_cast<wchar_t*>(L"EnableV5Header32bppBGRA");
			VARIANT varValue;
			varValue.vt = VT_BOOL;
			varValue.boolVal = VARIANT_TRUE;
			(void)props->Write(1, &option, &varValue);

			ThrowIfFailed(frame->Initialize(props.Get()));
			ThrowIfFailed(frame->SetSize(static_cast<UINT>(data->Width), data->Height));
			ThrowIfFailed(frame->SetResolution(72, 72));
			ThrowIfFailed(frame->SetPixelFormat(&targetGuid));

			// Do conversion.
			ComPtr<IWICBitmap> source;
			ThrowIfFailed(pWIC->CreateBitmapFromMemory(static_cast<UINT>(data->Width), data->Height, pfGuid,
				static_cast<UINT>(data->Pitch), static_cast<UINT>(data->Pitch * data->Height), &(data->Data[0]), source.GetAddressOf()));
			ComPtr<IWICFormatConverter> FC;
			ThrowIfFailed(pWIC->CreateFormatConverter(FC.GetAddressOf()));
			ThrowIfFailed(FC->Initialize(source.Get(), targetGuid, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeMedianCut));
			WICRect rect = { 0, 0, static_cast<INT>(data->Width), static_cast<INT>(data->Height) };
			ThrowIfFailed(frame->WriteSource(FC.Get(), &rect));

			ThrowIfFailed(frame->Commit());
			ThrowIfFailed(encoder->Commit());
			delonfail.clear();
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE DeviceResources::CurrentRtv() const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(
				mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
				mCurrBackBuffer,
				mRtvDescriptorSize);
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE DeviceResources::Dsv() const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart());
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE DeviceResources::DsvMS() const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(
				mDsvHeap->GetCPUDescriptorHandleForHeapStart(),
				1,
				mDsvDescriptorSize);
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE DeviceResources::GetDsv(int index) const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(
				mDsvHeap->GetCPUDescriptorHandleForHeapStart(),
				index,
				mDsvDescriptorSize);
		}

		CD3DX12_CPU_DESCRIPTOR_HANDLE DeviceResources::GetRtv(int index) const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(
				mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
				index,
				mRtvDescriptorSize);
		}

		ID3D12Resource* DeviceResources::CurrentBackBuffer() const
		{
			return mSwapChainBuffer[mCurrBackBuffer].Get();
		}

		ID3D12Resource* DeviceResources::DepthStencilBuffer() const
		{
			return mDepthStencilBuffer.Get();
		}

		ID3D12Resource* DeviceResources::CurrentOffScreenBuffer() const
		{
			return mOffScreenBuffer[mCurrBackBuffer].Get();
		}

		ID3D12Resource* DeviceResources::DepthStencilBufferMS() const
		{
			return mDepthStencilBufferMS.Get();
		}

		ID3D12Resource* DeviceResources::ReadBackBuffer() const
		{
			return mReadBackBuffer.Get();
		}

		ID3D12Resource* DeviceResources::ReadBackDepthBuffer() const
		{
			return mReadBackDepthBuffer.Get();
		}

		// Be sure to pick up the first adapter that supports D3D12, use the following code.
		void DeviceResources::GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter)
		{
			*ppAdapter = nullptr;
			for (UINT adapterIndex = 0; ; ++adapterIndex)
			{
				IDXGIAdapter1* pAdapter = nullptr;
				if (DXGI_ERROR_NOT_FOUND == pFactory->EnumAdapters1(adapterIndex, &pAdapter))
				{
					// No more adapters to enumerate.
					break;
				}

				// Check to see if the adapter supports Direct3D 12, but don't create the
				// actual device yet.
				if (SUCCEEDED(D3D12CreateDevice(pAdapter, mMinFeatureLevel, _uuidof(ID3D12Device), nullptr)))
				{
					*ppAdapter = pAdapter;
					return;
				}
				pAdapter->Release();
			}
		}

		void DeviceResources::LogAdapters()
		{
			UINT i = 0;
			IDXGIAdapter* adapter = nullptr;
			std::vector<IDXGIAdapter*> adapterList;
			while (mdxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
			{
				DXGI_ADAPTER_DESC desc;
				adapter->GetDesc(&desc);

				std::wstring text = L"***Adapter: ";
				text += desc.Description;
				text += L"\n";

				OutputDebugString(text.c_str());

				adapterList.push_back(adapter);

				++i;
			}

			for (size_t i = 0; i < adapterList.size(); ++i)
			{
				LogAdapterOutputs(adapterList[i]);
				ReleaseCom(adapterList[i]);
			}
		}

		void DeviceResources::LogAdapterOutputs(IDXGIAdapter* adapter)
		{
			UINT i = 0;
			IDXGIOutput* output = nullptr;
			while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
			{
				DXGI_OUTPUT_DESC desc;
				output->GetDesc(&desc);

				std::wstring text = L"***Output: ";
				text += desc.DeviceName;
				text += L"\n";
				OutputDebugString(text.c_str());

				LogOutputDisplayModes(output, mBackBufferFormat);

				ReleaseCom(output);

				++i;
			}
		}

		void DeviceResources::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
		{
			UINT count = 0;
			UINT flags = 0;

			// Call with nullptr to get list count.
			output->GetDisplayModeList(format, flags, &count, nullptr);

			std::vector<DXGI_MODE_DESC> modeList(count);
			output->GetDisplayModeList(format, flags, &count, &modeList[0]);

			for (auto& x : modeList)
			{
				UINT n = x.RefreshRate.Numerator;
				UINT d = x.RefreshRate.Denominator;
				std::wstring text =
					L"Width = " + std::to_wstring(x.Width) + L" " +
					L"Height = " + std::to_wstring(x.Height) + L" " +
					L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
					L"\n";

				::OutputDebugString(text.c_str());
			}
		}

	}	// namespace rendering
}	// namespace handwork