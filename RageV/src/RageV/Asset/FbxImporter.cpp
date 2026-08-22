#include <rvpch.h>
#include "FbxImporter.h"
#include "ImportCache.h"
#include "MeshCook.h"
#include "RageV/Core/Log.h"
#include "RageV/IO/VFS.h"

#include "ufbx.h"

#include <algorithm>
#include <unordered_map>

namespace RageV::Assets
{
	namespace
	{
		Vec3 ToVec3(const ufbx_vec3& v)
		{
			return Vec3((float)v.x, (float)v.y, (float)v.z);
		}

		Quat ToQuat(const ufbx_quat& q)
		{
			// ufbx stores xyzw; Quat's constructor takes wxyz. The glTF
			// importer says the same thing about the same mistake.
			return Quat((float)q.w, (float)q.x, (float)q.y, (float)q.z);
		}

		Mat4 ToMat4(const ufbx_matrix& m)
		{
			// ufbx keeps an affine transform as four *columns* of three, which
			// is the order Mat4 already uses -- so this fills columns and
			// supplies the fourth row, and is not a transpose. Getting that
			// backwards produces a bind matrix that is wrong only where the
			// rotation is, which is invisible on a fixture with no rotation in
			// it.
			Mat4 out(1.0f);
			for (int column = 0; column < 4; column++)
			{
				out[column] = Vec4((float)m.cols[column].x,
								   (float)m.cols[column].y,
								   (float)m.cols[column].z,
								   column == 3 ? 1.0f : 0.0f);
			}
			return out;
		}

		// --- texture paths ----------------------------------------------------
		//
		// An FBX records the absolute path the artist had -- `C:\Users\someone
		// \textures\brick.png` -- and usually a relative one beside it. The
		// absolute one is a path into a computer that is not this one and is
		// never followed.
		//
		// The rule is the engine's usual one: relative to the model file, and
		// failing that the file's own name in the model's folder, which is what
		// survives an artist who kept textures next to the mesh and a build that
		// flattened them.
		std::string ResolveTexturePath(const std::filesystem::path& modelDirectory,
									   const ufbx_texture* texture)
		{
			if (!texture)
				return {};

			auto exists = [&](const std::string& relative)
			{
				std::error_code ec;
				return !relative.empty()
					&& std::filesystem::exists(modelDirectory / relative, ec);
			};

			// `relative_filename` is the exporter's own answer and is right
			// whenever it is present, so it is asked first.
			const std::string relative(texture->relative_filename.data,
									   texture->relative_filename.length);
			if (exists(relative))
				return relative;

			// Then the bare name beside the model.
			const std::string absolute(texture->filename.data, texture->filename.length);
			if (!absolute.empty())
			{
				const std::string name =
					std::filesystem::path(absolute).filename().generic_string();
				if (exists(name))
					return name;
			}

			// Nothing found. The relative name is still the most useful thing
			// to hand back -- the asset manager logs what it could not open,
			// and a name somebody recognises is better than one from a machine
			// they have never seen.
			return relative.empty()
				? std::filesystem::path(absolute).filename().generic_string()
				: relative;
		}

		int AddTexture(ImportedModel& model, const std::filesystem::path& directory,
					   const ufbx_texture* texture, bool srgb)
		{
			if (!texture)
				return -1;

			const std::string path = ResolveTexturePath(directory, texture);
			if (path.empty())
				return -1;

			for (size_t i = 0; i < model.Textures.size(); i++)
			{
				if (model.Textures[i].Path == path)
					return (int)i;
			}

			ImportedTexture entry;
			entry.Path = path;
			entry.SRGB = srgb;
			model.Textures.push_back(std::move(entry));
			return (int)model.Textures.size() - 1;
		}

