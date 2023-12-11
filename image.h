#ifndef _IMAGE_H_
#define _IMAGE_H_

#include <stdint.h>

#include <string>

#define GLM_FORCE_RADIANS
#include "dependencies/glm/glm.hpp"
#include "dependencies/glm/ext.hpp"

#include "texture.h"

class Image {
public:
	Image() {
		m_Width = 0;
		m_Height = 0;
		m_Channels = 0;
		m_Data = nullptr;
	};
	Image(std::string filePath);
	Image(uint32_t width, uint32_t height, uint32_t channels);
	~Image();

	Texture& GetTexture();
	void CreateTexture();

	const glm::ivec3 operator()(uint32_t x, uint32_t y) {
		unsigned char* pixel = m_Data + ((y * m_Width + x) * m_Channels);
		return glm::ivec3(pixel[0], pixel[1], pixel[2]);		
	}

	static Image Blend(Image& a, Image& b, float pct);

	uint32_t		m_Width, m_Height;
	uint32_t		m_Channels;
	unsigned char * m_Data;
	bool			m_DataFromFile;
	std::string		m_FilePath;
private:
	Texture			m_Texture;
};

#endif
