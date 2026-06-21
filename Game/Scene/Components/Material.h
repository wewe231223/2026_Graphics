#pragma once
#include <cstdint>
#include "Utility/ComponentRestraint.h"

namespace Game {
	/*
	MaterialGroup Index 체계
	
	Model(SubMesh)
	    └─ MaterialGroupItemIndex (그룹 내부 인덱스)
	           │
	           ▼
	RegisteredMaterialGroup.Items[MaterialGroupItemIndex]
	    ├─ Pipeline
	    └─ MaterialIndex (전역 Material 배열 인덱스)
	           │
	           ▼
	RenderFrameData.DrawRecord.mMaterialIndex
	*/
    ComponentDecl(
        Material,
        ComponentFields(
            ComponentField(std::uint32_t, MaterialGroupIndex, 0)
            ComponentField(std::uint32_t, Flags, 0)
        ),
        BOOST_PP_SEQ_NIL
    );
}
