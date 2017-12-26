// Fbx file importer.

#include "fbxloader.h"
#include "fbxsdk.h"
#include "../utility/transform.h"

namespace handwork
{
	// Struct declaration
	struct JointInfo
	{
		JointInfo() : Valid(false)
		{}

		std::string Name;
		int Parent;
		Matrix4x4 GlobalBindposeInverse;
		FbxCluster* Cluster;
		bool Valid;
		Vector3f Translation;
		Vector3f Scaling;
		Vector3f Rotation;
	};

	struct MeshVI
	{
		std::vector<MeshVertex> Vertices;
		std::vector<int> Indices;
	};

	// Helper methods
	// FbxSDK use row vector, our system use column vector, so do transpose.
	Matrix4x4 ConvertToMatrix4X4(const FbxAMatrix& tf)
	{
		auto tranforms = Matrix4x4(
			(float)tf[0][0], (float)tf[1][0], (float)tf[2][0], (float)tf[3][0],
			(float)tf[0][1], (float)tf[1][1], (float)tf[2][1], (float)tf[3][1],
			(float)tf[0][2], (float)tf[1][2], (float)tf[2][2], (float)tf[3][2],
			(float)tf[0][3], (float)tf[1][3], (float)tf[2][3], (float)tf[3][3]);
		return tranforms;
	}

	Vector3f ConvertToVector3f(const FbxDouble3& v)
	{
		return Vector3f((float)v[0], (float)v[1], (float)v[2]);
	}

	int FindJoint(const std::string& name, const std::vector<JointInfo>& skeletonInfo)
	{
		for (size_t i = 0; i < skeletonInfo.size(); ++i)
		{
			if (skeletonInfo[i].Name == name)
				return i;
		}
		return -1;
	}

	// Method pre-declaration 
	void BakeTRS(FbxNode* rootNode);
	void BakeConfigure(FbxNode* node);
	void ProcessSkeletonHierarchyRecursively(FbxNode* node, int myIndex, int inParentIndex, std::vector<JointInfo>& skeletonInfo);
	void ProcessSkeletonEliminationRecursively(FbxNode* node, std::vector<JointInfo>& skeletonInfo);
	void ProcessNode(FbxNode* node, std::vector<JointInfo>& skeletonInfo, std::vector<MeshVI*>& meshVICache);
	void ProcessMesh(FbxNode* node, std::vector<JointInfo>& skeletonInfo, std::vector<MeshVI*>& meshVICache);
	void ProcessJoints(FbxNode* node, std::vector<MeshVertex>& vertices, std::vector<JointInfo>& skeletonInfo);
	void PackVI(std::vector<MeshVI*>& meshVICache, std::vector<MeshVertex>& meshVertices, std::vector<int>& meshIndices);
	void ReadPosition(FbxMesh* mesh, std::vector<MeshVertex>& vertices, const Transform& world);
	void ReadIndex(FbxMesh* mesh, std::vector<int>& indices);
	void ReadNormal(FbxMesh* mesh, std::vector<MeshVertex>& vertices, const Transform& world, bool reGenerate = true);
	void ReadTangent(FbxMesh* mesh, std::vector<MeshVertex>& vertices, const Transform& world, bool reGenerate = true);

	bool ImportFbx(const std::string& filename, float& fileScale, std::vector<MeshJoint>& skeleton,
		std::vector<MeshVertex>& meshVertices, std::vector<int>& meshIndices)
	{
		return ImportFbx(filename.c_str(), fileScale, skeleton, meshVertices, meshIndices);
	}