		// --- materials --------------------------------------------------------
		//
		// **This is the row's stated cost, and it is real.** An FBX material is
		// Lambert or Phong underneath; metallic-roughness arrives as vendor
		// extras under a different property name per exporter -- Maya's
		// Stingray, Max's Physical Material, Blender's Principled export. ufbx
		// normalises those into `material->pbr`, and that is what this reads.
		//
		// What it deliberately does not do is *invent* PBR from Phong. A file
		// carrying only a Lambert gets its diffuse colour and the material
		// defaults, and the log says so, because a roughness guessed from a
		// shininess exponent is a number nobody can debug six months later.
		void ReadMaterial(ImportedModel& model, const std::filesystem::path& directory,
						  const ufbx_material& source, ImportedMaterial& out)
		{
			out.Name = source.name.length ? std::string(source.name.data, source.name.length)
										  : "Material";

			const ufbx_material_pbr_maps& pbr = source.pbr;

			// **Whether this material has PBR to give at all.**
			//
			// ufbx exposes `pbr` for every material, including the two FBX
			// builtins -- for those it *derives* the view, turning a shininess
			// exponent into a roughness. Reading it unconditionally is how the
			// first cut of this importer came to fabricate a roughness while the
			// design entry said it would not, which scenetest caught on its first
			// run.
			//
			// Everything else in the enum is a real PBR shader -- Arnold, 3ds Max
			// Physical, Maya Stingray, glTF, OSL standard surface -- whose numbers
			// are the artist's own and should be taken exactly.
			const bool isPbr = source.shader_type != UFBX_SHADER_FBX_LAMBERT
							&& source.shader_type != UFBX_SHADER_FBX_PHONG;

			if (isPbr && pbr.base_color.has_value)
			{
				const ufbx_vec4 colour = pbr.base_color.value_vec4;
				out.Params.BaseColor = { (float)colour.x, (float)colour.y,
										 (float)colour.z, (float)colour.w };
			}
			else if (source.fbx.diffuse_color.has_value)
			{
				// The Lambert fallback: a colour is a colour in any shading
				// model, so this one transfers honestly. Nothing else does.
				const ufbx_vec3 colour = source.fbx.diffuse_color.value_vec3;
				out.Params.BaseColor = { (float)colour.x, (float)colour.y,
										 (float)colour.z, 1.0f };
			}

			// Left at the engine's defaults for a Lambert or a Phong, deliberately.
			// A shininess exponent is not a roughness, and a number that looks like
			// one is worse than an obvious default: the first is a bug somebody
			// spends an afternoon on, the second is a slider they move.
			if (isPbr && pbr.metalness.has_value)
				out.Params.Metallic = (float)pbr.metalness.value_real;
			if (isPbr && pbr.roughness.has_value)
				out.Params.Roughness = (float)pbr.roughness.value_real;

			if (isPbr && pbr.emission_color.has_value)
			{
				const ufbx_vec3 emissive = pbr.emission_color.value_vec3;
				out.Params.EmissiveColor = { (float)emissive.x, (float)emissive.y,
											 (float)emissive.z, 1.0f };
			}

			// Colour maps are sRGB; everything else is data. Getting this
			// backwards is the most common cause of PBR that looks subtly
			// washed out, which is why the glTF importer says the same thing.
			// A *map* is not a derived number -- a texture is either connected or
			// it is not -- so these are taken from either kind of material. For a
			// Phong the diffuse map is the base colour map under another name, and
			// dropping it would lose the artist's actual work over a technicality.
			out.BaseColorTexture = AddTexture(model, directory,
											  isPbr ? pbr.base_color.texture
													: source.fbx.diffuse_color.texture, true);
			out.EmissiveTexture = AddTexture(model, directory,
											 isPbr ? pbr.emission_color.texture
												   : source.fbx.emission_color.texture, true);
			out.NormalTexture = AddTexture(model, directory,
										   isPbr ? pbr.normal_map.texture
												 : source.fbx.normal_map.texture, false);
			out.OcclusionTexture = AddTexture(model, directory,
											  isPbr ? pbr.ambient_occlusion.texture
													: nullptr, false);

			// FBX keeps metalness and roughness as separate maps where glTF
			// packs them into one. The engine's packed slot is filled by the
			// glTF path only; here the roughness map is the one that matters
			// and metalness usually is not textured at all.
			out.MetallicRoughnessTexture = AddTexture(model, directory,
													  isPbr ? pbr.roughness.texture
															: nullptr, false);

			if (out.BaseColorTexture >= 0)  out.Params.MapFlags |= MaterialMap_BaseColor;
			if (out.NormalTexture >= 0)     out.Params.MapFlags |= MaterialMap_Normal;
			if (out.OcclusionTexture >= 0)  out.Params.MapFlags |= MaterialMap_Occlusion;
			if (out.EmissiveTexture >= 0)   out.Params.MapFlags |= MaterialMap_Emissive;
			if (out.MetallicRoughnessTexture >= 0)
				out.Params.MapFlags |= MaterialMap_Roughness | MaterialMap_Metallic;

			if (!isPbr)
			{
				RV_CORE_WARN("FBX material '{0}' is a {1} material, which has no PBR "
							 "properties. Its colour and maps are used; metallic and "
							 "roughness stay at the engine's defaults, because a "
							 "roughness derived from a shininess exponent is a number "
							 "nobody could debug.",
							 out.Name,
							 source.shader_type == UFBX_SHADER_FBX_LAMBERT ? "Lambert"
																		   : "Phong");
			}
		}

