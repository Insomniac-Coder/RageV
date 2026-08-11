#pragma once
#include "imgui.h"
#include "RageV/Asset/Asset.h"
#include <filesystem>

// Asset and file icons, drawn as vectors rather than shipped as images.
//
// ---------------------------------------------------------------------------
// Why vectors and not a bitmap set
// ---------------------------------------------------------------------------
//
// The content browser has a size slider running from 34 to 128 pixels, and the
// editor has a UI scale on top of that. A bitmap set is sharp at the size it
// was authored at and soft everywhere else, so covering that range honestly
// means several sizes of every icon, and a rule about which to pick.
//
// It also means **two sets** once there is a light theme, because a set drawn
// for a dark background has the wrong contrast on a light one. Drawing them as
// paths in the theme's own colours makes that problem not exist: one
// definition, correct in both themes, crisp at every size, and nothing to load
// or fail to load.
//
// ---------------------------------------------------------------------------
// Why they are monochrome
// ---------------------------------------------------------------------------
//
// The obvious thing is to colour icons by type, and the palette has one rule:
// red means "you can act on this, or it is acting now". Colouring a mesh red
// because it is a mesh spends that signal on decoration, and once every icon is
// coloured, none of them is telling you anything.
//
// So type is carried by *shape*, which distinguishes better than hue anyway --
// it survives being small, it survives being greyscale, and it works for a
// reader who cannot separate red from green. Colour is left to say what colour
// says best here: this one is selected, this one is under the cursor.
//
// ---------------------------------------------------------------------------
// Why a kind is not an AssetType
// ---------------------------------------------------------------------------
//
// `AssetType` is what the engine can *import* -- seven things with a handle and
// a loader. The content browser lists what is in the folder, which is a wider
// set: the scripts beside the scenes, the shaders, a readme, the .csproj. All
// of those had one glyph between them, and a browser where half the files look
// identical is a browser you navigate by reading filenames.
//
// So the icon set is keyed on a `Kind`, which an AssetType maps into and which
// an extension can also reach. Adding a file type the editor grows to
// understand means one enumerator and one drawing, not a change to the asset
// system.

namespace RageV::UI
{
	enum class IconKind
	{
		Folder,

		// The seven the registry gives a handle to.
		Mesh,
		Texture,
		Material,
		Prefab,
		Scene,
		Audio,
		Curve,

		// Everything else that turns up in a project's folders.
		Script,     // .cs .cpp .h .hpp .c .cc
		Shader,     // .rvshader .glsl .vert .frag .comp .spv
		Font,       // .ttf .otf
		Document,   // .md .txt
		Data,       // .json .ini .yaml .yml .csproj .rvproject .sln
		Archive,    // .pak .zip

		// Scene contents. The hierarchy has the same problem the browser had:
		// a list of names in one weight, where the only way to tell a light
		// from a camera from a mesh is to read every line. These are what an
		// entity *is*, judged by the components on it.
		Entity,          // nothing that identifies it further
		Light,
		Camera,
		ParticleEmitter,
		AudioSource,

		// Toolbar actions. Not things -- verbs. They live here because the
		// alternative is a second icon system with its own canvas and its own
		// stroke weight, and two icon systems in one toolbar is exactly the
		// mismatch these replace: three-letter abbreviations sitting next to
		// fifteen drawn glyphs.
		ToolTranslate,
		ToolRotate,
		ToolScale,
		More,          // the overflow menu every tool draws as a vertical ellipsis
		Play,
		Stop,
		Pause,

		Unknown,
	};

	// An imported asset's type, as a kind. `AssetType::None` answers Unknown --
	// use KindForFile when there is a path to look at as well.
	IconKind KindForAsset(AssetType type);

	// The type when the registry knows one, the extension when it does not.
	// The registry wins: a .png the engine imported is a Texture whatever the
	// extension table thinks, and the extension is only ever the fallback.
	IconKind KindForFile(const std::filesystem::path& path, AssetType type);

	// Draws into the given draw list, fitted to a square of `size` at
	// `topLeft`. `color` is the whole icon; there are no second colours.
	void DrawIcon(ImDrawList* drawList, ImVec2 topLeft, float size,
				  IconKind kind, ImU32 color);

	// The same icon at text size, advancing the cursor like any other item so
	// a caller can SameLine past it. For a drag preview or a list row.
	void DrawInlineIcon(IconKind kind, ImU32 color);
}
