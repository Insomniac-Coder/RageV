#include <rvpch.h>
#include "GltfImporter.h"
#include "RageV/Core/Log.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cgltf.h>

namespace RageV
{
	namespace
	{
		const char* ResultName(cgltf_result result)
		{
			switch (result)
			{
				case cgltf_result_success:         return "success";
				case cgltf_result_data_too_short:  return "data too short";
				case cgltf_result_unknown_format:  return "unknown format";
				case cgltf_result_invalid_json:    return "invalid JSON";
				case cgltf_result_invalid_gltf:    return "invalid glTF";
				case cgltf_result_out_of_memory:   return "out of memory";
				case cgltf_result_file_not_found:  return "file not found";
				case cgltf_result_io_error:        return "I/O error";
				case cgltf_result_legacy_gltf:     return "glTF 1.0 is not supported";
				default:                           return "unknown error";
			}
		}

		int IndexOf(const void* element, const void* array, size_t stride)
		{
			if (!element || !array)
				return -1;
			return (int)(((const char*)element - (const char*)array) / stride);
		}

		// glTF stores a node's transform either as a matrix or as separate
		// translation/rotation/scale. cgltf can hand back the composed matrix
		// for both, and the engine stores euler angles, so it is decomposed
		// once here rather than every frame.
		void ReadNodeTransform(const cgltf_node& node, ImportedNode& out)
		{
			if (node.has_matrix)
			{
				glm::mat4 matrix;
				std::memcpy(&matrix[0][0], node.matrix, sizeof(float) * 16);

				glm::vec3 skew;
				glm::vec4 perspective;
				glm::quat rotation;
				if (glm::decompose(matrix, out.Scale, rotation, out.Position, skew, perspective))
					out.Rotation = glm::eulerAngles(rotation);
				return;
			}

			if (node.has_translation)
				out.Position = { node.translation[0], node.translation[1], node.translation[2] };

			if (node.has_rotation)
			{
				// glTF stores quaternions xyzw; glm's constructor takes wxyz.
				const glm::quat rotation(node.rotation[3], node.rotation[0],
										 node.rotation[1], node.rotation[2]);
				out.Rotation = glm::eulerAngles(rotation);
			}

			if (node.has_scale)
				out.Scale = { node.scale[0], node.scale[1], node.scale[2] };
		}

		int AddTexture(ImportedModel& model, const cgltf_texture_view& view, bool srgb)
		{
			if (!view.texture || !view.texture->image || !view.texture->image->uri)
				return -1;

			ImportedTexture texture;
			texture.Path = view.texture->image->uri;
			texture.SRGB = srgb;

			// Deduplicated: a base colour and an emissive map are commonly the
			// same file, and importing it twice would cost two GPU textures.
			for (size_t i = 0; i < model.Textures.size(); i++)
			{
				if (model.Textures[i].Path == texture.Path && model.Textures[i].SRGB == srgb)
					return (int)i;
			}

			model.Textures.push_back(std::move(texture));
			return (int)model.Textures.size() - 1;
		}

		void ReadMaterial(ImportedModel& model, const cgltf_material& source, ImportedMaterial& out)
		{
			out.Name = source.name ? source.name : "Material";

			if (source.has_pbr_metallic_roughness)
			{
				const auto& pbr = source.pbr_metallic_roughness;

				out.Params.BaseColor = { pbr.base_color_factor[0], pbr.base_color_factor[1],
										 pbr.base_color_factor[2], pbr.base_color_factor[3] };
				out.Params.Metallic = pbr.metallic_factor;
				out.Params.Roughness = pbr.roughness_factor;

				out.BaseColorTexture = AddTexture(model, pbr.base_color_texture, true);
				// Metallic-roughness is data, not colour: sampling it through
				// sRGB would bend both channels.
				out.MetallicRoughnessTexture = AddTexture(model, pbr.metallic_roughness_texture, false);

				if (out.BaseColorTexture >= 0)
					out.Params.MapFlags |= MaterialMap_BaseColor;
				if (out.MetallicRoughnessTexture >= 0)
					out.Params.MapFlags |= MaterialMap_MetallicRoughness;
			}

			out.Params.EmissiveColor = { source.emissive_factor[0], source.emissive_factor[1],
										 source.emissive_factor[2], 1.0f };
			out.Params.NormalScale = source.normal_texture.scale != 0.0f
								   ? source.normal_texture.scale : 1.0f;

			out.NormalTexture = AddTexture(model, source.normal_texture, false);
			out.OcclusionTexture = AddTexture(model, source.occlusion_texture, false);
			out.EmissiveTexture = AddTexture(model, source.emissive_texture, true);

			if (out.NormalTexture >= 0)    out.Params.MapFlags |= MaterialMap_Normal;
			if (out.OcclusionTexture >= 0) out.Params.MapFlags |= MaterialMap_Occlusion;
			if (out.EmissiveTexture >= 0)  out.Params.MapFlags |= MaterialMap_Emissive;
		}

