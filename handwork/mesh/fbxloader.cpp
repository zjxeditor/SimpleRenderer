// Fbx file importer.

#include "fbxloader.h"

namespace handwork
{
	void PrintNode(FbxNode* pNode);

	bool FbxLoader::ImportFile(const char* filename)
	{
		// Initialize the SDK manager. This object handles memory management.
		FbxManager* sdkManager = FbxManager::Create();

		// Create the IO settings object.
		FbxIOSettings *ios = FbxIOSettings::Create(sdkManager, IOSROOT);
		sdkManager->SetIOSettings(ios);

		// Create an importer using the SDK manager.
		FbxImporter* importer = FbxImporter::Create(sdkManager, "");

		// Use the first argument as the filename for the importer.
		if (!importer->Initialize(filename, -1, sdkManager->GetIOSettings()))
		{
			Error("Call to FbxImporter::Initialize() failed.");
			Error("Error returned:\n %s", importer->GetStatus().GetErrorString());
			return false;
		}

		// Create a new scene so that it can be populated by the imported file.
		FbxScene* scene = FbxScene::Create(sdkManager, "myScene");

		// Import the contents of the file into the scene.
		importer->Import(scene);

		// The file is imported, so get rid of the importer.
		importer->Destroy();

		// do NOT use FbxSystemUnit::ConvertScene(lScene), which just simply set transform.scale of root nodes.
		if (scene->GetGlobalSettings().GetSystemUnit() == FbxSystemUnit::mm)
			fileScale = 0.001f;
		else if (scene->GetGlobalSettings().GetSystemUnit() == FbxSystemUnit::dm)
			fileScale = 0.1f;
		else if (scene->GetGlobalSettings().GetSystemUnit() == FbxSystemUnit::m)
			fileScale = 1.0f;
		else if (scene->GetGlobalSettings().GetSystemUnit() == FbxSystemUnit::cm)
			fileScale = 0.01f;
		else
		{
			Warning("Unsupport file scale");
			fileScale = 1.0f;
		}

		// Convert mesh, NURBS and patch into triangle mesh
		FbxGeometryConverter geomConverter(sdkManager);
		geomConverter.Triangulate(scene, /*replace*/true);
		// Split meshes per material, so that we only have one material per mesh.
		// However, this method will fail sometimes due to the FBK SDK issues. So
		// we still need to manage multi material in one mesh.
		// geomConverter.SplitMeshesPerMaterial(scene, /*replace*/true);
		
		// Read data
		FbxNode* rootNode = scene->GetRootNode();
		if(rootNode)
		{
			BakeTRS(rootNode);
			ProcessSkeletonHierarchy(rootNode);
			ProcessSkeletonElimination(rootNode);
			LOG(INFO) << StringPrintf("Read joint number %d", Skeleton.size());



		}

		
		// Destroy the SDK manager and all the other objects it was handling.
		sdkManager->Destroy();

		
		
		return true;
	}

	int FbxLoader::FindJoint(std::string name)
	{
		for (size_t i = 0; i < Skeleton.size(); ++i)
		{
			if (Skeleton[i].Name == name)
				return i;
		}
		return -1;
	}

	void FbxLoader::BakeTRS(FbxNode* rootNode)
	{
		if (!rootNode)
			return;

		// Do this setup for each node (FbxNode).
		// We set up what we want to bake via ConvertPivotAnimationRecursive.
		// When the destination is set to 0, baking will occur.
		// When the destination value is set to the sources value, the source values will be retained and not baked.
		BakeConfigure(rootNode);

		// When the setup is done, call ConvertPivotAnimationRecursive to the scenes root node.
		// Sampling rate e.g. 30.0.
		rootNode->ConvertPivotAnimationRecursive(nullptr, FbxNode::eDestinationPivot, 24.0);
	}

