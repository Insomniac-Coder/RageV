#include <rvpch.h>
#include "MeshCook.h"
#include "RageV/Core/Log.h"

#include <cstring>

namespace RageV::Assets
{
	namespace
	{
		constexpr char kMagic[4] = { 'R', 'V', 'M', 'S' };
		// 2: a material's blend mode. **Bumping this is the whole reason a
		// version is here**: a cached `.rvmesh` from version 1 carries no blend
		// mode, so a re-import of a model with glass in it silently answered
		// "opaque" out of the cache and the importer's new code never ran. The
		// symptom was a windscreen that stayed solid however many times the
		// model was re-imported, which points at the importer and not at a file
		// beside it.
		constexpr uint32_t kVersion = 2;

		// --- writing --------------------------------------------------------

		template <typename T>
		void Put(std::vector<uint8_t>& out, const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			const uint8_t* bytes = (const uint8_t*)&value;
			out.insert(out.end(), bytes, bytes + sizeof(T));
		}

		void PutString(std::vector<uint8_t>& out, const std::string& value)
		{
			Put(out, (uint32_t)value.size());
			out.insert(out.end(), value.begin(), value.end());
		}

		template <typename T>
		void PutVector(std::vector<uint8_t>& out, const std::vector<T>& values)
		{
			static_assert(std::is_trivially_copyable_v<T>);
			Put(out, (uint64_t)values.size());
			const uint8_t* bytes = (const uint8_t*)values.data();
			out.insert(out.end(), bytes, bytes + values.size() * sizeof(T));
		}

		template <typename T>
		void PutChannel(std::vector<uint8_t>& out, const Anim::Channel<T>& channel)
		{
			PutVector(out, channel.Times);
			PutVector(out, channel.Values);
		}

		// --- reading --------------------------------------------------------
		// Every read is bounds-checked against the buffer's end: a truncated
		// or corrupt file must answer false, not read past its bytes.

		struct Reader
		{
			const uint8_t* Cursor;
			const uint8_t* End;
			bool Ok = true;

			template <typename T>
			bool Get(T& out)
			{
				static_assert(std::is_trivially_copyable_v<T>);
				if (!Ok || (size_t)(End - Cursor) < sizeof(T))
					return Ok = false;
				std::memcpy(&out, Cursor, sizeof(T));
				Cursor += sizeof(T);
				return true;
			}

			bool GetString(std::string& out)
			{
				uint32_t length = 0;
				if (!Get(length) || (size_t)(End - Cursor) < length)
					return Ok = false;
				out.assign((const char*)Cursor, length);
				Cursor += length;
				return true;
			}

			template <typename T>
			bool GetVector(std::vector<T>& out)
			{
				static_assert(std::is_trivially_copyable_v<T>);
				uint64_t count = 0;
				if (!Get(count) || (size_t)(End - Cursor) < count * sizeof(T))
					return Ok = false;
				out.resize((size_t)count);
				std::memcpy(out.data(), Cursor, (size_t)count * sizeof(T));
				Cursor += count * sizeof(T);
				return true;
			}

			template <typename T>
			bool GetChannel(Anim::Channel<T>& out)
			{
				return GetVector(out.Times) && GetVector(out.Values);
			}
		};
	}

	std::vector<uint8_t> MeshCook::Serialize(const ImportedModel& model)
	{
		std::vector<uint8_t> out;
		out.insert(out.end(), kMagic, kMagic + 4);
		Put(out, kVersion);

		PutString(out, model.Name);

		Put(out, (uint32_t)model.Primitives.size());
		for (const ImportedPrimitive& primitive : model.Primitives)
		{
			PutString(out, primitive.Name);
			PutVector(out, primitive.Vertices);
			PutVector(out, primitive.Indices);
			Put(out, (int32_t)primitive.Material);
			PutVector(out, primitive.Joints);
			PutVector(out, primitive.Weights);
		}

		Put(out, (uint32_t)model.Materials.size());
		for (const ImportedMaterial& material : model.Materials)
		{
			PutString(out, material.Name);
			Put(out, material.Params);
			Put(out, (int32_t)material.Blend);
			Put(out, (int32_t)material.BaseColorTexture);
			Put(out, (int32_t)material.NormalTexture);
			Put(out, (int32_t)material.MetallicRoughnessTexture);
			Put(out, (int32_t)material.OcclusionTexture);
			Put(out, (int32_t)material.EmissiveTexture);
		}

		Put(out, (uint32_t)model.Textures.size());
		for (const ImportedTexture& texture : model.Textures)
		{
			PutString(out, texture.Path);
			Put(out, (uint8_t)(texture.SRGB ? 1 : 0));
		}

		Put(out, (uint32_t)model.Nodes.size());
		for (const ImportedNode& node : model.Nodes)
		{
			PutString(out, node.Name);
			Put(out, node.Position);
			Put(out, node.Rotation);
			Put(out, node.Scale);
			Put(out, (int32_t)node.Parent);
			PutVector(out, node.Primitives);
		}

		Put(out, (uint32_t)model.Skeleton.Bones.size());
		for (const Bone& bone : model.Skeleton.Bones)
		{
			PutString(out, bone.Name);
			Put(out, (int32_t)bone.Parent);
			Put(out, bone.InverseBind);
			Put(out, bone.RestPosition);
			Put(out, bone.RestRotation);
			Put(out, bone.RestScale);
		}

		Put(out, (uint32_t)model.Clips.size());
		for (const Anim::Clip& clip : model.Clips)
		{
			PutString(out, clip.Name);
			Put(out, clip.Duration);
			Put(out, (uint32_t)clip.Tracks.size());
			for (const Anim::BoneTrack& track : clip.Tracks)
			{
				PutChannel(out, track.Position);
				PutChannel(out, track.Rotation);
				PutChannel(out, track.Scale);
			}
		}

		return out;
	}

