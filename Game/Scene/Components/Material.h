#pragma once 
#include "Utility/ComponentRestraint.h"
#include "Game/Base/Common.h"

namespace Game {
	Component(Material)
		uint32_t MaterialIndex{ 0 };
		Interface::IPipeline* Pipeline{ nullptr };
	EndComponent(Material)
}
