// Frame resource for rendering. Should be customed for each application according to shader definition.

#pragma once

#include "d3dutil.h"
#include "uploadbuffer.h"

namespace handwork
{
	namespace rendering
	{
		// Stores the resources needed for the CPU to build the command lists for a frame.  
		struct FrameResource
		{
		public:
			FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT instaceCount);
			FrameResource(const FrameResource& rhs) = delete;
			FrameResource& operator=(const FrameResource& rhs) = delete;
			~FrameResource();

			// We cannot reset the allocator until the GPU is done processing the commands.
			// So each frame needs their own allocator.
			Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CmdListAlloc;

			// We cannot update a cbuffer until the GPU is done processing the commands
			// that reference it.  So each frame needs their own cbuffers.
			std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
			std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;
			std::unique_ptr<UploadBuffer<SsaoConstants>> SsaoCB = nullptr;
			std::unique_ptr<UploadBuffer<MaterialData>> MaterialBuffer = nullptr;
			std::unique_ptr<UploadBuffer<InstanceData>> InstanceBuffer = nullptr;

			// Fence value to mark commands up to this fence point.  This lets us
			// check if these frame resources are still in use by the GPU.
			UINT64 Fence = 0;
		};

	}	// namespace rendering
}	// namespace handwork