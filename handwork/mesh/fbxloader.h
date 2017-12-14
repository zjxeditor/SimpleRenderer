// Fbx file importer.

#pragma once

#include "../utility/utility.h"
#include "../utility/transform.h"
#include "fbxsdk.h"

namespace handwork
{
	struct Joint
	{
		Joint() : Valid(false)
		{}

		std::string Name;
		int Parent;
		Matrix4x4 GlobalBindposeInverse;
		FbxCluster* Cluster;
		bool Valid;
	};

	class FbxLoader
	{
	public:
		bool ImportFile(const char* filename);

	private:
		int FindJoint(std::string name);
		void BakeTRS(FbxNode* rootNode);
		void BakeConfigure(FbxNode* node);
		void ProcessSkeletonHierarchy(FbxNode* rootNode);
		void ProcessSkeletonHierarchyRecursively(FbxNode* node, int myIndex, int inParentIndex);
		void ProcessSkeletonElimination(FbxNode* rootNode);
		void ProcessSkeletonEliminationRecursively(FbxNode* node);

	private:
		float fileScale;
		std::vector<Joint> Skeleton;

	};

}	// namespace dhandwork