	bool ImportFbx(const char* filename, float& fileScale, std::vector<MeshJoint>& skeleton,
		std::vector<MeshVertex>& meshVertices, std::vector<int>& meshIndices)
	{
		skeleton.clear();
		meshVertices.clear();
		meshIndices.clear();

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
		if(!rootNode)
		{
			Error("Invalid fbx file: %s", filename);
			sdkManager->Destroy();
			return false;
		}

		// Bake fbx data.
		BakeTRS(rootNode);

		// Process skeleton hierarchy.
		std::vector<JointInfo> skeletonInfo;
		for (int childIndex = 0; childIndex < rootNode->GetChildCount(); ++childIndex)
		{
			FbxNode* currNode = rootNode->GetChild(childIndex);
			ProcessSkeletonHierarchyRecursively(currNode, 0, -1, skeletonInfo);
		}

		// Eliminate skeleton.
		for (int childIndex = 0; childIndex < rootNode->GetChildCount(); ++childIndex)
		{
			FbxNode* currNode = rootNode->GetChild(childIndex);
			ProcessSkeletonEliminationRecursively(currNode, skeletonInfo);
		}
		std::vector<int> newPos(skeletonInfo.size(), -1);
		std::vector<JointInfo> temp;
		for (int i = 0; i < (int)skeletonInfo.size(); ++i)
		{
			auto& item = skeletonInfo[i];
			if (item.Parent >= 0 && !skeletonInfo[item.Parent].Valid)
				item.Valid = false;
			if (!item.Valid)
				continue;

			newPos[i] = temp.size();
			if (item.Parent < 0)
				temp.push_back(item);
			else
			{
				item.Parent = newPos[item.Parent];
				CHECK_GE(item.Parent, 0);
				temp.push_back(item);
			}
		}
		skeletonInfo.clear();
		skeletonInfo = temp;

		// Get joint translation, rotation and scale.
		for(auto& item : skeletonInfo)
		{
			FbxNode* linkNode = item.Cluster->GetLink();
			item.Translation = ConvertToVector3f(linkNode->LclTranslation.Get());
			item.Rotation = ConvertToVector3f(linkNode->LclRotation.Get());
			item.Scaling = ConvertToVector3f(linkNode->LclScaling.Get());
		}

		LOG(INFO) << StringPrintf("Read joint number %d", skeletonInfo.size());

		// Process nodes
		std::vector<MeshVI*> meshVICache;
		for (int i = 0; i < rootNode->GetChildCount(); i++)
			ProcessNode(rootNode->GetChild(i), skeletonInfo, meshVICache);
		
		// Destroy the SDK manager and all the other objects it was handling.
		sdkManager->Destroy();

		// Pack mesh vertices and indices.
		PackVI(meshVICache, meshVertices, meshIndices);

		// Free memory
		for (int i = 0; i < (int)meshVICache.size(); ++i)
			delete meshVICache[i];
		meshVICache.clear();

		// Pack skeleton data
		skeleton.resize(skeletonInfo.size());
		for (int i = 0; i < (int)skeletonInfo.size(); ++i)
		{
			skeleton[i].Name = skeletonInfo[i].Name;
			skeleton[i].Parent = skeletonInfo[i].Parent;
			skeleton[i].GlobalBindposeInverse = skeletonInfo[i].GlobalBindposeInverse;
			skeleton[i].Translation = skeletonInfo[i].Translation;
			skeleton[i].Rotation = skeletonInfo[i].Rotation;
			skeleton[i].Scaling = skeletonInfo[i].Scaling;
		}
		
		return true;
	}


	void BakeTRS(FbxNode* rootNode)
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

	void BakeConfigure(FbxNode* node)
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

	void ProcessSkeletonHierarchyRecursively(FbxNode* node, int myIndex, int inParentIndex, std::vector<JointInfo>& skeletonInfo)
	{
		if (node->GetNodeAttribute() && node->GetNodeAttribute()->GetAttributeType() && node->GetNodeAttribute()->GetAttributeType() == FbxNodeAttribute::eSkeleton)
		{
			JointInfo currJoint;
			currJoint.Parent = inParentIndex;
			currJoint.Name = node->GetName();
			skeletonInfo.push_back(currJoint);
		}
		for (int i = 0; i < node->GetChildCount(); i++)
		{
			ProcessSkeletonHierarchyRecursively(node->GetChild(i), skeletonInfo.size(), myIndex, skeletonInfo);
		}
	}

	void ProcessSkeletonEliminationRecursively(FbxNode* node, std::vector<JointInfo>& skeletonInfo)
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
					continue;

