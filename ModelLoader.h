#pragma once
#include <string>
#include <memory>


class Model;

class ModelLoader
{
public:
	static std::unique_ptr<Model> LoadOBJ(const std::string& filepath);
};

