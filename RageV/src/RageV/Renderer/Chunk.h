#pragma once
#include <vector>	

namespace RageV
{
	enum FaceType
	{
		LEFT,
		RIGHT,
		TOP,
		BOTTOM,
		FRONT,
		BACK
	};
	struct surfaceData
	{
		float x;
		float y;
		float z;
		FaceType faceType;
	};
	class Chunk {
	public:
		Chunk(float xCoord, float zCoord, int seed_1, int seed_2, int seed_3);
		~Chunk() = default;
		const std::vector<surfaceData>& GetData() { return m_Points; }
	private:
		std::vector<surfaceData> m_Points;
		std::vector<float> m_BlockData;
	public:
		static int CHUNK_DIMENSION;
		static int CHUNK_HEIGHT;
		static int BASE;
	};
}