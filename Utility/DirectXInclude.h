#pragma once 
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXColors.h>
#include <DirectXCollision.h>
#include <comdef.h>
#include <dxgidebug.h>
#include <d3dcompiler.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <d2d1_3.h>
#include <d3d11on12.h>
#include <dxcapi.h>
#include "External/Include/DirectXTK12/d3dx12.h"
#include "External/Include/DirectXTK12/SimpleMath.h"
#include "External/Include/DirectXTK12/SimpleMath.inl"
#include "External/Include/DirectXTEX/DirectXTex.h"

// DirectX12 Library
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxcompiler.lib")

#pragma comment(lib, "DirectXTex.lib")

#pragma comment(lib, "DirectXTK12.lib")

using namespace Microsoft::WRL;
namespace SimpleMath = DirectX::SimpleMath;

using ID3D12Blob = ID3D10Blob;


