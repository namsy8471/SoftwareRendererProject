#pragma once
class Texture
{
private:
	unsigned char* pixels = nullptr;
	int width = 0;
	int height = 0;

public:
	Texture();
	~Texture();
};

