// General helper code for d3d rendering.

#pragma once

#include <windows.h>
#include <wrl.h>
#include <dxgi1_4.h>
#include <d3d12.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <string>
#include <memory>
#include <algorithm>
#include <vector>
#include <array>
#include <unordered_map>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <cassert>
#include "d3dx12.h"
#include "MathHelper.h"
#include "bridgestructs.h"

#pragma warning(disable : 4244)

namespace handwork
{
	namespace rendering
	{
		const int gNumFrameResources = 3;

		inline void d3dSetDebugName(IDXGIObject* obj, const char* name)
		{
			if (obj)
			{
				obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
			}
		}
		inline void d3dSetDebugName(ID3D12Device* obj, const char* name)
		{
			if (obj)
			{
				obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
			}
		}
		inline void d3dSetDebugName(ID3D12DeviceChild* obj, const char* name)
		{
			if (obj)
			{
				obj->SetPrivateData(WKPDID_D3DDebugObjectName, lstrlenA(name), name);
			}
		}

		inline std::wstring AnsiToWString(const std::string& str)
		{
			WCHAR buffer[512];
			MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, buffer, 512);
			return std::wstring(buffer);
		}

		class d3dUtil
		{
		public:

			static bool IsKeyDown(int vkeyCode);

			static std::string ToString(HRESULT hr);

			static UINT CalcConstantBufferByteSize(UINT byteSize)
			{
				// Constant buffers must be a multiple of the minimum hardware
				// allocation size (usually 256 bytes).  So round up to nearest
				// multiple of 256.  We do this by adding 255 and then masking off
				// the lower 2 bytes which store all bits < 256.
				// Example: Suppose byteSize = 300.
				// (300 + 255) & ~255
				// 555 & ~255
				// 0x022B & ~0x00ff
				// 0x022B & 0xff00
				// 0x0200
				// 512
				return (byteSize + 255) & ~255;
			}

			static Microsoft::WRL::ComPtr<ID3DBlob> LoadBinary(const std::wstring& filename);

			static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
				ID3D12Device* device,
				ID3D12GraphicsCommandList* cmdList,
				const void* initData,
				UINT64 byteSize,
				Microsoft::WRL::ComPtr<ID3D12Resource>& uploadBuffer);

			static Microsoft::WRL::ComPtr<ID3DBlob> CompileShader(
				const std::wstring& filename,
				const D3D_SHADER_MACRO* defines,
				const std::string& entrypoint,
				const std::string& target);

			// Merge bounding box / sphere
			static DirectX::BoundingBox MergeBoundingBox(const std::vector<DirectX::BoundingBox>& boxList);
			static DirectX::BoundingSphere MergeBoundingSphere(const std::vector<DirectX::BoundingSphere>& sphereList);

			// Quick create camera view matrix
			static Matrix4x4 CameraLookAt(const Vector3f& pos, const Vector3f& target, const Vector3f& up);

			static Matrix4x4 ConvertToMatrix4x4(const DirectX::XMFLOAT4X4& tf)
			{
				return Matrix4x4(
					tf.m[0][0], tf.m[0][1], tf.m[0][2], tf.m[0][3],
					tf.m[1][0], tf.m[1][1], tf.m[1][2], tf.m[1][3],
					tf.m[2][0], tf.m[2][1], tf.m[2][2], tf.m[2][3],
					tf.m[3][0], tf.m[3][1], tf.m[3][2], tf.m[3][3]);
			}

