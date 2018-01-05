// General helper code for d3d rendering.

#include "d3dutil.h"
#include <comdef.h>
#include <fstream>

namespace handwork
{
	namespace rendering
	{
		using Microsoft::WRL::ComPtr;
		using namespace DirectX;

		bool d3dUtil::IsKeyDown(int vkeyCode)
		{
			return (GetAsyncKeyState(vkeyCode) & 0x8000) != 0;
		}

		ComPtr<ID3DBlob> d3dUtil::LoadBinary(const std::wstring& filename)
		{
			std::ifstream fin(filename, std::ios::binary);

			fin.seekg(0, std::ios_base::end);
			std::ifstream::pos_type size = (int)fin.tellg();
			fin.seekg(0, std::ios_base::beg);

			ComPtr<ID3DBlob> blob;
			ThrowIfFailed(D3DCreateBlob(size, blob.GetAddressOf()));

			fin.read((char*)blob->GetBufferPointer(), size);
			fin.close();

			return blob;
		}

		Microsoft::WRL::ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
			ID3D12Device* device,
			ID3D12GraphicsCommandList* cmdList,
			const void* initData,
			UINT64 byteSize,
			Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer)
		{
			ComPtr<ID3D12Resource> defaultBuffer;

			// Create the actual default buffer resource.
			ThrowIfFailed(device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
				D3D12_RESOURCE_STATE_COMMON,
				nullptr,
				IID_PPV_ARGS(defaultBuffer.GetAddressOf())));

			// In order to copy CPU memory data into our default buffer, we need to create
			// an intermediate upload heap. 
			ThrowIfFailed(device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(byteSize),
				D3D12_RESOURCE_STATE_GENERIC_READ,
				nullptr,
				IID_PPV_ARGS(uploadBuffer.GetAddressOf())));


			// Describe the data we want to copy into the default buffer.
			D3D12_SUBRESOURCE_DATA subResourceData = {};
			subResourceData.pData = initData;
			subResourceData.RowPitch = byteSize;
			subResourceData.SlicePitch = subResourceData.RowPitch;

			// Schedule to copy the data to the default buffer resource.  At a high level, the helper function UpdateSubresources
			// will copy the CPU memory into the intermediate upload heap.  Then, using ID3D12CommandList::CopySubresourceRegion,
			// the intermediate upload heap data will be copied to mBuffer.
			cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
				D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
			UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subResourceData);
			cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(defaultBuffer.Get(),
				D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

			// Note: uploadBuffer has to be kept alive after the above function calls because
			// the command list has not been executed yet that performs the actual copy.
			// The caller can Release the uploadBuffer after it knows the copy has been executed.

			return defaultBuffer;
		}

		ComPtr<ID3DBlob> d3dUtil::CompileShader(
			const std::wstring& filename,
			const D3D_SHADER_MACRO* defines,
			const std::string& entrypoint,
			const std::string& target)
		{
			UINT compileFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)  
			compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

			HRESULT hr = S_OK;

			ComPtr<ID3DBlob> byteCode = nullptr;
			ComPtr<ID3DBlob> errors;
			hr = D3DCompileFromFile(filename.c_str(), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
				entrypoint.c_str(), target.c_str(), compileFlags, 0, &byteCode, &errors);

			if (errors != nullptr)
				OutputDebugStringA((char*)errors->GetBufferPointer());

			ThrowIfFailed(hr);

			return byteCode;
		}

		DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber) :
			ErrorCode(hr),
			FunctionName(functionName),
			Filename(filename),
			LineNumber(lineNumber)
		{
		}

		std::wstring DxException::ToString()const
		{
			// Get the string description of the error code.
			_com_error err(ErrorCode);
			std::wstring msg = err.ErrorMessage();

			return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
		}

		BoundingBox d3dUtil::MergeBoundingBox(const std::vector<DirectX::BoundingBox>& boxList)
		{
			BoundingBox temp0, temp1;
			temp0 = boxList[0];
			for (UINT i = 1; i < boxList.size(); ++i)
			{
				BoundingBox::CreateMerged(temp1, temp0, boxList[i]);
				if (++i >= boxList.size())
					return temp1;
				BoundingBox::CreateMerged(temp0, temp1, boxList[i]);
			}
			return temp0;
		}

		BoundingSphere d3dUtil::MergeBoundingSphere(const std::vector<DirectX::BoundingSphere>& sphereList)
		{
			BoundingSphere temp0, temp1;
			temp0 = sphereList[0];
			for (UINT i = 1; i < sphereList.size(); ++i)
			{
				BoundingSphere::CreateMerged(temp1, temp0, sphereList[i]);
				if (++i >= sphereList.size())
					return temp1;
				BoundingSphere::CreateMerged(temp0, temp1, sphereList[i]);
			}
			return temp0;
		}

		Matrix4x4 d3dUtil::CameraLookAt(const Vector3f& pos, const Vector3f& target, const Vector3f& up)
		{
			Vector3f L = Normalize(target - pos);
			Vector3f R = Normalize(Cross(up, L));
			Vector3f U = Cross(L, R);

			Matrix4x4 view;

			// Fill in the view matrix entries.
			float x = -Dot(pos, R);
			float y = -Dot(pos, U);
			float z = -Dot(pos, L);

			view.m[0][0] = R.x;
			view.m[0][1] = R.y;
			view.m[0][2] = R.z;
			view.m[0][3] = x;

			view.m[1][0] = U.x;
			view.m[1][1] = U.y;
			view.m[1][2] = U.z;
			view.m[1][3] = y;

			view.m[2][0] = L.x;
			view.m[2][1] = L.y;
			view.m[2][2] = L.z;
			view.m[2][3] = z;

			view.m[3][0] = 0.0f;
			view.m[3][1] = 0.0f;
			view.m[3][2] = 0.0f;
			view.m[3][3] = 1.0f;

			return view;
		}

	}	// namespace rendering
}	// namespace handwork