		// --- skinning (8.9 stage 2) -------------------------------------------
		//
		// **The skeleton is what the skin binds, not what looks like a bone.**
		// A node carrying a `ufbx_bone` attribute is a hint the exporter left;
		// the definition is the set of `ufbx_skin_cluster`s, because a cluster
		// is the thing that owns a bind matrix and a list of weights. Rigs
		// routinely carry bone-shaped nodes nothing is skinned to -- IK
		// targets, twist helpers, a control rig the animator drives -- and
		// importing those gives a skeleton whose indices address nothing.
		struct SkinMapping
		{
			// ufbx cluster index to skeleton bone index, or -1 for a cluster
			// whose bone was dropped.
			std::vector<int> ToBone;
			// Every bone node, so a baked animation channel can ask whether the
			// node it moves is a bone of this skeleton.
			std::unordered_map<const ufbx_node*, int> BoneOf;
		};

		bool ReadSkin(const ufbx_skin_deformer& skin, Skeleton& out, SkinMapping& mapping)
		{
			const size_t count = skin.clusters.count;
			if (count == 0)
				return false;

			std::unordered_map<const ufbx_node*, int> clusterOf;
			for (size_t i = 0; i < count; i++)
			{
				const ufbx_node* bone = skin.clusters.data[i]->bone_node;
				if (bone)
					clusterOf.emplace(bone, (int)i);
			}

			// **A bone's parent is its nearest bone *ancestor*, not its direct
			// parent.** An FBX rig puts nodes between bones as a matter of
			// course -- the armature, a namespace group, a scale helper ufbx
			// inserted for a non-standard inherit mode -- and stopping at the
			// first parent that is not a bone would make every bone in the file
			// a root. The glTF path gets away with the simpler rule because its
			// joints array is flat by construction.
			std::vector<int> parentOf(count, -1);
			for (size_t i = 0; i < count; i++)
			{
				const ufbx_node* bone = skin.clusters.data[i]->bone_node;
				for (const ufbx_node* walk = bone ? bone->parent : nullptr;
					 walk; walk = walk->parent)
				{
					const auto found = clusterOf.find(walk);
					if (found != clusterOf.end())
					{
						parentOf[i] = found->second;
						break;
					}
				}
			}

			// Depth first from the roots, so a parent is emitted before its
			// children -- Skeleton requires that, and composition is one
			// forward pass. The visited set makes a malformed cycle a dropped
			// bone rather than a hang, which is the glTF path's rule too.
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
				if (parentOf[i] < 0 && skin.clusters.data[i]->bone_node)
					stack.push_back((int)i);
			}
			std::reverse(stack.begin(), stack.end());

			while (!stack.empty())
			{
				const int cluster = stack.back();
				stack.pop_back();

				if (mapping.ToBone[cluster] >= 0)
					continue;

				mapping.ToBone[cluster] = (int)order.size();
				order.push_back(cluster);

				for (auto child = childrenOf[cluster].rbegin();
					 child != childrenOf[cluster].rend(); ++child)
				{
					stack.push_back(*child);
				}
			}

			const std::string skinName = skin.name.length
									   ? std::string(skin.name.data, skin.name.length)
									   : std::string("(unnamed)");

