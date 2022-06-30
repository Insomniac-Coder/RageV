#include "rvpch.h"
#include "Chunk.h"
#include <iostream>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "PerlinNoise.hpp"

namespace RageV
{
	int Chunk::CHUNK_DIMENSION = 64;
	int Chunk::CHUNK_HEIGHT = 64;
	int Chunk::BASE = 32;

	Chunk::Chunk(float xCoord, float zCoord, int seed_1, int seed_2, int seed_3)
	{
		const siv::PerlinNoise::seed_type seed1 = seed_1;
		const siv::PerlinNoise::seed_type seed2 = seed_2;
		const siv::PerlinNoise::seed_type seed3 = seed_3;

		const siv::PerlinNoise perlin1{ seed1 };
		const siv::PerlinNoise perlin2{ seed2 };
		const siv::PerlinNoise perlin3{ seed3 };

		m_BlockData.resize(CHUNK_DIMENSION * CHUNK_DIMENSION);
		unsigned int counter = 0;
		for (; counter < m_BlockData.size();)
		{
			int x = counter % CHUNK_DIMENSION;
			int z = counter / CHUNK_DIMENSION;
			//int y = (int)(RageV::perlin2D(xCoord + (x / (float)CHUNK_DIMENSION), zCoord + (z / (float)CHUNK_DIMENSION)) * (float)CHUNK_HEIGHT);
			int y = int(perlin1.octave2D_11(xCoord + (x / (float)CHUNK_DIMENSION), zCoord + (z / (float)CHUNK_DIMENSION), 4) * perlin2.octave2D_11(xCoord + (x / (float)CHUNK_DIMENSION), zCoord + (z / (float)CHUNK_DIMENSION), 4) * (CHUNK_HEIGHT - BASE)) + BASE;
			m_BlockData[counter] = (y > 0) ? y : 1;
			counter++;
		}

		for (int z = 0; z < CHUNK_DIMENSION; z++)
		{
			for (int x = 0; x < CHUNK_DIMENSION; x++)
			{
				for (int y = 0; y < m_BlockData[(z * CHUNK_DIMENSION) + x]; y++)
				{
					//front check
					int frontZ = z + 1;
					if (frontZ < CHUNK_DIMENSION)
					{
						if (m_BlockData[(frontZ * CHUNK_DIMENSION) + x] - y - 1 < 0)
						{
							m_Points.push_back({ float(x + (xCoord * CHUNK_DIMENSION)), (float)y, float(z + (zCoord * CHUNK_DIMENSION) + 0.5), FaceType::FRONT});
						}
					}
					else
					{
						m_Points.push_back({ float(x + (xCoord * CHUNK_DIMENSION)), (float)y, float(z + (zCoord * CHUNK_DIMENSION) + 0.5), FaceType::FRONT });
					}
					//back check
					int backZ = z - 1;
					if (backZ >= 0)
					{
						if (m_BlockData[(backZ * CHUNK_DIMENSION) + x] - y - 1 < 0)
						{
							m_Points.push_back({ float(x + (xCoord * CHUNK_DIMENSION)), (float)y, float(z + (zCoord * CHUNK_DIMENSION) - 0.5), FaceType::BACK });
						}
					}
					else
					{
						m_Points.push_back({ float(x + (xCoord * CHUNK_DIMENSION)), (float)y, float(z + (zCoord * CHUNK_DIMENSION) - 0.5), FaceType::BACK });
					}
					//left check
					int leftX = x - 1;
					if (leftX >= 0)
					{
						m_Points.push_back({ float(x + (xCoord * CHUNK_DIMENSION) - 0.5), (float)y, float(z + (zCoord * CHUNK_DIMENSION)), FaceType::LEFT });
					}
					else
					{
						m_Points.push_back({ float(x + (xCoord * CHUNK_DIMENSION) - 0.5), (float)y, float(z + (zCoord * CHUNK_DIMENSION)), FaceType::LEFT });
					}

					//right check
					int rightX = x + 1;
					if (rightX < CHUNK_DIMENSION)
					{
						m_Points.push_back({ float(x + (xCoord * CHUNK_DIMENSION) + 0.5), (float)y, float(z + (zCoord * CHUNK_DIMENSION)), FaceType::TOP });
					}
					else
					{
						m_Points.push_back({ float(x + (xCoord * CHUNK_DIMENSION) + 0.5), (float)y, float(z + (zCoord * CHUNK_DIMENSION)), FaceType::TOP });
					}

					//top and bottom check
					if (y == 0)
					{
						m_Points.push_back({ float(x + (xCoord * CHUNK_DIMENSION)), (float)y + (float)0.5, float(z + (zCoord * CHUNK_DIMENSION)), FaceType::BOTTOM });
					}
					if (y == m_BlockData[(z * CHUNK_DIMENSION) + x] - 1)
					{
						m_Points.push_back({ float(x + (xCoord * CHUNK_DIMENSION)), (float)y - (float)0.5, float(z + (zCoord * CHUNK_DIMENSION)), FaceType::BOTTOM });
					}
				}
			}
		}

	}
}