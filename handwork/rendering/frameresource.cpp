// Frame resource for rendering. Should be customed for each application according to shader definition.

#include "frameresource.h"

namespace handwork
{
	namespace rendering
	{
		FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount, UINT materialCount)
		{
			ThrowIfFailed(device->CreateCommandAllocator(
				D3D12_COMMAND_LIST_TYPE_DIRECT,
				IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

			PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
			SsaoCB = std::make_unique<UploadBuffer<SsaoConstants>>(device, 1, true);
			MaterialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);
			ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(device, objectCount, true);
		}

		FrameResource::~FrameResource()
		{
		}

	}	// namespace rendering
}	// namespace handwork