#include "utility/utility.h"
#include "mesh/fbxloader.h"

using namespace handwork;
using namespace std;

void main(int argc, char* argv[])
{
	FLAGS_log_dir = "./";
	// Initialize Google's logging library.
	google::InitGoogleLogging(argv[0]);

	string file = "C:\\Users\\Jx\\Desktop\\hand.fbx";
	float fileScale = 0.0f;
	vector<Joint> skeleton;
	vector<Vertex> vertices;
	vector<int> indices;
	
	bool flag = ImportFbx(file, fileScale, skeleton, vertices, indices);
}