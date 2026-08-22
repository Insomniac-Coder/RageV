#pragma once

#include "Asset.h"
#include "RageV/Renderer/Material.h"

#include <filesystem>
#include <string>

namespace RageV::Assets
{
	// A material as it is *authored and stored*, which is not what `Material`
	// is.
	//
	// `Material` owns GPU objects -- a descriptor set per frame in flight, a
	// uniform buffer, resolved textures -- and cannot exist without a device.
	// This is the plain data behind one: the scalars, and the five maps by
	// handle rather than by pointer. It is what the file holds, what the
	// importer writes, and what the manager turns into a `Material` once.
	//
	// Keeping the two apart is what lets a material be inspected, diffed and
	// packaged by tools that have no renderer -- rvpack among them.
	struct MaterialDesc
	{
		std::string Name = "Material";
		MaterialParams Params;

		// Opaque or Blend. In the desc rather than in Params because it is not
		// a shading parameter -- see the enum in Material.h. Written as a name
		// so a `.rmat` reads as one, and absent from a file means Opaque, which
		// is what every material written before this existed is.
		BlendMode Blend = BlendMode::Opaque;

		// Null means "no map", which is not the same as a missing file: the
		// shader falls back to the scalar parameter, and MapFlags says which.
		//
		// **Explicitly Invalid(), because a default-constructed UUID is
		// random.** That is deliberate elsewhere -- an entity or an asset gets
		// an identity by existing -- but here a handle is a *reference*, and a
		// randomly-generated one is a reference to nothing that answers
		// IsValid() with yes. Left defaulted, every material claimed five maps,
		// wrote five bogus handles into its file, and rendered correctly only
		// because an unresolvable handle happens to clear the slot on load.
		//
		// The same shape as EntityRef needing to be its own type: a field whose
		// zero value means "none" cannot use a type whose default is "something
		// new".
		AssetHandle BaseColorMap = AssetHandle::Invalid();
		AssetHandle NormalMap = AssetHandle::Invalid();
		AssetHandle OcclusionMap = AssetHandle::Invalid();
		AssetHandle EmissiveMap = AssetHandle::Invalid();

		// Separate greyscale roughness and metallic, and dielectric
		// reflectance. glTF packs the first two into one texture; the importer
		// splits it, so nothing downstream of an import sees a packed map.
		AssetHandle RoughnessMap = AssetHandle::Invalid();
		AssetHandle MetallicMap = AssetHandle::Invalid();
		AssetHandle SpecularMap = AssetHandle::Invalid();
		AssetHandle HeightMap = AssetHandle::Invalid();
	};

	// Reads and writes `.rmat`, the file behind AssetType::Material.
	//
	// YAML like everything else authored here. A material is a dozen numbers
	// and five handles; somebody will want to diff one in a review, or fix a
	// handle by hand after moving files about.
	class MaterialSerializer
	{
	public:
		static bool Save(const MaterialDesc& material, const std::filesystem::path& path);

		// False leaves `out` untouched, so a caller that pre-filled a default
		// keeps it rather than being handed an empty material it has to detect.
		static bool Load(MaterialDesc& out, const std::filesystem::path& path);
	};
}
