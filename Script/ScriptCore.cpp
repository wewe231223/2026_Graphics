#include "ScriptCore.h"

#include <utility>

#include <sol/sol.hpp>

using namespace Script;

ScriptCore::ScriptCore()
	: mIsInitialized { false },
	  mLuaState {} {
}

ScriptCore::~ScriptCore() {
	Shutdown();
}

ScriptCore::ScriptCore(ScriptCore&& Other) noexcept
	: mIsInitialized { Other.mIsInitialized },
	  mLuaState { std::move(Other.mLuaState) } {
	Other.mIsInitialized = false;
}

ScriptCore& ScriptCore::operator=(ScriptCore&& Other) noexcept {
	if (this != &Other) {
		mIsInitialized = Other.mIsInitialized;
		mLuaState = std::move(Other.mLuaState);

		Other.mIsInitialized = false;
	}

	return *this;
}

bool ScriptCore::Initialize() {
	if (mIsInitialized == true) {
		return true;
	}

	mLuaState = std::make_unique<sol::state>();
	mLuaState->open_libraries(sol::lib::base, sol::lib::package, sol::lib::coroutine, sol::lib::string, sol::lib::os, sol::lib::math, sol::lib::table, sol::lib::debug, sol::lib::utf8);

	mIsInitialized = true;

	return true;
}

void ScriptCore::Shutdown() {
	if (mIsInitialized == false) {
		return;
	}

	ResetState();
	mIsInitialized = false;
}

bool ScriptCore::IsInitialized() const {
	return mIsInitialized;
}

sol::state* ScriptCore::GetState() {
	return mLuaState.get();
}

const sol::state* ScriptCore::GetState() const {
	return mLuaState.get();
}

void ScriptCore::ResetState() {
	mLuaState.reset();
}
