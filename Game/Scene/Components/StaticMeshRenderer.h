#pragma once 

#include "Utility/ComponentRestraint.h"
#include "Game/Base/Common.h"

namespace Game {
	Component(StaticMeshRenderer)
		Interface::IModelNode* modelNode{ nullptr };
	EndComponent(StaticMeshRenderer)
}
