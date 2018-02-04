// Manage device related resources for DirectX.

#pragma once

#include "d3dUtil.h"

namespace handwork
{
	namespace rendering
	{
		// Provides an interface for an application that owns DeviceResources to be notified of the device being lost or created.
		class IDeviceNotify
		{
		public:
			virtual void OnDeviceLost() = 0;
			virtual void OnDeviceRestored() = 0;
		};

		enum class MSAATYPE
		{
			MSAAx1,
			MSAAx2,
			MSAAx4,
			MSAAx8
		};

		class RetrieveImageData
		{
		public:
			UINT Width;
			UINT Height;
			UINT Pitch;
			std::vector<BYTE> Data;
		};

		class DeviceResources
		{
		public:
			DeviceResources(MSAATYPE msaaType, UINT maxWidth = 1920, UINT maxHeight = 1080);
			~DeviceResources();
			void SetWindow(HINSTANCE app, HWND window);
			void SetWindowSize(Vector2i windowSize);
			void HandleDeviceLost();
			void RegisterDeviceNotify(IDeviceNotify* deviceNotify);
			void Trim();
			void PreparePresent(bool clearDepth = false);
			void Present(UINT64& currentFrameFence);
			void FlushCommandQueue();

			// Device accessors.
			Vector2i GetRenderTargetSize() const { return mRenderTargetSize; }
			Vector2i GetWindowSize() const { return mWindowSize; }

			// D3D Accessors.
			ID3D12Device* GetD3DDevice() const { return md3dDevice.Get(); }
			IDXGISwapChain* GetSwapChain() const { return mSwapChain.Get(); }
			ID3D12CommandQueue* GetCommandQueue() const { return mCommandQueue.Get(); }
			ID3D12CommandAllocator* GetDirectCmdListAlloc() const { return mDirectCmdListAlloc.Get(); }
			ID3D12GraphicsCommandList* GetCommandList() const { return mCommandList.Get(); }

			ID3D12Fence* GetFence() const { return mFence.Get(); }
			UINT64 GetCurrentFence() const { return mCurrentFence; }

			CD3DX12_CPU_DESCRIPTOR_HANDLE CurrentRtv() const;
			CD3DX12_CPU_DESCRIPTOR_HANDLE Dsv() const;
			CD3DX12_CPU_DESCRIPTOR_HANDLE DsvMS() const;
			CD3DX12_CPU_DESCRIPTOR_HANDLE GetDsv(int index) const;
			CD3DX12_CPU_DESCRIPTOR_HANDLE GetRtv(int index) const;

			D3D12_VIEWPORT GetScreenViewport() const { return mScreenViewport; }
			D3D12_RECT GetScissorRect() const { return mScissorRect; }

			ID3D12Resource* CurrentBackBuffer() const;
			ID3D12Resource* DepthStencilBuffer() const;
			ID3D12Resource* CurrentOffScreenBuffer() const;
			ID3D12Resource* DepthStencilBufferMS() const;
			ID3D12Resource* ReadBackBuffer() const;
			void ManualSwapBackBuffers(){ mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount; }

			UINT GetRtvSize() const { return mRtvDescriptorSize; }
			UINT GetDsvSize() const { return mDsvDescriptorSize; }
			UINT GetCbvSrvUavSize() const { return mCbvSrvUavDescriptorSize; }

			UINT GetMsaaCount() const { return mMsaaCount; }
			UINT GetMsaaQuality() const { return mMsaaQuality; }
			UINT GetSwapChainBufferCount() const { return SwapChainBufferCount; }
			DXGI_FORMAT GetBackBufferFormat() const { return mBackBufferFormat; }
			DXGI_FORMAT GetDepthStencilFormat() const { return mDepthStencilFormat; }

			// Note that the result need to be manually freed.
			RetrieveImageData* RetrieveRenderTargetBuffer();
			void SaveToLocalImage(RetrieveImageData* data, const std::string& file);

		private:
			void CreateDeviceIndependentResources();
			void CreateDeviceResources();
			void CreateWindowSizeDependentResources();
			void UpdateRenderTargetSize();
			void GetHardwareAdapter(IDXGIFactory4* pFactory, IDXGIAdapter1** ppAdapter);
			void LogAdapters();
			void LogAdapterOutputs(IDXGIAdapter* adapter);
			void LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format);

			// Direct3D objects.
			Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory;
			Microsoft::WRL::ComPtr<IDXGIAdapter1> mHardwareAdapter;
			Microsoft::WRL::ComPtr<ID3D12Device> md3dDevice;
			Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
			Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
			Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList;

			// Direct3D fence for sync.
			Microsoft::WRL::ComPtr<ID3D12Fence> mFence;
			UINT64 mCurrentFence = 0;

			// Direct3D rendering objects.
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;
			D3D12_VIEWPORT mScreenViewport;
			D3D12_RECT mScissorRect;

			// Direct3D resources.
			static const int SwapChainBufferCount = 2;
			int mCurrBackBuffer = 0;
			Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
			Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;

			// Direct3D descriptor size.
			UINT mRtvDescriptorSize = 0;
			UINT mDsvDescriptorSize = 0;
			UINT mCbvSrvUavDescriptorSize = 0;

			// MSAA
			Microsoft::WRL::ComPtr<ID3D12Resource> mOffScreenBuffer[SwapChainBufferCount];
			Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBufferMS;
			UINT mMsaaCount;
			UINT mMsaaQuality;

			// Readback
			Microsoft::WRL::ComPtr<ID3D12Resource> mReadBackBuffer;
			UINT64 mReadBackRowPitch = 0;

			// Direct3D properties.
			D3D_DRIVER_TYPE md3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
			DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
			DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			D3D_FEATURE_LEVEL mMinFeatureLevel = D3D_FEATURE_LEVEL_12_0;

			// Cached reference to the application and window.
			HINSTANCE mhAppInst = nullptr;
			HWND mhMainWnd = nullptr;

			// Cached device properties.
			Vector2i mRenderTargetSize;
			Vector2i mWindowSize;
			UINT mMaxWidth;
			UINT mMaxHeight;

			// The IDeviceNotify can be held directly as it owns the DeviceResources.
			IDeviceNotify* mDeviceNotify;
		};

	}	// namespace rendering
}	// namespace handwork

