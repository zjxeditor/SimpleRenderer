#include "utility/utility.h"
#include "mesh/fbxloader.h"
#include "mesh/meshtopology.h"
#include <iostream>
#include "utility/stringprint.h"

using namespace handwork;
using namespace std;

void main(int argc, char* argv[])
{
	FLAGS_log_dir = "./";
	// Initialize Google's logging library.
	google::InitGoogleLogging(argv[0]);

	string file = "C:\\Users\\Jx\\Desktop\\hand.fbx";
	float fileScale = 0.0f;
	vector<MeshJoint> skeleton;
	vector<MeshVertex> vertices;
	vector<int> indices;
	bool flag = ImportFbx(file, fileScale, skeleton, vertices, indices);

	vector<Vector3f> positions(vertices.size());
	std::transform(vertices.begin(), vertices.end(), positions.begin(), [](MeshVertex& a) {return a.Position; });
	MeshTopology topology(indices.size(), &indices[0], positions.size(), &positions[0]);

	/*SDVertex* sdv = topology.GetVertices();
	for (int i = 0; i < (int)positions.size(); ++i)
	{
		cout << StringPrintf("Vertex %d valence %d", i, sdv[i].valence()) << endl;
	}*/

	for (int i = 0; i < (int)vertices.size(); ++i)
	{
		cout << StringPrintf("Vertex %d blend %d", i, vertices[i].BlendInfo.size()) << endl;;
	}

}