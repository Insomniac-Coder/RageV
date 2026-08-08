#include <rvpch.h>
#include "Entity.h"
#include "Components.h"

namespace RageV
{
	UUID Entity::GetUUID() const
	{
		return GetComponent<IDComponent>().ID;
	}

	const std::string& Entity::GetName() const
	{
		return GetComponent<TagComponent>().Name;
	}
}