	void FbxLoader::BakeConfigure(FbxNode* node)
	{
		if (!node)
			return;
		
		FbxVector4 zero(0, 0, 0);

		// Activate pivot converting
		node->SetPivotState(FbxNode::eSourcePivot, FbxNode::ePivotActive);
		node->SetPivotState(FbxNode::eDestinationPivot, FbxNode::ePivotActive);

		// We want to set all these to 0 and bake them into the transforms.
		node->SetPostRotation(FbxNode::eDestinationPivot, zero);
		node->SetPreRotation(FbxNode::eDestinationPivot, zero);
		node->SetRotationOffset(FbxNode::eDestinationPivot, zero);
		node->SetScalingOffset(FbxNode::eDestinationPivot, zero);
		node->SetRotationPivot(FbxNode::eDestinationPivot, zero);
		node->SetScalingPivot(FbxNode::eDestinationPivot, zero);

		// This is to import in a system that supports rotation order.
		// If rotation order is not supported, do this instead:
		// pNode->SetRotationOrder(FbxNode::eDESTINATION_SET , FbxNode::eEULER_XYZ);
		// FbxEuler::EOrder rotationOrder;
		// node->GetRotationOrder(FbxNode::eSourcePivot, rotationOrder);
		// node->SetRotationOrder(FbxNode::eDestinationPivot, rotationOrder);
		node->SetRotationOrder(FbxNode::eDestinationPivot, FbxEuler::eOrderXYZ);

		// Similarly, this is the case where geometric transforms are supported by the system.
		// If geometric transforms are not supported, set them to zero instead of
		// the sources geometric transforms.
		// Geometric transform = local transform, not inherited by children.
		node->SetGeometricTranslation(FbxNode::eDestinationPivot, zero);
		node->SetGeometricRotation(FbxNode::eDestinationPivot, zero);
		node->SetGeometricScaling(FbxNode::eDestinationPivot, zero);

		// Idem for quaternions.
		node->SetQuaternionInterpolation(FbxNode::eDestinationPivot, node->GetQuaternionInterpolation(FbxNode::eSourcePivot));

		for (int i = 0; i < node->GetChildCount(); i++)
			BakeConfigure(node->GetChild(i));
	}

	void FbxLoader::ProcessSkeletonHierarchy(FbxNode* rootNode)
	{
		for (int childIndex = 0; childIndex < rootNode->GetChildCount(); ++childIndex)
		{
			FbxNode* currNode = rootNode->GetChild(childIndex);
			ProcessSkeletonHierarchyRecursively(currNode, 0, -1);
		}
	}

	void FbxLoader::ProcessSkeletonHierarchyRecursively(FbxNode* node, int myIndex, int inParentIndex)
	{
		if (node->GetNodeAttribute() && node->GetNodeAttribute()->GetAttributeType() && node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			Joint currJoint;
			currJoint.Parent = inParentIndex;
			currJoint.Name = node->GetName();
			Skeleton.push_back(currJoint);
		}
		for (int i = 0; i < node->GetChildCount(); i++)
		{
			ProcessSkeletonHierarchyRecursively(node->GetChild(i), Skeleton.size(), myIndex);
		}
	}

	void FbxLoader::ProcessSkeletonElimination(FbxNode* rootNode)
	{
		for (int childIndex = 0; childIndex < rootNode->GetChildCount(); ++childIndex)
		{
			FbxNode* currNode = rootNode->GetChild(childIndex);
			ProcessSkeletonEliminationRecursively(currNode);
		}

		std::vector<Joint> temp;
		for (auto& item : Skeleton)
		{
			if(item.Parent >= 0 && !Skeleton[item.Parent].Valid)
				item.Valid = false;

			if (item.Valid)
				temp.push_back(item);
		}
		Skeleton.clear();
		Skeleton = temp;
	}

	void FbxLoader::ProcessSkeletonEliminationRecursively(FbxNode* node)
	{
		if (node->GetNodeAttribute() && node->GetNodeAttribute()->GetAttributeType() && node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			FbxMesh* mesh = node->GetMesh();
			int deformerNum = mesh->GetDeformerCount();
			for (int deformerIndex = 0; deformerIndex < deformerNum; ++deformerIndex)
			{
				// Only use skin deformer
				FbxSkin* skin = reinterpret_cast<FbxSkin*>(mesh->GetDeformer(deformerIndex, FbxDeformer::eSkin));
				if (!skin)
				{
					continue;
				}

				int clusterNum = skin->GetClusterCount();
				for (int clusterIndex = 0; clusterIndex < clusterNum; ++clusterIndex)
				{
					FbxCluster* cluster = skin->GetCluster(clusterIndex);
					std::string jointName = cluster->GetLink()->GetName();
					int jointIndex = FindJoint(jointName);
					if (jointIndex < 0)
					{
						Warning("Joint name not found in skeleton in mesh %s", node->GetName());
						continue;
					}
					Skeleton[jointIndex].Valid = true;
				}
			}
		}
		for (int i = 0; i < node->GetChildCount(); i++)
		{
			ProcessSkeletonEliminationRecursively(node->GetChild(i));
		}
	}



}	// namespace handwork