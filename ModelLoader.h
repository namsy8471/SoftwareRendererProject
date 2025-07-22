#pragma once
#include <string>

class Model;
class ModelLoader
{
public:
	static bool LoadOBJ(const std::string& filepath, Model& outModel);
};

