#include <rvpch.h>
#include "Entity.h"
#include "Components.h"

namespace RageV
{
	UUID Entity::GetUUID()
	{
		return GetComponent<IDComponent>().ID;
	}

	const std::string& Entity::GetName()
	{
		return GetComponent<TagComponent>().Name;
	}
}
