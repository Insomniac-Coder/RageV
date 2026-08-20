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

		// Geometry, one primitive per (mesh, material) pair.
		for (size_t i = 0; i < scene->nodes.count; i++)
		{
			const ufbx_node* node = scene->nodes.data[i];
			if (!node->mesh)
				continue;

			const ufbx_mesh& mesh = *node->mesh;
			const auto owner = nodeIndex.find(node);

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

				ReadMeshPart(mesh, part, primitive);
				if (primitive.Vertices.empty())
					continue;

				if (owner != nodeIndex.end())
					out.Nodes[owner->second].Primitives.push_back((int)out.Primitives.size());

				out.Primitives.push_back(std::move(primitive));
			}
		}

		// Stage 1 stops here, and says so rather than importing a skeleton it
		// would pose wrongly (7bn).
		if (scene->skin_deformers.count > 0)
		{
			RV_CORE_WARN("FBX '{0}' has {1} skin(s); FBX skinning is not imported yet "
						 "(ROADMAP 8.9 stage 2). The geometry comes in at its bind "
						 "pose.",
						 path.string(), scene->skin_deformers.count);
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
		return true;
	}
}
