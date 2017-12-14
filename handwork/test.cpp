//#include "mesh/fbxloader.h"
//
//using namespace handwork;
//using namespace std;
//
//void main()
//{
//	auto a = new FbxLoader();
//	string file = "C:\\Users\\Jx\\Desktop\\hand.fbx";
//
//	a->ImportFile(file.c_str());
//
//
//}

#include <iostream>
#include "glog/logging.h"
#include "utility/stringprint.h"

void main(int argc, char* argv[])
{
	
	FLAGS_log_dir = "./";
	// Initialize Google's logging library.
	google::InitGoogleLogging(argv[0]);

	//// ...
	//LOG(INFO) << "Found " << 5 << " cookies";
	//LOG(INFO) << "Found " << 5 << " cookies";
	//LOG(INFO) << "Found " << 5 << " cookies";
	//LOG(INFO) << "Found " << 5 << " cookies";

}