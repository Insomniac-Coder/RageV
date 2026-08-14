#pragma once
#include "RageV/Scene/ComponentRegistry.h"
#include "RageV/Scene/Entity.h"
#include <vector>

namespace RageV
{
	class Scene;
}

namespace RageV::UI
{
	// A property row: label left, control right, both sized against the panel
	// rather than against a number somebody typed once.
	//
	// A one-row table per field rather than one table around the whole
	// inspector, because the fields are drawn from a dozen call sites that do
	// not know about each other. Several one-row tables with the same
	// proportions line up with each other, which is the property that matters;
	// what is given up is dragging the divider, so they are not resizable (a
	// drag that moved one row and not the rest would read as a bug).
	//
	// `labelFraction` exists for one row type: three axis fields and their
	// badges need more of the width than a single control does.
	void BeginField(const char* label, const char* tooltip, float labelFraction = 0.38f);
	void EndField();

	// One reflected field, from any registry -- a component's, or one of the
	// three settings blocks'. Returns true when the value changed this frame.
	//
	// `block` is whatever the field's accessor was generated against.
	//
	// The last three are optional and null for a settings block, which has no
	// scene, no component and no owning entity:
	//
	//   * `scene` resolves an Entity field's *name*, which only the scene can
	//     answer -- and has to keep saying something useful when the answer is
	//     "that entity is gone".
	//   * `desc` is read for one hint: a method-name field names the sibling
	//     field holding the entity whose methods to offer.
	//   * `owner` is the fallback for that same hint, and the source of the
	//     animation clips a clip-picking field offers.
	//
	// A field that needs one of them and does not have it draws as plain and
	// says so rather than pretending -- there is no entity to pick from a file
	// full of numbers.
	bool DrawField(const FieldDesc& field, void* block, Scene* scene = nullptr,
				   const ComponentDesc* desc = nullptr, Entity owner = {});

	// An asset slot: the current name, a searchable dropdown of every asset of
	// the accepted type, and a drop target for a drag out of the content
	// browser. Returns true when the handle changed.
	//
	// The dropdown is the half that was missing. Drag-and-drop is the fastest
	// way to assign something you can already see, and the *only* way this had
	// -- so assigning an asset meant knowing which folder it was in and
	// navigating there first. A list of the candidates is what makes a field
	// answerable without leaving the inspector, and it is filtered by type for
	// the same reason the drop is: a handle of the wrong type resolves to
	// nothing and presents as the field simply not working.
	//
	// Types that can be created from nothing get a "New..." entry. Today that
	// is the post profile; a mesh or a texture has to come from somewhere.
	bool DrawAssetPicker(const char* id, AssetHandle& handle, AssetType accepts);

	// A post profile's fields, indented under whatever named it.
	//
	// Edits are written to the asset as they are made and the cache is
	// dropped, so the change is on screen next frame and on disk immediately
	// -- the same contract the curve editor has, and for the same reason:
	// updating the cache and saving on some later event is how an edit
	// survives on screen and not in the file.
	//
	// **This edits a shared asset**, so every camera pointing at the profile
	// changes with it. The drawer says which file it is writing to for exactly
	// that reason.
	void DrawPostProfile(AssetHandle handle);

	// Every field in a list, honouring each one's VisibleIf. Returns true if
	// any of them changed.
	//
	// This is what a settings block is drawn with: the registry is already the
	// single description of what a setting is, and a panel that enumerates the
	// same fields by hand is the drift §7s exists to close -- in the panel
	// rather than on disk, but the same shape of bug and just as silent.
	bool DrawFields(const std::vector<FieldDesc>& fields, void* block);
}
