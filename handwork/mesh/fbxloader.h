// Fbx file importer.

#pragma once

#include "../utility/utility.h"
#include "../utility/transform.h"
#include "../utility/geometry.h"

namespace handwork
{
	struct MeshJoint
	{
		MeshJoint() : Parent(-1) {}
		MeshJoint(std::string name, int parent, Matrix4x4 m)
			: Name(name), Parent(parent), GlobalBindposeInverse(m) {}

		std::string Name;
		int Parent;
		Matrix4x4 GlobalBindposeInverse;	// Transform matrix from mesh space to bone space
		Vector3f Translation;	// Local translation matrix
		Vector3f Scaling;		// Local scaling matrix 
		Vector3f Rotation;		// Local 3 axis rotation in degree
	};

	struct MeshBlendPair
	{
		MeshBlendPair() : Index(-1), Weight(-1.0f) {}
		MeshBlendPair(int index, float weight) 
			: Index(index), Weight(weight) {}

		int Index;
		float Weight;
	};

	struct MeshVertex
	{
		MeshVertex() : Position(0, 0, 0), Normal(0, 0, 0), Tangent(0, 0, 0)
		{
			BlendInfo.reserve(4);
		}

		Vector3f Position;
		Vector3f Normal;
		Vector3f Tangent;
		std::vector<MeshBlendPair> BlendInfo;
	};

	bool ImportFbx(const char* filename, float& fileScale, std::vector<MeshJoint>& skeleton,
		std::vector<MeshVertex>& meshVertices, std::vector<int>& meshIndices);

	bool ImportFbx(const std::string& filename, float& fileScale, std::vector<MeshJoint>& skeleton,
		std::vector<MeshVertex>& meshVertices, std::vector<int>& meshIndices);
		
}	// namespace dhandwork