			if (order.size() != count)
			{
				RV_CORE_WARN("Skin '{0}' has {1} cluster(s) but only {2} reach a root; "
							 "the rest are dropped", skinName, count, order.size());
			}

			if (order.empty())
				return false;

			// **The rest pose comes from the bind matrices, not from where the
			// nodes currently sit, and that is a real difference from glTF.**
			//
			// An FBX is routinely saved with the rig standing on an animated
			// frame, so a bone node's transform is whatever pose the artist
			// happened to leave -- while the bind matrix is the one thing in
			// the file defined to be the bind pose. Deriving the rest from it
			// also makes the property this whole path is tested on true *by
			// construction*: a skeleton at rest composes to the identity for
			// every bone, so the mesh renders exactly as it was modelled.
			//
			// `geometry_to_bone` is the mesh's *geometry* space to the bone,
			// which is the space `mesh.vertex_position` is read in above, so
			// the two agree without a conversion. `mesh_node_to_bone` is the
			// same quantity through the node's own transform and is the wrong
			// one here for exactly that reason.
			out.Bones.clear();
			out.Bones.resize(order.size());

			std::vector<Mat4> globalRest(order.size(), Mat4(1.0f));

			for (size_t bone = 0; bone < order.size(); bone++)
			{
				const int cluster = order[bone];
				const ufbx_skin_cluster& source = *skin.clusters.data[cluster];
				const ufbx_node* node = source.bone_node;

				Bone& target = out.Bones[bone];
				target.Name = node && node->name.length
							? std::string(node->name.data, node->name.length)
							: ("bone" + std::to_string(bone));
				target.Parent = parentOf[cluster] >= 0 ? mapping.ToBone[parentOf[cluster]] : -1;
				target.InverseBind = ToMat4(source.geometry_to_bone);

				globalRest[bone] = Math::Inverse(target.InverseBind);

				const Mat4 local = target.Parent >= 0
								 ? Math::Inverse(globalRest[target.Parent]) * globalRest[bone]
								 : globalRest[bone];

				Quat rotation;
				if (Math::Decompose(local, target.RestPosition, rotation, target.RestScale))
				{
					target.RestRotation = rotation;
				}
				else
				{
					// A singular bind matrix -- a bone flattened to nothing on
					// one axis. Left at the identity rather than dropped,
					// because the indices around it are already assigned.
					RV_CORE_WARN("Bone '{0}' has a bind matrix that will not decompose; "
								 "resting it at the identity", target.Name);
					target.RestPosition = Vec3(0.0f);
					target.RestScale = Vec3(1.0f);
				}

				if (node)
					mapping.BoneOf.emplace(node, (int)bone);
			}

			if (!out.IsWellOrdered())
			{
				RV_CORE_ERROR("Skin '{0}' could not be ordered parents-first", skinName);
				return false;
			}

			return true;
		}

		// One mesh vertex's influences, as the top four.
		//
		// ufbx sorts a vertex's weights by decreasing influence, so "the top
		// four" is a prefix rather than a search. They are explicitly *not*
		// normalised in the file, and a fifth influence dropped here unbalances
		// them further -- so this renormalises, because weights that do not sum
		// to one shrink the vertex towards the origin rather than failing
		// visibly.
		void ReadVertexSkin(const ufbx_skin_deformer& skin, const SkinMapping& mapping,
							uint32_t vertex, UVec4& joints, Vec4& weights)
		{
			joints = UVec4(0);
			weights = Vec4(0.0f);

			if (vertex < skin.vertices.count)
			{
				const ufbx_skin_vertex& entry = skin.vertices.data[vertex];

				uint32_t taken = 0;
				float total = 0.0f;
				for (uint32_t i = 0; i < entry.num_weights && taken < 4; i++)
				{
					const ufbx_skin_weight& influence =
						skin.weights.data[entry.weight_begin + i];
					if (influence.cluster_index >= mapping.ToBone.size())
						continue;

					const int bone = mapping.ToBone[influence.cluster_index];
					if (bone < 0)
						continue;

					joints[taken] = (uint32_t)bone;
					weights[taken] = (float)influence.weight;
					total += (float)influence.weight;
					taken++;
				}

				if (total > 1e-6f)
				{
					weights /= total;
					return;
				}
			}

			// No influence this skeleton can address. Bone 0 at full weight
			// leaves the vertex wherever the root puts it, which is wrong in a
			// way somebody can see and name; all-zero weights collapse it to the
			// origin and streak the mesh across the screen.
			joints = UVec4(0);
			weights = Vec4(1.0f, 0.0f, 0.0f, 0.0f);
		}

