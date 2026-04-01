#pragma once

#include <memory>

namespace sol {
	class state;
}

namespace Script {
	class ScriptCore {
	public:
		ScriptCore();
		~ScriptCore();
		ScriptCore(const ScriptCore& Other) = delete;
		ScriptCore& operator=(const ScriptCore& Other) = delete;
		ScriptCore(ScriptCore&& Other) noexcept;
		ScriptCore& operator=(ScriptCore&& Other) noexcept;

	public:
		bool Initialize();
		void Shutdown();

		bool IsInitialized() const;
		sol::state* GetState();
		const sol::state* GetState() const;

	private:
		void ResetState();

	private:
		bool mIsInitialized;
		std::unique_ptr<sol::state> mLuaState;
	};
}
