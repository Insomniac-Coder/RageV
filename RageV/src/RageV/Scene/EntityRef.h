#pragma once
#include "RageV/Core/UUID.h"

#include <cstdint>

namespace RageV
{
	// A reference from one component to another entity.
	//
	// **A distinct type rather than a bare UUID**, because reflection deduces a
	// field's kind from its member type and `AssetHandle` is *already* an alias
	// for UUID -- so a second alias would be indistinguishable from an asset,
	// and every entity slot in the inspector would come up with the content
	// browser's drop target instead of the hierarchy's. The wrapper is what
	// makes `FieldType::Entity` deducible at all.
	//
	// It stores the UUID and never an `ECS::Entity`. A handle is an index into
	// one scene's pool: it means nothing after a save, after a reload, or after
	// the snapshot restore that Play and Stop perform every single time. The
	// UUID is the reference that survives all three, which is why it is what
	// the scene file has always stored.
	//
	// Dangling is *expected*, not exceptional -- the target may be destroyed
	// mid-play while this still names it. So resolving one is
	// `Scene::GetEntityByUUID`, which answers an invalid Entity rather than
	// asserting, and every caller tests it.
	struct EntityRef
	{
		UUID Value = UUID::Invalid();

		constexpr EntityRef() = default;
		constexpr EntityRef(UUID value) : Value(value) {}

		constexpr bool IsValid() const { return Value.IsValid(); }

		// For the places that store or compare it as a number: the serializer,
		// the undo stack's variant, and the C# bridge's text form.
		constexpr explicit operator uint64_t() const { return (uint64_t)Value; }

		friend constexpr bool operator==(const EntityRef& a, const EntityRef& b)
		{
			return (uint64_t)a.Value == (uint64_t)b.Value;
		}

		friend constexpr bool operator!=(const EntityRef& a, const EntityRef& b)
		{
			return !(a == b);
		}
	};
}
