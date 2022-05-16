#pragma once
#include <string>
#include <memory>

namespace RageV
{

	class Texture
	{

	public:
		virtual ~Texture() {}
		virtual void Bind(unsigned int slot = 0) const = 0;
		virtual const unsigned int& GetWidth() const = 0;
		virtual const unsigned int& GetHeight() const = 0;
		virtual void SetData(void* data, const unsigned int& size) = 0;
	};

	class Texture2D : public Texture
	{
	public:	
		static std::shared_ptr<Texture2D> Create(const unsigned int& width, const unsigned int& heigth);
		static std::shared_ptr<Texture2D> Create(const std::string& path);
	};

}