#pragma once
#include "RageV/Math/Math.h"
#include "RageV/Renderer/RHI/RHIDevice.h"

#include <filesystem>
#include <vector>

namespace RageV
{
	// **Lighting that survives a restart.**
	//
	// Everything the engine has computed about indirect light until now has
	// been *cached*: solved once at runtime and held in memory, gone the moment
	// the process ends. This is the other thing -- computed once, written to
	// disk, loaded and used. See docs/BAKING-ROADMAP.md, which spends its first
	// section on why those two words are not the same and what each buys.
	//
	// **One file, one baked thing.** A field is a volume texture; a probe is a
	// cube array. Both are "some texture, plus the facts that say whether it is
	// still true", and that is the whole of this format: a header describing
	// the shape, a stamp describing what it was baked *for*, and the bytes.
	//
	// The stamp is what makes a bake safe to trust. A stored answer nobody can
	// invalidate is an answer that silently stops being true -- the same lesson
	// the reflection probes learned when nothing ever raised their dirty flag.
	// So a loader compares the stamp against the scene it is loading into and
	// refuses a bake that was made for a different one, and the field solves at
	// runtime as it did before. A refused bake is never a broken picture.
	class BakedLighting
	{
	public:
		// What kind of thing the payload is. Written into the file, so a loader
		// that is handed the wrong file says so rather than reading a cube as a
		// volume and drawing whatever that turns out to be.
		enum class Kind : uint32_t
		{
			IrradianceField = 1,
			ReflectionProbe = 2,
		};

		// **What the bake was made for.** Every number here is something that,
		// if it changed, makes the stored light wrong: where the box is, how
		// big, how finely divided, and the lighting it was solved under -- the
		// same hash the runtime field already keeps for exactly this purpose.
		//
		// Compared whole. There is no "close enough" for a stamp: two of these
		// either describe the same bake or they do not.
		struct Stamp
		{
			Vec3 Centre{ 0.0f };
			Vec3 Extents{ 1.0f };
			Vec3 AxisX{ 1.0f, 0.0f, 0.0f };
			Vec3 AxisY{ 0.0f, 1.0f, 0.0f };
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t Depth = 0;
			uint32_t Tiles = 0;
			// The byte hash of the light list and the environment, as
			// Scene::UpdateIrradianceVolumes computes it.
			uint64_t Lighting = 0;

			bool Matches(const Stamp& other) const;
		};

		// Writes the texture's contents beside its stamp. Reads the texture
		// back through the device, which stalls -- see RHIDevice::ReadTexture,
		// and note that this is a bake, so the frame it stalls is one nobody is
		// watching.
		//
		// Creates the directory if it is missing. False, with a log line, on
		// anything that goes wrong; a bake that did not happen must never look
		// like one that did.
		static bool Write(RHI::RHIDevice& device, const std::filesystem::path& file,
						  Kind kind, const Stamp& stamp,
						  const RHI::Ref<RHI::RHITexture>& texture);

		// Reads a file's stamp and payload. False if the file is missing, is
		// not one of these, is a version this build does not know, or holds a
		// different kind -- each of which is a reason to solve at runtime
		// instead, and none of which is an error worth stopping for.
		static bool Read(const std::filesystem::path& file, Kind kind,
						 Stamp& stamp, std::vector<uint8_t>& payload);

		// Where a scene's bakes live: `<project>/assets/baked/<scene>/`. Beside
		// the assets rather than in the cooked cache, because a bake is
		// authored content -- it is the output of a deliberate action, it wants
		// to be in version control, and a cache is a thing you are allowed to
		// delete.
		static std::filesystem::path DirectoryFor(const std::string& sceneName);

		// Where a key from DirectoryFor lands on disk. Writing is the one half
		// of this that is not virtual: only the editor and the bake tool write,
		// and they write loose files into the project.
		static std::filesystem::path WritePathFor(const std::filesystem::path& key);
	};
}
