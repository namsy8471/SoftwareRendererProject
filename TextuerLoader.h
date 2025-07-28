#pragma once
#include <string>
#include <memory>

struct StbiImageDeleter
{
	void operator()(unsigned char* p) const;
};

using StbiImagePtr = std::unique_ptr<unsigned char, StbiImageDeleter>;


class TextuerLoader
{
public:
	static StbiImagePtr LoadImageFile(const std::string& filepath, int& width, int& height);
};

