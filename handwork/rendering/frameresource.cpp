// Frame resource for rendering. Should be customed for each application according to shader definition.

#include "frameresource.h"

namespace handwork
{
	namespace rendering
	{
		FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount, UINT instanceCount)
		{
			ThrowIfFailed(device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

			PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, std::max(passCount, (UINT)1), true);
			SsaoCB = std::make_unique<UploadBuffer<SsaoConstants>>(device, 1, true);
			MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, std::max(materialCount, (UINT)1), false);
			ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, std::max(objectCount, (UINT)1), true);
			InstanceBuffer = std::make_unique<UploadBuffer<InstanceData>>(device, std::max(instanceCount, (UINT)1), false);
		}

		FrameResource::~FrameResource()
		{
		}

	}	// namespace rendering
}	// namespace handwork