				int clusterNum = skin->GetClusterCount();
				for (int clusterIndex = 0; clusterIndex < clusterNum; ++clusterIndex)
				{
					FbxCluster* cluster = skin->GetCluster(clusterIndex);
					std::string jointName = cluster->GetLink()->GetName();
					int jointIndex = FindJoint(jointName, skeletonInfo);
					if (jointIndex < 0)
					{
						Warning("JointInfo name not found in skeleton in mesh %s", node->GetName());
						continue;
					}
					skeletonInfo[jointIndex].Cluster = cluster;
					skeletonInfo[jointIndex].Valid = true;
				}
			}
		}
		for (int i = 0; i < node->GetChildCount(); i++)
		{
			ProcessSkeletonEliminationRecursively(node->GetChild(i), skeletonInfo);
		}
	}

	void ProcessNode(FbxNode* node, std::vector<JointInfo>& skeletonInfo, std::vector<MeshVI*>& meshVICache)
	{
		if (node->GetNodeAttribute())
		{
			switch (node->GetNodeAttribute()->GetAttributeType())
			{
			case FbxNodeAttribute::eMesh:
				ProcessMesh(node, skeletonInfo, meshVICache);
				break;
			default:
				break;
			}
		}

		for (int i = 0; i < node->GetChildCount(); ++i)
		{
			ProcessNode(node->GetChild(i), skeletonInfo, meshVICache);
		}
	}

	void ProcessMesh(FbxNode* node, std::vector<JointInfo>& skeletonInfo, std::vector<MeshVI*>& meshVICache)
	{
		FbxMesh* mesh = node->GetMesh();
		if (mesh == nullptr)
			return;

		int controlPointsCount = mesh->GetControlPointsCount();
		int triangleCount = mesh->GetPolygonCount();
		if (triangleCount == 0 || controlPointsCount == 0)
			return;
		
		// Get the world matrix.
		auto tf = node->EvaluateGlobalTransform(FbxTime(0.0f), FbxNode::eDestinationPivot);
		Transform world(ConvertToMatrix4X4(tf));

		// Load material
		MeshVI* currentVI = new MeshVI();
		auto& vertices = currentVI->Vertices;
		auto& indices = currentVI->Indices;
		vertices.resize(controlPointsCount);
		indices.reserve(triangleCount * 3);

		// Read positions, indices, normals and tangents
		ReadPosition(mesh, vertices, world);
		ReadIndex(mesh, indices);
		ReadNormal(mesh, vertices, world, true);
		ReadTangent(mesh, vertices, world, true);
		
		// Process joint information
		ProcessJoints(node, vertices, skeletonInfo);

		meshVICache.push_back(currentVI);
	}

	void ProcessJoints(FbxNode* node, std::vector<MeshVertex>& vertices, std::vector<JointInfo>& skeletonInfo)
	{
		FbxMesh* mesh = node->GetMesh();
		int deformerNum = mesh->GetDeformerCount();

		for (int deformerIndex = 0; deformerIndex < deformerNum; ++deformerIndex)
		{
			// Only use skin deformer
			FbxSkin* skin = reinterpret_cast<FbxSkin*>(mesh->GetDeformer(deformerIndex, FbxDeformer::eSkin));
			if (!skin)
				continue;

			int clusterNum = skin->GetClusterCount();
			for (int clusterIndex = 0; clusterIndex < clusterNum; ++clusterIndex)
			{
				FbxCluster* cluster = skin->GetCluster(clusterIndex);
				std::string jointName = cluster->GetLink()->GetName();
				int jointIndex = FindJoint(jointName, skeletonInfo);
				if (jointIndex < 0)
				{
					Warning("Valid joint name not found in skeleton for mesh %s", node->GetName());
					continue;
				}
				if (!skeletonInfo[jointIndex].Valid)
					continue;

				FbxAMatrix transformMatrix;
				FbxAMatrix transformLinkMatrix;
				FbxAMatrix globalBindposeInverseMatrix;

				//transformMatrix = node->EvaluateGlobalTransform();
				//transformLinkMatrix = cluster->GetLink()->EvaluateGlobalTransform();

				cluster->GetTransformMatrix(transformMatrix);	// The transformation of the mesh at binding time
				cluster->GetTransformLinkMatrix(transformLinkMatrix);	// The transformation of the cluster(joint) at binding time from joint space to world space
				globalBindposeInverseMatrix = transformMatrix * transformLinkMatrix.Inverse();

				// Update the information in mSkeleton 
				skeletonInfo[jointIndex].GlobalBindposeInverse = ConvertToMatrix4X4(globalBindposeInverseMatrix);

				// Associate each joint with the control points it affects
				int indexNum = cluster->GetControlPointIndicesCount();
				double* weights = cluster->GetControlPointWeights();
				int* indices = cluster->GetControlPointIndices();
				for (int i = 0; i < indexNum; ++i)
				{
					MeshBlendPair blendPair;
					blendPair.Index = jointIndex;
					blendPair.Weight = (float)weights[i];
					vertices[indices[i]].BlendInfo.push_back(blendPair);
				}
			}
		}
	}

	void PackVI(std::vector<MeshVI*>& meshVICache, std::vector<MeshVertex>& meshVertices, std::vector<int>& meshIndices)
	{
		int meshNum = (int)meshVICache.size();
		if (meshNum == 0)
			return;

		// Process single subset MeshVI
		int offset = 0;
		for (int i = 0; i < meshNum; ++i)
		{
			auto& currentVertices = meshVICache[i]->Vertices;
			auto& currentIndices = meshVICache[i]->Indices;

			meshVertices.insert(meshVertices.end(), currentVertices.begin(), currentVertices.end());
			std::transform(currentIndices.begin(), currentIndices.end(), currentIndices.begin(),
				[offset](int a) { return a + offset; });
			meshIndices.insert(meshIndices.end(), currentIndices.begin(), currentIndices.end());
			
			offset += currentVertices.size();
		}
	}

	void ReadPosition(FbxMesh* mesh, std::vector<MeshVertex>& vertices, const Transform& world)
	{
		FbxVector4* pCtrlPoint = mesh->GetControlPoints();
		int controlPointsCount = mesh->GetControlPointsCount();

		for (int i = 0; i < controlPointsCount; ++i)
		{
			vertices[i].Position.x = (float)pCtrlPoint[i][0];
			vertices[i].Position.y = (float)pCtrlPoint[i][1];
			vertices[i].Position.z = (float)pCtrlPoint[i][2];
			vertices[i].Position = world(vertices[i].Position, VectorType::Point);
		}
	}

	void ReadIndex(FbxMesh* mesh, std::vector<int>& indices)
	{
		int triangleCount = mesh->GetPolygonCount();

		for (int i = 0; i < triangleCount; ++i)
			for (int j = 0; j < 3; j++)
			{
				int ctrlPointIndex = mesh->GetPolygonVertex(i, j);
				indices.push_back(ctrlPointIndex);
			}
	}

	void ReadNormal(FbxMesh* mesh, std::vector<MeshVertex>& vertices, const Transform& world, bool reGenerate)
	{
		if (mesh->GetElementNormalCount() < 1)
		{
			Warning("Lack Normal in mesh %s", mesh->GetName());
			if (!reGenerate)
				return;
			if (!mesh->GenerateNormals())
			{
				Warning("Regenerate normal failed for mesh %s", mesh->GetName());
				return;
			}
		}

		FbxGeometryElementNormal* leNormal = mesh->GetElementNormal(0);
		int controlPointsCount = mesh->GetControlPointsCount();
		int triangleCount = mesh->GetPolygonCount();
		int vertexCounter = 0;

		switch (leNormal->GetMappingMode())
		{
		case FbxGeometryElement::eByControlPoint:
			switch (leNormal->GetReferenceMode())
			{
			case FbxGeometryElement::eDirect:
				for (int i = 0; i < controlPointsCount; ++i)
				{
					vertices[i].Normal.x = (float)leNormal->GetDirectArray()[i][0];
					vertices[i].Normal.y = (float)leNormal->GetDirectArray()[i][1];
					vertices[i].Normal.z = (float)leNormal->GetDirectArray()[i][2];
					vertices[i].Normal = world(vertices[i].Normal, VectorType::Normal);
				}
				break;
			case FbxGeometryElement::eIndexToDirect:
				for (int i = 0; i < controlPointsCount; ++i)
				{
					int id = leNormal->GetIndexArray()[i];
					vertices[i].Normal.x = (float)leNormal->GetDirectArray()[id][0];
					vertices[i].Normal.y = (float)leNormal->GetDirectArray()[id][1];
					vertices[i].Normal.z = (float)leNormal->GetDirectArray()[id][2];
					vertices[i].Normal = world(vertices[i].Normal, VectorType::Normal);
				}
				break;
			default:
				Warning("Unsupport normal reference mode for mesh %s", mesh->GetName());
			}
			break;

		case FbxGeometryElement::eByPolygonVertex:
		{
			switch (leNormal->GetReferenceMode())
			{
			case FbxGeometryElement::eDirect:
				for (int i = 0; i < triangleCount; ++i)
					for (int j = 0; j < 3; j++)
					{
						int ctrlPointIndex = mesh->GetPolygonVertex(i, j);
						vertices[ctrlPointIndex].Normal.x = (float)leNormal->GetDirectArray()[vertexCounter][0];
						vertices[ctrlPointIndex].Normal.y = (float)leNormal->GetDirectArray()[vertexCounter][1];
						vertices[ctrlPointIndex].Normal.z = (float)leNormal->GetDirectArray()[vertexCounter][2];
						vertices[ctrlPointIndex].Normal = world(vertices[ctrlPointIndex].Normal, VectorType::Normal);
						++vertexCounter;
					}
				break;
			case FbxGeometryElement::eIndexToDirect:
				for (int i = 0; i < triangleCount; ++i)
					for (int j = 0; j < 3; j++)
					{
						int ctrlPointIndex = mesh->GetPolygonVertex(i, j);
						int id = leNormal->GetIndexArray()[vertexCounter];
						vertices[ctrlPointIndex].Normal.x = (float)leNormal->GetDirectArray()[id][0];
						vertices[ctrlPointIndex].Normal.y = (float)leNormal->GetDirectArray()[id][1];
						vertices[ctrlPointIndex].Normal.z = (float)leNormal->GetDirectArray()[id][2];
						vertices[ctrlPointIndex].Normal = world(vertices[ctrlPointIndex].Normal, VectorType::Normal);
						++vertexCounter;
					}
				break;
			default:
				Warning("Unsupport normal reference mode for mesh %s", mesh->GetName());
			}
		}
		break;

		default:
			Warning("Unsupport normal mapping mode for mesh %s", mesh->GetName());
		}
	}

	void ReadTangent(FbxMesh* mesh, std::vector<MeshVertex>& vertices, const Transform& world, bool reGenerate)
	{
		if (mesh->GetElementTangentCount() < 1)
		{
			Warning("Lack Tangent in mesh %s", mesh->GetName());
			if (!reGenerate)
				return;
			if (!mesh->GenerateTangentsDataForAllUVSets())
			{
				Warning("Regenerate tangent failed for mesh %s", mesh->GetName());
				return;
			}
		}

		FbxGeometryElementTangent* leTangent = mesh->GetElementTangent(0);
		int controlPointsCount = mesh->GetControlPointsCount();
		int triangleCount = mesh->GetPolygonCount();
		int vertexCounter = 0;

		switch (leTangent->GetMappingMode())
		{
		case FbxGeometryElement::eByControlPoint:
			switch (leTangent->GetReferenceMode())
			{
			case FbxGeometryElement::eDirect:
				for (int i = 0; i < controlPointsCount; ++i)
				{
					vertices[i].Tangent.x = (float)leTangent->GetDirectArray()[i][0];
					vertices[i].Tangent.y = (float)leTangent->GetDirectArray()[i][1];
					vertices[i].Tangent.z = (float)leTangent->GetDirectArray()[i][2];
					vertices[i].Tangent = world(vertices[i].Tangent, VectorType::Vector);
				}
				break;
			case FbxGeometryElement::eIndexToDirect:
				for (int i = 0; i < controlPointsCount; ++i)
				{
					int id = leTangent->GetIndexArray()[i];
					vertices[i].Tangent.x = (float)leTangent->GetDirectArray()[id][0];
					vertices[i].Tangent.y = (float)leTangent->GetDirectArray()[id][1];
					vertices[i].Tangent.z = (float)leTangent->GetDirectArray()[id][2];
					vertices[i].Tangent = world(vertices[i].Tangent, VectorType::Vector);
				}
				break;
			default:
				Warning("Unsupport tangent reference mode for mesh %s", mesh->GetName());
			}
			break;

		case FbxGeometryElement::eByPolygonVertex:
			switch (leTangent->GetReferenceMode())
			{
			case FbxGeometryElement::eDirect:
				for (int i = 0; i < triangleCount; ++i)
					for (int j = 0; j < 3; j++)
					{
						int ctrlPointIndex = mesh->GetPolygonVertex(i, j);
						vertices[ctrlPointIndex].Tangent.x = (float)leTangent->GetDirectArray()[vertexCounter][0];
						vertices[ctrlPointIndex].Tangent.y = (float)leTangent->GetDirectArray()[vertexCounter][1];
						vertices[ctrlPointIndex].Tangent.z = (float)leTangent->GetDirectArray()[vertexCounter][2];
						vertices[ctrlPointIndex].Tangent = world(vertices[ctrlPointIndex].Tangent, VectorType::Vector);
						++vertexCounter;
					}
				break;
			case FbxGeometryElement::eIndexToDirect:
				for (int i = 0; i < triangleCount; ++i)
					for (int j = 0; j < 3; j++)
					{
						int ctrlPointIndex = mesh->GetPolygonVertex(i, j);
						int id = leTangent->GetIndexArray()[vertexCounter];
						vertices[ctrlPointIndex].Tangent.x = (float)leTangent->GetDirectArray()[id][0];
						vertices[ctrlPointIndex].Tangent.y = (float)leTangent->GetDirectArray()[id][1];
						vertices[ctrlPointIndex].Tangent.z = (float)leTangent->GetDirectArray()[id][2];
						vertices[ctrlPointIndex].Tangent = world(vertices[ctrlPointIndex].Tangent, VectorType::Vector);
						++vertexCounter;
					}
				break;
			default:
				Warning("Unsupport tangent reference mode for mesh %s", mesh->GetName());
			}
			break;

		default:
			Warning("Unsupport tangent mapping mode for mesh %s", mesh->GetName());
		}
	}

}	// namespace handwork