		// --- animation (8.9 stage 2) ------------------------------------------
		//
		// **Baked, not read as curves, and that is the whole design.** An FBX
		// node's transform is not a TRS triple: it is a chain of ten parts --
		// translation, rotation offset, rotation pivot, pre-rotation, rotation,
		// post-rotation, the two pivot inverses, scaling offset and scaling
		// pivot -- composed in an order the node itself names. Reading the
		// `Lcl Rotation` X, Y and Z curves and calling the result a quaternion
		// track is the classic FBX importer bug, and it is wrong twice over:
		// pre-rotation is where Maya keeps joint orientation, so every bone
		// arrives rotated by its joint orient, and euler angles interpolated as
		// if they were quaternion keys take a different path between the same
		// two poses.
		//
		// `ufbx_bake_anim` composes that chain and hands back translation,
		// rotation and scale keys that are *defined* to be linearly
		// interpolatable, which is exactly the contract BoneTrack has. Key
		// reduction is on, rotation included, because the runtime slerps them --
		// that option is a claim about the sampler, and would be a lie if
		// SamplePose lerped.
		void ReadAnimation(const ufbx_scene& scene, const ufbx_anim_stack& stack,
						   const SkinMapping& mapping, size_t boneCount,
						   Anim::Clip& out)
		{
			out.Name = stack.name.length ? std::string(stack.name.data, stack.name.length)
										 : "Clip";
			out.Tracks.assign(boneCount, BoneTrack{});

			ufbx_bake_opts opts = {};
			// A clip authored on frames 30-60 starts at zero, as every glTF one
			// does. Not the same as subtracting the start time afterwards: ufbx
			// trims on the file's own integer ticks and does not accumulate the
			// rounding.
			opts.trim_start_time = true;
			opts.key_reduction_enabled = true;
			opts.key_reduction_rotation = true;

			ufbx_error error;
			ufbx_baked_anim* baked = ufbx_bake_anim(&scene, stack.anim, &opts, &error);
			if (!baked)
			{
				char description[512];
				ufbx_format_error(description, sizeof(description), &error);
				RV_CORE_WARN("Animation '{0}' would not bake: {1}", out.Name, description);
				return;
			}

			for (size_t i = 0; i < baked->nodes.count; i++)
			{
				const ufbx_baked_node& node = baked->nodes.data[i];
				if (node.typed_id >= scene.nodes.count)
					continue;

				const auto found = mapping.BoneOf.find(scene.nodes.data[node.typed_id]);
				if (found == mapping.BoneOf.end())
					continue;   // moves something that is not a bone of this skin

				BoneTrack& track = out.Tracks[found->second];

				// A constant channel keeps its single key rather than being
				// dropped. Dropping it would fall the bone back to its rest
				// transform, and rest here is the *bind* pose -- so a bone the
				// clip holds somewhere other than bind would snap to bind
				// instead of staying where the animator put it.
				track.Position.Times.reserve(node.translation_keys.count);
				track.Position.Values.reserve(node.translation_keys.count);
				for (size_t k = 0; k < node.translation_keys.count; k++)
				{
					const ufbx_baked_vec3& key = node.translation_keys.data[k];
					track.Position.Times.push_back((float)key.time);
					track.Position.Values.push_back(ToVec3(key.value));
				}

				track.Rotation.Times.reserve(node.rotation_keys.count);
				track.Rotation.Values.reserve(node.rotation_keys.count);
				for (size_t k = 0; k < node.rotation_keys.count; k++)
				{
					const ufbx_baked_quat& key = node.rotation_keys.data[k];
					track.Rotation.Times.push_back((float)key.time);
					track.Rotation.Values.push_back(ToQuat(key.value));
				}

				track.Scale.Times.reserve(node.scale_keys.count);
				track.Scale.Values.reserve(node.scale_keys.count);
				for (size_t k = 0; k < node.scale_keys.count; k++)
				{
					const ufbx_baked_vec3& key = node.scale_keys.data[k];
					track.Scale.Times.push_back((float)key.time);
					track.Scale.Values.push_back(ToVec3(key.value));
				}
			}

			ufbx_free_baked_anim(baked);
			out.RecomputeDuration();
		}

