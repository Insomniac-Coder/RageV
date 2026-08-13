#include <rvpch.h>
#include "GltfImporter.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/VFS.h"
#include "RageV/Math/Math.h"
#include <cgltf.h>

namespace RageV::Assets
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
				Mat4 matrix;
				std::memcpy(&matrix[0][0], node.matrix, sizeof(float) * 16);

				Quat rotation;
				if (Math::Decompose(matrix, out.Position, rotation, out.Scale))
					out.Rotation = Math::ToEuler(rotation);
				return;
			}

			if (node.has_translation)
				out.Position = { node.translation[0], node.translation[1], node.translation[2] };

			if (node.has_rotation)
			{
				// glTF stores quaternions xyzw; Quat's constructor takes wxyz.
				const Quat rotation(node.rotation[3], node.rotation[0],
										 node.rotation[1], node.rotation[2]);
				out.Rotation = Math::ToEuler(rotation);
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

				// No flag for the packed texture: there is no packed slot to
				// flag any more. AssetManager::InstantiateModel splits it into
				// a roughness map and a metallic map, and the flags are set
				// there, when the two halves have handles and are real assets.
				if (out.MetallicRoughnessTexture >= 0)
					out.Params.MapFlags |= MaterialMap_Roughness | MaterialMap_Metallic;
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


		// --- skinning ---------------------------------------------------------
		// glTF's joints array is in whatever order the exporter wrote it, and
		// Skeleton requires parents before children so composition can be one
		// forward pass. So the joints are reordered here and everything that
		// refers to them -- the vertices' JOINTS_0, the animation channels --
		// is remapped through the same table.
		//
		// Doing it at import rather than at load means the runtime never sees
		// the arbitrary order, and the invariant is established once by code
		// that can fail loudly instead of being hoped for.
		struct SkinMapping
		{
			// glTF joint index to skeleton bone index.
			std::vector<int> ToBone;
			// cgltf node pointer to glTF joint index.
			std::unordered_map<const cgltf_node*, int> JointOf;
		};

		bool ReadSkin(const cgltf_skin& skin, Skeleton& out, SkinMapping& mapping)
		{
			const size_t count = skin.joints_count;
			if (count == 0)
				return false;

			mapping.JointOf.clear();
			for (size_t i = 0; i < count; i++)
				mapping.JointOf[skin.joints[i]] = (int)i;

			// Each joint's parent, as a glTF joint index. A joint whose node
			// parent is not itself a joint is a root of this skeleton -- which
			// is the normal case for the topmost bone, whose parent is the
			// armature node.
			std::vector<int> parentOf(count, -1);
			for (size_t i = 0; i < count; i++)
			{
				const cgltf_node* parent = skin.joints[i]->parent;
				const auto found = parent ? mapping.JointOf.find(parent) : mapping.JointOf.end();
				parentOf[i] = found != mapping.JointOf.end() ? found->second : -1;
			}

			// Depth first from the roots, so a parent is always emitted before
			// its children. A cycle would be a malformed file; the visited set
			// makes that a dropped bone rather than a hang.
			mapping.ToBone.assign(count, -1);
			std::vector<int> order;
			order.reserve(count);

			std::vector<std::vector<int>> childrenOf(count);
			for (size_t i = 0; i < count; i++)
			{
				if (parentOf[i] >= 0)
					childrenOf[parentOf[i]].push_back((int)i);
			}

			std::vector<int> stack;
			for (size_t i = 0; i < count; i++)
			{
				if (parentOf[i] < 0)
					stack.push_back((int)i);
			}
			// Reversed, so the first root is processed first once popped.
			std::reverse(stack.begin(), stack.end());

			while (!stack.empty())
			{
				const int joint = stack.back();
				stack.pop_back();

				if (mapping.ToBone[joint] >= 0)
					continue;

				mapping.ToBone[joint] = (int)order.size();
				order.push_back(joint);

				for (auto child = childrenOf[joint].rbegin();
					 child != childrenOf[joint].rend(); ++child)
				{
					stack.push_back(*child);
				}
			}

			if (order.size() != count)
			{
				RV_CORE_WARN("Skin '{0}' has {1} joints but only {2} are reachable from a "
							 "root; the rest are dropped",
							 skin.name ? skin.name : "(unnamed)", count, order.size());
			}

			out.Bones.clear();
			out.Bones.resize(order.size());

			for (size_t bone = 0; bone < order.size(); bone++)
			{
				const int joint = order[bone];
				const cgltf_node* node = skin.joints[joint];

				Bone& target = out.Bones[bone];
				target.Name = node->name ? node->name : ("bone" + std::to_string(bone));
				target.Parent = parentOf[joint] >= 0 ? mapping.ToBone[parentOf[joint]] : -1;

				// The node's own rest transform, which is what a bone the clip
				// does not animate holds.
				if (node->has_matrix)
				{
					// A node given as a matrix has to be taken apart, because
					// the pose is stored as three components.
					Mat4 matrix;
					memcpy(&matrix[0][0], node->matrix, sizeof(float) * 16);

					Quat rotation;
					if (Math::Decompose(matrix, target.RestPosition, rotation, target.RestScale))
					{
						target.RestRotation = rotation;
					}
				}
				else
				{
					if (node->has_translation)
						target.RestPosition = { node->translation[0], node->translation[1],
												node->translation[2] };
					if (node->has_rotation)
					{
						// glTF stores xyzw; Quat's constructor takes wxyz.
						target.RestRotation = Quat(node->rotation[3], node->rotation[0],
														node->rotation[1], node->rotation[2]);
					}
					if (node->has_scale)
						target.RestScale = { node->scale[0], node->scale[1], node->scale[2] };
				}

				// The inverse bind matrix is optional; the identity is the
				// documented default and means the bind pose is mesh space.
				if (skin.inverse_bind_matrices)
				{
					float values[16] = {};
					if (cgltf_accessor_read_float(skin.inverse_bind_matrices, joint, values, 16))
						memcpy(&target.InverseBind[0][0], values, sizeof(values));
				}
			}

			if (!out.IsWellOrdered())
			{
				RV_CORE_ERROR("Skin '{0}' could not be ordered parents-first",
							  skin.name ? skin.name : "(unnamed)");
				return false;
			}

			return true;
		}

		void ReadAnimation(const cgltf_animation& source, const SkinMapping& mapping,
						   size_t boneCount, Anim::Clip& out)
		{
			out.Name = source.name ? source.name : "Clip";
			out.Tracks.assign(boneCount, BoneTrack{});

			for (cgltf_size c = 0; c < source.channels_count; c++)
			{
				const cgltf_animation_channel& channel = source.channels[c];
				if (!channel.target_node || !channel.sampler)
					continue;

				const auto joint = mapping.JointOf.find(channel.target_node);
				if (joint == mapping.JointOf.end())
					continue;   // animates something that is not a bone of this skin

				const int bone = mapping.ToBone[joint->second];
				if (bone < 0 || bone >= (int)boneCount)
					continue;

				const cgltf_accessor* input = channel.sampler->input;
				const cgltf_accessor* output = channel.sampler->output;
				if (!input || !output)
					continue;

				const cgltf_size keys = std::min(input->count, output->count);
				BoneTrack& track = out.Tracks[bone];

				// STEP and CUBICSPLINE are read as if they were linear rather
				// than refused. A clip that plays slightly wrong is a better
				// failure than a character that does not move, and the two are
				// rare enough that guessing is the right trade until somebody
				// hits it.
				if (channel.sampler->interpolation != cgltf_interpolation_type_linear)
				{
					RV_CORE_WARN("Animation '{0}' uses a non-linear interpolation; "
								 "sampling it linearly", out.Name);
				}

				for (cgltf_size k = 0; k < keys; k++)
				{
					float time = 0.0f;
					cgltf_accessor_read_float(input, k, &time, 1);

					float value[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

					switch (channel.target_path)
					{
						case cgltf_animation_path_type_translation:
							cgltf_accessor_read_float(output, k, value, 3);
							track.Position.Times.push_back(time);
							track.Position.Values.push_back({ value[0], value[1], value[2] });
							break;

						case cgltf_animation_path_type_rotation:
							cgltf_accessor_read_float(output, k, value, 4);
							// xyzw on disk, wxyz in the constructor.
							track.Rotation.Times.push_back(time);
							track.Rotation.Values.push_back(
								Quat(value[3], value[0], value[1], value[2]));
							break;

						case cgltf_animation_path_type_scale:
							cgltf_accessor_read_float(output, k, value, 3);
							track.Scale.Times.push_back(time);
							track.Scale.Values.push_back({ value[0], value[1], value[2] });
							break;

						default:
							// Morph target weights, which nothing here supports.
							break;
					}
				}
			}

			out.RecomputeDuration();
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
			const cgltf_accessor* joints = nullptr;
			const cgltf_accessor* weights = nullptr;

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
					// One influence set, so four bones a vertex. glTF allows
					// more as JOINTS_1 and beyond; four is what almost every
					// exporter emits and what the vertex format carries.
					case cgltf_attribute_type_joints:
						if (attribute.index == 0)
							joints = attribute.data;
						break;
					case cgltf_attribute_type_weights:
						if (attribute.index == 0)
							weights = attribute.data;
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

			// Skinning, if the primitive has any. Both attributes or neither:
			// joints without weights would give every vertex to bone zero and
			// weights without joints have nothing to address.
			if (joints && weights)
			{
				out.Joints.resize(positions->count);
				out.Weights.resize(positions->count);

				for (cgltf_size i = 0; i < positions->count; i++)
				{
					cgltf_uint indices[4] = { 0, 0, 0, 0 };
					cgltf_accessor_read_uint(joints, i, indices, 4);
					out.Joints[i] = { indices[0], indices[1], indices[2], indices[3] };

					float influence[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					cgltf_accessor_read_float(weights, i, influence, 4);

					// Normalised here rather than trusted. The spec requires
					// the four to sum to one and exporters round; a sum of 0.98
					// darkens a whole limb by two percent, which reads as bad
					// lighting rather than as bad weights.
					const float sum = influence[0] + influence[1] + influence[2] + influence[3];
					const float scale = sum > 1e-6f ? 1.0f / sum : 0.0f;

					out.Weights[i] = { influence[0] * scale, influence[1] * scale,
									   influence[2] * scale, influence[3] * scale };

					// A vertex with no influence at all would collapse to the
					// origin once skinned. Giving it entirely to the first bone
					// leaves it somewhere, which is recoverable.
					if (scale == 0.0f)
						out.Weights[i].x = 1.0f;
				}
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

					const Vec3 normal = Math::Normalize(
						Math::Cross(b.Position - a.Position, c.Position - a.Position));

					a.Normal += normal;
					b.Normal += normal;
					c.Normal += normal;
				}

				for (MeshVertex& vertex : out.Vertices)
				{
					vertex.Normal = Math::Dot(vertex.Normal, vertex.Normal) > 0.0f
								  ? Math::Normalize(vertex.Normal)
								  : Vec3(0.0f, 1.0f, 0.0f);
				}
			}

			out.Material = IndexOf(source.material, data.materials, sizeof(cgltf_material));
			return true;
		}

		// cgltf reads the .gltf itself, its external .bin, and everything a
		// URI names through this one callback -- so a model in a pak imports
		// exactly the way a loose one does, sibling files included.
		cgltf_result VfsFileRead(const cgltf_memory_options* memory,
								 const cgltf_file_options*, const char* path,
								 cgltf_size* size, void** data)
		{
			std::vector<uint8_t> bytes;
			if (!IO::VFS::ReadBytes(path, bytes))
				return cgltf_result_file_not_found;

			// Allocated the way cgltf frees: through its own memory options,
			// because cgltf_free releases every buffer this handed over.
			void* copy = memory->alloc_func
					   ? memory->alloc_func(memory->user_data, bytes.size())
					   : malloc(bytes.size());
			if (!copy)
				return cgltf_result_out_of_memory;

			std::memcpy(copy, bytes.data(), bytes.size());
			*data = copy;
			if (size)
				*size = bytes.size();

			return cgltf_result_success;
		}

		void VfsFileRelease(const cgltf_memory_options* memory,
							const cgltf_file_options*, void* data)
		{
			if (memory->free_func)
				memory->free_func(memory->user_data, data);
			else
				free(data);
		}
	}

	bool GltfImporter::Import(const std::filesystem::path& path, ImportedModel& out)
	{
		const std::string filename = path.string();

		cgltf_options options = {};
		options.file.read = VfsFileRead;
		options.file.release = VfsFileRelease;
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

		// The skin, before the primitives, because reading a primitive's
		// JOINTS_0 needs the mapping the skin produces.
		//
		// The first skin only. A file with two independent characters in it is
		// a file that should have been two, and supporting it would put a
		// skeleton reference on every primitive for a case nobody exports.
		SkinMapping mapping;
		if (data->skins_count > 0)
		{
			if (data->skins_count > 1)
			{
				RV_CORE_WARN("'{0}' has {1} skins; importing the first",
							 filename, data->skins_count);
			}

			if (!ReadSkin(data->skins[0], out.Skeleton, mapping))
				out.Skeleton.Bones.clear();
		}

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

				// JOINTS_0 addresses glTF's joint order and the skeleton is in
				// its own. Remapped here, once, so nothing downstream has to
				// know the two ever differed.
				if (primitive.IsSkinned() && !mapping.ToBone.empty())
				{
					for (UVec4& joint : primitive.Joints)
					{
						for (int component = 0; component < 4; component++)
						{
							const uint32_t index = joint[component];
							const int bone = index < mapping.ToBone.size()
										   ? mapping.ToBone[index] : -1;
							joint[component] = bone >= 0 ? (uint32_t)bone : 0u;
						}
					}
				}
				else if (primitive.IsSkinned())
				{
					// Skinning data with no skin to address. Dropped rather
					// than kept: the indices point into a skeleton that is not
					// there.
					primitive.Joints.clear();
					primitive.Weights.clear();
				}

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

		// Animations last: they address bones, which the skin above defined.
		if (!out.Skeleton.IsEmpty())
		{
			for (cgltf_size i = 0; i < data->animations_count; i++)
			{
				Anim::Clip clip;
				ReadAnimation(data->animations[i], mapping, out.Skeleton.Size(), clip);

				// A clip animating nothing this skeleton owns is not worth
				// keeping; it would appear in the inspector as a choice that
				// does nothing.
				bool animates = false;
				for (const BoneTrack& track : clip.Tracks)
					animates = animates || !track.IsEmpty();

				if (animates)
					out.Clips.push_back(std::move(clip));
			}
		}
		else if (data->animations_count > 0)
		{
			RV_CORE_WARN("'{0}' has animations but no skin; they are not imported", filename);
		}

		cgltf_free(data);

		RV_CORE_INFO("Imported '{0}': {1} primitives, {2} materials, {3} textures, {4} nodes",
					 filename, out.Primitives.size(), out.Materials.size(),
					 out.Textures.size(), out.Nodes.size());

		if (!out.Skeleton.IsEmpty())
		{
			RV_CORE_INFO("  and a skeleton of {0} bones with {1} clips",
						 out.Skeleton.Size(), out.Clips.size());
		}

		return true;
	}
}
