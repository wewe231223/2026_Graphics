#pragma once 
#include "Utility/DirectXInclude.h"

struct ModelContext {
    SimpleMath::Matrix world;      
    SimpleMath::Matrix prevWorld;  

    DirectX::XMFLOAT4 bbCenter;    
    DirectX::XMFLOAT4 bbExtents;   

    uint32_t material;
    uint32_t flags;
    uint32_t boneIndexStart;
    uint32_t objectID;             

    DirectX::XMFLOAT4 custom0;     
    DirectX::XMFLOAT4 custom1;     
};

static_assert(sizeof(ModelContext) % 16 == 0);


// 가나다라마사사