		bool ReadPrimitive(const cgltf_data& data, const cgltf_primitive& source,
						   const std::string& name, ImportedPrimitive& out)
		{
			// Only triangles. Points, lines and strips would each need their own
			// pipeline topology, and no exporter emits them for solid geometry.
			if (source.type != cgltf_primitive_type_triangles)
				return false;

			const cgltf_accessor* positions = nullptr;
			const cgltf_accessor* normals = nullptr;
			const cgltf_accessor* texcoords = nullptr;

			for (cgltf_size i = 0; i < source.attributes_count; i++)
			{
				const cgltf_attribute& attribute = source.attributes[i];
				switch (attribute.type)
				{
					case cgltf_attribute_type_position: positions = attribute.data; break;
					case cgltf_attribute_type_normal:   normals = attribute.data; break;
					case cgltf_attribute_type_texcoord:
						// TEXCOORD_0 only; the shader samples one set.
						if (attribute.index == 0)
							texcoords = attribute.data;
						break;
					default: break;
				}
			}

			if (!positions)
				return false;

			out.Name = name;
			out.Vertices.resize(positions->count);

			for (cgltf_size i = 0; i < positions->count; i++)
			{
				MeshVertex& vertex = out.Vertices[i];

				float value[3] = { 0.0f, 0.0f, 0.0f };
				cgltf_accessor_read_float(positions, i, value, 3);
				vertex.Position = { value[0], value[1], value[2] };

				// A model with no normals would render black under any light.
				// Facet normals are generated below when this is the case.
				if (normals && cgltf_accessor_read_float(normals, i, value, 3))
					vertex.Normal = { value[0], value[1], value[2] };
				else
					vertex.Normal = { 0.0f, 0.0f, 0.0f };

				float uv[2] = { 0.0f, 0.0f };
				if (texcoords)
					cgltf_accessor_read_float(texcoords, i, uv, 2);
				vertex.TexCoord = { uv[0], uv[1] };
			}

			if (source.indices)
			{
				out.Indices.resize(source.indices->count);
				for (cgltf_size i = 0; i < source.indices->count; i++)
					out.Indices[i] = (uint32_t)cgltf_accessor_read_index(source.indices, i);
			}
			else
			{
				// Non-indexed geometry is legal; the renderer always draws
				// indexed, so a trivial index buffer is generated.
				out.Indices.resize(out.Vertices.size());
				for (size_t i = 0; i < out.Indices.size(); i++)
					out.Indices[i] = (uint32_t)i;
			}

			if (!normals)
			{
				for (size_t i = 0; i + 2 < out.Indices.size(); i += 3)
				{
					MeshVertex& a = out.Vertices[out.Indices[i]];
					MeshVertex& b = out.Vertices[out.Indices[i + 1]];
					MeshVertex& c = out.Vertices[out.Indices[i + 2]];

					const glm::vec3 normal = glm::normalize(
						glm::cross(b.Position - a.Position, c.Position - a.Position));

					a.Normal += normal;
					b.Normal += normal;
					c.Normal += normal;
				}

				for (MeshVertex& vertex : out.Vertices)
				{
					vertex.Normal = glm::dot(vertex.Normal, vertex.Normal) > 0.0f
								  ? glm::normalize(vertex.Normal)
								  : glm::vec3(0.0f, 1.0f, 0.0f);
				}
			}

			out.Material = IndexOf(source.material, data.materials, sizeof(cgltf_material));
			return true;
		}
	}