	bool MeshCook::IsCooked(const uint8_t* bytes, size_t size)
	{
		return bytes && size >= 4 && std::memcmp(bytes, kMagic, 4) == 0;
	}

	bool MeshCook::Deserialize(ImportedModel& out, const uint8_t* bytes, size_t size)
	{
		if (!IsCooked(bytes, size))
			return false;

		Reader reader{ bytes + 4, bytes + size };

		uint32_t version = 0;
		if (!reader.Get(version) || version != kVersion)
		{
			// A warning, not an error: since the importers fall through to the
			// source on a refusal this is a handled condition, and the one it
			// is handling is an ordinary version bump. It was an error, and a
			// red line for something the engine recovers from perfectly is how
			// a real failure stops being noticed.
			RV_CORE_WARN("Cooked mesh is version {0}; this engine reads {1}",
						 version, kVersion);
			return false;
		}

		ImportedModel model;
		reader.GetString(model.Name);

		uint32_t count = 0;
		reader.Get(count);
		model.Primitives.resize(reader.Ok ? count : 0);
		for (ImportedPrimitive& primitive : model.Primitives)
		{
			reader.GetString(primitive.Name);
			reader.GetVector(primitive.Vertices);
			reader.GetVector(primitive.Indices);
			int32_t material = -1;
			reader.Get(material);
			primitive.Material = material;
			reader.GetVector(primitive.Joints);
			reader.GetVector(primitive.Weights);
		}

		reader.Get(count);
		model.Materials.resize(reader.Ok ? count : 0);
		for (ImportedMaterial& material : model.Materials)
		{
			reader.GetString(material.Name);
			reader.Get(material.Params);

			int32_t blend = 0;
			reader.Get(blend);
			material.Blend = blend == (int32_t)BlendMode::Blend ? BlendMode::Blend
															   : BlendMode::Opaque;

			int32_t indices[5] = { -1, -1, -1, -1, -1 };
			for (int32_t& index : indices)
				reader.Get(index);
			material.BaseColorTexture = indices[0];
			material.NormalTexture = indices[1];
			material.MetallicRoughnessTexture = indices[2];
			material.OcclusionTexture = indices[3];
			material.EmissiveTexture = indices[4];
		}

		reader.Get(count);
		model.Textures.resize(reader.Ok ? count : 0);
		for (ImportedTexture& texture : model.Textures)
		{
			reader.GetString(texture.Path);
			uint8_t srgb = 1;
			reader.Get(srgb);
			texture.SRGB = srgb != 0;
		}

		reader.Get(count);
		model.Nodes.resize(reader.Ok ? count : 0);
		for (ImportedNode& node : model.Nodes)
		{
			reader.GetString(node.Name);
			reader.Get(node.Position);
			reader.Get(node.Rotation);
			reader.Get(node.Scale);
			int32_t parent = -1;
			reader.Get(parent);
			node.Parent = parent;
			reader.GetVector(node.Primitives);
		}

		reader.Get(count);
		model.Skeleton.Bones.resize(reader.Ok ? count : 0);
		for (Bone& bone : model.Skeleton.Bones)
		{
			reader.GetString(bone.Name);
			int32_t parent = -1;
			reader.Get(parent);
			bone.Parent = parent;
			reader.Get(bone.InverseBind);
			reader.Get(bone.RestPosition);
			reader.Get(bone.RestRotation);
			reader.Get(bone.RestScale);
		}

		reader.Get(count);
		model.Clips.resize(reader.Ok ? count : 0);
		for (Anim::Clip& clip : model.Clips)
		{
			reader.GetString(clip.Name);
			reader.Get(clip.Duration);
			uint32_t trackCount = 0;
			reader.Get(trackCount);
			clip.Tracks.resize(reader.Ok ? trackCount : 0);
			for (Anim::BoneTrack& track : clip.Tracks)
			{
				reader.GetChannel(track.Position);
				reader.GetChannel(track.Rotation);
				reader.GetChannel(track.Scale);
			}
		}

		if (!reader.Ok)
		{
			RV_CORE_ERROR("Cooked mesh ends before its data does");
			return false;
		}

		out = std::move(model);
		return true;
	}
}
