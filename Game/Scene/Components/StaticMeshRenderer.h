#pragma once 

#include "Utility/ComponentRestraint.h"
#include "Game/Model/Model.h"

namespace Game {
	Component(StaticMeshRenderer)
		Model* modelNode{ nullptr };
	EndComponent(StaticMeshRenderer)
}