		// --- geometry ---------------------------------------------------------
		//
		// One `ImportedPrimitive` per (mesh, material) pair, which is the split
		// the renderer already needs: it binds a material per draw, so two
		// materials on one mesh cannot share a buffer. ufbx calls those
		// `material_parts`.
		//
		// Faces are n-gons -- quads are the common case where a glTF primitive
		// is triangles by definition -- so every face goes through
		// `ufbx_triangulate_face` on the way in.
		struct VertexKey
		{
			uint32_t Index = 0;
			uint32_t Normal = 0;
			uint32_t UV = 0;

			bool operator==(const VertexKey& other) const
			{
				return Index == other.Index && Normal == other.Normal && UV == other.UV;
			}
		};

		struct VertexKeyHash
		{
			size_t operator()(const VertexKey& key) const
			{
				// Three 32-bit indices into one bucket. The mix is the usual
				// one; a mesh big enough for the collision rate to matter is
				// bigger than anything this importer will see.
				size_t hash = key.Index;
				hash = hash * 31 + key.Normal;
				hash = hash * 31 + key.UV;
				return hash;
			}
		};

		void ReadMeshPart(const ufbx_mesh& mesh, const ufbx_mesh_part& part,
						  const ufbx_skin_deformer* skin, const SkinMapping& mapping,
						  ImportedPrimitive& out)
		{
			std::unordered_map<VertexKey, uint32_t, VertexKeyHash> seen;
			std::vector<uint32_t> triangle(mesh.max_face_triangles * 3);

			for (size_t f = 0; f < part.face_indices.count; f++)
			{
				const ufbx_face face = mesh.faces.data[part.face_indices.data[f]];
				const uint32_t triangles = ufbx_triangulate_face(
					triangle.data(), triangle.size(), &mesh, face);

				for (uint32_t corner = 0; corner < triangles * 3; corner++)
				{
					const uint32_t index = triangle[corner];

					VertexKey key;
					key.Index = mesh.vertex_position.indices.data[index];
					key.Normal = mesh.vertex_normal.exists
							   ? mesh.vertex_normal.indices.data[index] : 0;
					key.UV = mesh.vertex_uv.exists
						   ? mesh.vertex_uv.indices.data[index] : 0;

					const auto found = seen.find(key);
					if (found != seen.end())
					{
						out.Indices.push_back(found->second);
						continue;
					}

					MeshVertex vertex;
					vertex.Position = ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_position, index));
					vertex.Normal = mesh.vertex_normal.exists
								  ? ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_normal, index))
								  : Vec3(0.0f, 1.0f, 0.0f);

					if (mesh.vertex_uv.exists)
					{
						const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, index);

						// FBX puts the UV origin at the bottom left and every
						// texture this engine samples has it at the top, which
						// is the same flip the glTF path never needs because
						// glTF already agrees with the sampler.
						vertex.TexCoord = Vec2((float)uv.x, 1.0f - (float)uv.y);
					}
					else
					{
						vertex.TexCoord = Vec2(0.0f, 0.0f);
					}

					// Influences follow the *mesh* vertex, which is what
					// `key.Index` already is -- so the dedup key needs nothing
					// added to it: two corners that share a position share
					// their weights by definition.
					if (skin)
					{
						UVec4 joints;
						Vec4 weights;
						ReadVertexSkin(*skin, mapping, key.Index, joints, weights);
						out.Joints.push_back(joints);
						out.Weights.push_back(weights);
					}

					const uint32_t position = (uint32_t)out.Vertices.size();
					out.Vertices.push_back(vertex);
					out.Indices.push_back(position);
					seen.emplace(key, position);
				}
			}
		}
	}

	bool FbxImporter::Import(const std::filesystem::path& path, ImportedModel& out)
	{
		// Cooked bytes if there are any, exactly as the glTF importer does.
		// Which parser produced a `.rvmesh` stopped mattering when it was
		// cooked, so this is the same three lines and deliberately so.
		std::vector<uint8_t> bytes;
		if ((ImportCache::Fetch(path, bytes) || IO::VFS::ReadBytes(path, bytes)) &&
			MeshCook::IsCooked(bytes.data(), bytes.size()))
		{
			if (MeshCook::Deserialize(out, bytes.data(), bytes.size()))
				return true;

			RV_CORE_ERROR("Cooked mesh '{0}' will not parse", path.string());
			return false;
		}

		return ImportSource(path, out);
	}

	bool FbxImporter::ImportSource(const std::filesystem::path& path, ImportedModel& out)
	{
		ufbx_load_opts opts = {};

		// **The conversion that decides whether this format works at all.**
		// glTF is Y-up, right-handed, metres, always. An FBX carries its own
		// axes and unit scale, and the three exporters people use disagree:
		// Maya is Y-up centimetres, 3ds Max is Z-up centimetres, Blender is
		// Z-up metres. Asking ufbx for the engine's space on *load* keeps the
		// geometry and the node transforms in one space as they are read,
		// rather than fixing them afterwards in two places that can drift.
		opts.target_axes = ufbx_axes_right_handed_y_up;
		opts.target_unit_meters = 1.0f;
		opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

		// A mesh with no normals is a mesh the lighting cannot use, and FBX
		// does not require them.
		opts.generate_missing_normals = true;

		ufbx_error error;
		ufbx_scene* scene = ufbx_load_file(path.string().c_str(), &opts, &error);
		if (!scene)
		{
			char description[512];
			ufbx_format_error(description, sizeof(description), &error);
			RV_CORE_ERROR("Could not read FBX '{0}': {1}", path.string(), description);
			return false;
		}

		out = ImportedModel();
		out.Name = path.stem().string();

		const std::filesystem::path directory = path.parent_path();

		out.Materials.reserve(scene->materials.count);
		for (size_t i = 0; i < scene->materials.count; i++)
		{
			ImportedMaterial material;
			ReadMaterial(out, directory, *scene->materials.data[i], material);
			out.Materials.push_back(std::move(material));
		}

		// A material's index in `out.Materials` is its index in
		// `scene->materials`, which is what lets a part name one by pointer
		// identity below.
		std::unordered_map<const ufbx_material*, int> materialIndex;
		for (size_t i = 0; i < scene->materials.count; i++)
			materialIndex.emplace(scene->materials.data[i], (int)i);

		// Nodes, parents before children. ufbx guarantees that ordering in
		// `scene->nodes`, which is the same guarantee `ImportedModel` makes to
		// its own consumers -- one forward pass builds the entity tree.
		std::unordered_map<const ufbx_node*, int> nodeIndex;

		for (size_t i = 0; i < scene->nodes.count; i++)
		{
			const ufbx_node* node = scene->nodes.data[i];
			if (node->is_root)
				continue;

			ImportedNode entry;
			entry.Name = node->name.length
					   ? std::string(node->name.data, node->name.length) : "Node";

			const ufbx_transform& transform = node->local_transform;
			entry.Position = ToVec3(transform.translation);
			entry.Scale = ToVec3(transform.scale);

			// Euler XYZ in radians, which is what ImportedNode carries and
			// what the scene format writes.
			const ufbx_vec3 euler =
				ufbx_quat_to_euler(transform.rotation, UFBX_ROTATION_ORDER_XYZ);
			entry.Rotation = Vec3(Math::Radians((float)euler.x),
								  Math::Radians((float)euler.y),
								  Math::Radians((float)euler.z));

			const auto parent = nodeIndex.find(node->parent);
			entry.Parent = parent == nodeIndex.end() ? -1 : parent->second;

			nodeIndex.emplace(node, (int)out.Nodes.size());
			out.Nodes.push_back(std::move(entry));
		}

		// **The skeleton before the geometry, because the geometry addresses
		// it.** One skin, the first in the file: a file with two independent
		// characters in it is a file that should have been two files, and
		// supporting it would mean every skinned primitive carrying which
		// skeleton it belongs to for a case nobody exports. `ImportedModel`
		// says the same thing about glTF.
		SkinMapping mapping;
		const ufbx_skin_deformer* skeletonSkin = nullptr;

		if (scene->skin_deformers.count > 0)
		{
			skeletonSkin = scene->skin_deformers.data[0];
			if (!ReadSkin(*skeletonSkin, out.Skeleton, mapping))
			{
				out.Skeleton.Bones.clear();
				skeletonSkin = nullptr;
			}

			if (scene->skin_deformers.count > 1)
			{
				RV_CORE_WARN("FBX '{0}' has {1} skins; only the first is imported",
							 path.string(), scene->skin_deformers.count);
			}
		}

		// Geometry, one primitive per (mesh, material) pair.
		for (size_t i = 0; i < scene->nodes.count; i++)
		{
			const ufbx_node* node = scene->nodes.data[i];
			if (!node->mesh)
				continue;

			const ufbx_mesh& mesh = *node->mesh;
			const auto owner = nodeIndex.find(node);

			// Only the skin the skeleton came from. A second skin's cluster
			// indices address a different bone list, so reading its weights
			// through this mapping would produce plausible numbers pointing at
			// the wrong bones -- which is worse than the mesh arriving static.
			const ufbx_skin_deformer* skin = nullptr;
			for (size_t d = 0; skeletonSkin && d < mesh.skin_deformers.count; d++)
			{
				if (mesh.skin_deformers.data[d] == skeletonSkin)
				{
					skin = skeletonSkin;
					break;
				}
			}

			for (size_t p = 0; p < mesh.material_parts.count; p++)
			{
				const ufbx_mesh_part& part = mesh.material_parts.data[p];
				if (part.num_triangles == 0)
					continue;

				ImportedPrimitive primitive;
				primitive.Name = out.Nodes.empty() || owner == nodeIndex.end()
							   ? std::string("Mesh")
							   : out.Nodes[owner->second].Name;

				if (p < mesh.materials.count)
				{
					const auto found = materialIndex.find(mesh.materials.data[p]);
					primitive.Material = found == materialIndex.end() ? -1 : found->second;
				}

				ReadMeshPart(mesh, part, skin, mapping, primitive);
				if (primitive.Vertices.empty())
					continue;

				if (owner != nodeIndex.end())
					out.Nodes[owner->second].Primitives.push_back((int)out.Primitives.size());

				out.Primitives.push_back(std::move(primitive));
			}
		}

		// Animations last: they address bones, which the skin above defined.
		// An empty clip -- one whose every channel moved something that is not
		// a bone of this skin -- is dropped rather than kept, because a clip
		// that plays and does nothing is indistinguishable from a broken one.
		if (!out.Skeleton.IsEmpty())
		{
			for (size_t i = 0; i < scene->anim_stacks.count; i++)
			{
				Anim::Clip clip;
				ReadAnimation(*scene, *scene->anim_stacks.data[i], mapping,
							  out.Skeleton.Size(), clip);

				const bool moves = std::any_of(clip.Tracks.begin(), clip.Tracks.end(),
											   [](const BoneTrack& track)
											   { return !track.IsEmpty(); });
				if (moves)
					out.Clips.push_back(std::move(clip));
			}
		}

		ufbx_free_scene(scene);

		if (out.Primitives.empty())
		{
			RV_CORE_ERROR("FBX '{0}' has no geometry in it", path.string());
			return false;
		}

		RV_CORE_INFO("Imported FBX '{0}': {1} primitive(s), {2} material(s), {3} node(s)",
					 path.filename().string(), out.Primitives.size(),
					 out.Materials.size(), out.Nodes.size());

		if (!out.Skeleton.IsEmpty())
		{
			RV_CORE_INFO("  with {0} bone(s) and {1} clip(s)",
						 out.Skeleton.Size(), out.Clips.size());
		}

		return true;
	}
}
