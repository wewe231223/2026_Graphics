#pragma once 

namespace Constants {
	template<typename T>
	constexpr T FrameCount = static_cast<T>(3); 

	constexpr bool AllowTearing = false; 

	constexpr const char* FontPath = "Resources/Font/NotoSansKR-Regular-Hestia.otf";
}
