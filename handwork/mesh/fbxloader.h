// Fbx file importer.

#pragma once

#include "../utility/utility.h"
#include "../utility/transform.h"
#include "../utility/geometry.h"

namespace handwork
{
	struct Joint
	{
		Joint() : Parent(-1) {}
		Joint(std::string name, int parent, Matrix4x4 m)
			: Name(name), Parent(parent), GlobalBindposeInverse(m) {}

		std::string Name;
		int Parent;
		Matrix4x4 GlobalBindposeInverse;	// Transform matrix from mesh space to bone space
		Vector3f Translation;	// Local translation matrix
		Vector3f Scaling;		// Local scaling matrix 
		Vector3f Rotation;		// Local 3 axis rotation in degree
	};

	struct BlendPair
	{
		BlendPair() : Index(-1), Weight(-1.0f) {}
		BlendPair(int index, float weight) 
			: Index(index), Weight(weight) {}

		int Index;
		float Weight;
	};

	struct Vertex
	{
		Vertex() : Position(0, 0, 0), Normal(0, 0, 0), Tangent(0, 0, 0)
		{
			BlendInfo.reserve(4);
		}

		Vector3f Position;
		Vector3f Normal;
		Vector3f Tangent;
		std::vector<BlendPair> BlendInfo;
	};

	bool ImportFbx(const char* filename, float& fileScale, std::vector<Joint>& skeleton,
		std::vector<Vertex>& meshVertices, std::vector<int>& meshIndices);

	bool ImportFbx(const std::string& filename, float& fileScale, std::vector<Joint>& skeleton,
		std::vector<Vertex>& meshVertices, std::vector<int>& meshIndices);
		
}	// namespace dhandwork