			static DirectX::XMFLOAT4X4 ConvertToXMFLOAT4x4(const Matrix4x4& tf)
			{
				return DirectX::XMFLOAT4X4(
					tf.m[0][0], tf.m[0][1], tf.m[0][2], tf.m[0][3],
					tf.m[1][0], tf.m[1][1], tf.m[1][2], tf.m[1][3],
					tf.m[2][0], tf.m[2][1], tf.m[2][2], tf.m[2][3],
					tf.m[3][0], tf.m[3][1], tf.m[3][2], tf.m[3][3]);
			}
		};

		class DxException
		{
		public:
			DxException() = default;
			DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber);

			std::wstring ToString()const;

			HRESULT ErrorCode = S_OK;
			std::wstring FunctionName;
			std::wstring Filename;
			int LineNumber = -1;
		};

		// Defines a subrange of geometry in a MeshGeometry.  This is for when multiple
		// geometries are stored in one vertex and index buffer.  It provides the offsets
		// and data needed to draw a subset of geometry stores in the vertex and index 
		// buffers so that we can implement the technique described by Figure 6.3.
		struct SubmeshGeometry
		{
			UINT IndexCount = 0;
			UINT VertexCount = 0;
			UINT StartIndexLocation = 0;
			INT BaseVertexLocation = 0;

			// Bounding box of the geometry defined by this submesh. 
			// This is used in later chapters of the book.
			DirectX::BoundingBox BoxBounds;
			DirectX::BoundingSphere SphereBounds;
		};

		struct MeshGeometry
		{
			// Give it a name so we can look it up by name.
			std::string Name;

			// System memory copies.  Use Blobs because the vertex/index format can be generic.
			// It is up to the client to cast appropriately.  
			Microsoft::WRL::ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
			Microsoft::WRL::ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

			Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
			Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

			Microsoft::WRL::ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
			Microsoft::WRL::ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

			// Data about the buffers.
			UINT VertexByteStride = 0;
			UINT VertexBufferByteSize = 0;
			DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;
			UINT IndexBufferByteSize = 0;

			// A MeshGeometry may store multiple geometries in one vertex/index buffer.
			// Use this container to define the Submesh geometries so we can draw
			// the Submeshes individually.
			std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

			D3D12_VERTEX_BUFFER_VIEW VertexBufferView()const
			{
				D3D12_VERTEX_BUFFER_VIEW vbv;
				vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
				vbv.StrideInBytes = VertexByteStride;
				vbv.SizeInBytes = VertexBufferByteSize;

				return vbv;
			}

			D3D12_INDEX_BUFFER_VIEW IndexBufferView()const
			{
				D3D12_INDEX_BUFFER_VIEW ibv;
				ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
				ibv.Format = IndexFormat;
				ibv.SizeInBytes = IndexBufferByteSize;

				return ibv;
			}

			// We can free this memory after we finish upload to the GPU.
			void DisposeUploaders()
			{
				VertexBufferUploader = nullptr;
				IndexBufferUploader = nullptr;
			}
		};

		// Simple struct to represent a material for our demos.  A production 3D engine
		// would likely create a class hierarchy of Materials.
		struct Material
		{
			// Unique material name for lookup.
			std::string Name;

			// Index into constant buffer corresponding to this material.
			int MatCBIndex = -1;

			// Dirty flag indicating the material has changed and we need to update the constant buffer.
			// Because we have a material constant buffer for each FrameResource, we have to apply the
			// update to each FrameResource.  Thus, when we modify a material we should set 
			// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
			int NumFramesDirty = gNumFrameResources;

			// Material constant buffer data used for shading.
			Vector4f DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
			Vector3f FresnelR0 = { 0.01f, 0.01f, 0.01f };
			float Roughness = .25f;
		};

		// Simple struct to represent a instance specific data.
		struct Instance
		{
			Matrix4x4 World;
			std::string MatName;
		};

		struct RenderItemData
		{
			std::string Name;
			Matrix4x4 World;
			std::string MatName;
			std::string GeoName;
			std::string DrawArgName;
			D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			std::vector<Instance> Instances;
		};

		// Lightweight structure stores parameters to draw a shape.  This will
		// vary from app-to-app.
		struct RenderItem
		{
			RenderItem() = default;
			RenderItem(const RenderItem& rhs) = delete;

			// Unioque render item name for look up.
			std::string Name;

			// Dirty flag indicating the object data has changed and we need to update the constant buffer.
			// Because we have an object cbuffer for each FrameResource, we have to apply the
			// update to each FrameResource.  Thus, when we modify obect data we should set 
			// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
			int NumFramesDirty = gNumFrameResources;

			// World matrix of the shape that describes the object's local space
			// relative to the world space, which defines the position, orientation,
			// and scale of the object in the world.
			Matrix4x4 World;

			// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
			UINT ObjCBIndex = -1;

			Material* Mat = nullptr;
			MeshGeometry* Geo = nullptr;
			SubmeshGeometry* SubMesh = nullptr;

			// Primitive topology.
			D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

			// DrawIndexedInstanced parameters.
			UINT IndexCount = 0;
			UINT StartIndexLocation = 0;
			int BaseVertexLocation = 0;

			// Instance drawing.
			std::vector<InstanceData> Instances;
			UINT InstCBIndex = -1;
		};

#ifndef ThrowIfFailed
#define ThrowIfFailed(x)                                              \
{                                                                     \
    HRESULT hr__ = (x);                                               \
    std::wstring wfn = AnsiToWString(__FILE__);                       \
    if(FAILED(hr__)) { throw DxException(hr__, L#x, wfn, __LINE__); } \
}
#endif

#ifndef ReleaseCom
#define ReleaseCom(x) { if(x){ x->Release(); x = 0; } }
#endif

	}	// namespace rendering
}	// namespace handwork