	bool GltfImporter::Import(const std::filesystem::path& path, ImportedModel& out)
	{
		const std::string filename = path.string();

		cgltf_options options = {};
		cgltf_data* data = nullptr;

		cgltf_result result = cgltf_parse_file(&options, filename.c_str(), &data);
		if (result != cgltf_result_success)
		{
			RV_CORE_ERROR("glTF parse failed for '{0}': {1}", filename, ResultName(result));
			return false;
		}

		// Resolves external .bin files, base64 data URIs and the GLB chunk.
		result = cgltf_load_buffers(&options, data, filename.c_str());
		if (result != cgltf_result_success)
		{
			RV_CORE_ERROR("glTF buffers failed for '{0}': {1}", filename, ResultName(result));
			cgltf_free(data);
			return false;
		}

		if (cgltf_validate(data) != cgltf_result_success)
			RV_CORE_WARN("'{0}' does not validate; importing anyway", filename);

		out = ImportedModel{};
		out.Name = path.stem().string();

		out.Materials.resize(data->materials_count);
		for (cgltf_size i = 0; i < data->materials_count; i++)
			ReadMaterial(out, data->materials[i], out.Materials[i]);

		// glTF meshes hold primitives; the importer flattens them and remembers
		// which primitives each mesh contributed, so nodes can point at them.
		std::vector<std::vector<int>> primitivesByMesh(data->meshes_count);

		for (cgltf_size m = 0; m < data->meshes_count; m++)
		{
			const cgltf_mesh& mesh = data->meshes[m];
			const std::string meshName = mesh.name ? mesh.name : out.Name;

			for (cgltf_size p = 0; p < mesh.primitives_count; p++)
			{
				ImportedPrimitive primitive;
				const std::string name = mesh.primitives_count > 1
									   ? meshName + "." + std::to_string(p)
									   : meshName;

				if (!ReadPrimitive(*data, mesh.primitives[p], name, primitive))
					continue;

				primitivesByMesh[m].push_back((int)out.Primitives.size());
				out.Primitives.push_back(std::move(primitive));
			}
		}

		// Walked depth first from the roots so a parent always precedes its
		// children, which is what lets the scene build the tree in one pass.
		std::vector<std::pair<const cgltf_node*, int>> pending;

		const cgltf_scene* scene = data->scene ? data->scene
							   : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
		if (scene)
		{
			for (cgltf_size i = 0; i < scene->nodes_count; i++)
				pending.emplace_back(scene->nodes[i], -1);
		}
		else
		{
			// A file with no scene is unusual but legal; take every root node.
			for (cgltf_size i = 0; i < data->nodes_count; i++)
			{
				if (!data->nodes[i].parent)
					pending.emplace_back(&data->nodes[i], -1);
			}
		}

		std::reverse(pending.begin(), pending.end());

		while (!pending.empty())
		{
			const auto [source, parent] = pending.back();
			pending.pop_back();

			ImportedNode node;
			node.Name = source->name ? source->name : "Node";
			node.Parent = parent;
			ReadNodeTransform(*source, node);

			if (source->mesh)
			{
				const int index = IndexOf(source->mesh, data->meshes, sizeof(cgltf_mesh));
				if (index >= 0 && index < (int)primitivesByMesh.size())
					node.Primitives = primitivesByMesh[index];
			}

			const int self = (int)out.Nodes.size();
			out.Nodes.push_back(std::move(node));

			for (cgltf_size i = source->children_count; i-- > 0; )
				pending.emplace_back(source->children[i], self);
		}

		cgltf_free(data);

		RV_CORE_INFO("Imported '{0}': {1} primitives, {2} materials, {3} textures, {4} nodes",
					 filename, out.Primitives.size(), out.Materials.size(),
					 out.Textures.size(), out.Nodes.size());
		return true;
	}
}
