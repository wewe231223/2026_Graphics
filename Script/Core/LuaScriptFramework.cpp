#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "Arche/World.h"
#include "Game/Scene/Components/ScriptComponent.h"
#include "LuaScriptFramework.h"

namespace Script {
    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::BehaviorOperationResult::Success() {
        BehaviorOperationResult Result{};
        Result.mIsSuccess = true;
        return Result;
    }

    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::BehaviorOperationResult::Failure(BehaviorErrorCode Code, const std::string& Message, Arche::EntityID Entity, const std::string& BehaviorFilePath) {
        BehaviorOperationResult Result{};
        Result.mIsSuccess = false;
        Result.mError.mCode = Code;
        Result.mError.mMessage = Message;
        Result.mError.mEntity = Entity;
        Result.mError.mBehaviorFilePath = BehaviorFilePath;
        return Result;
    }

    LuaBehaviorFramework::BehaviorOperationResult::operator bool() const {
        return mIsSuccess;
    }

    LuaBehaviorFramework::BehaviorContext::BehaviorContext()
        : mWorld{},
          mOwnerEntity{},
          mOwnerFramework{} {
    }

    LuaBehaviorFramework::BehaviorContext::~BehaviorContext() {
    }

    LuaBehaviorFramework::BehaviorContext::BehaviorContext(BehaviorContext&& Other) noexcept
        : mWorld{ Other.mWorld },
          mOwnerEntity{ Other.mOwnerEntity },
          mOwnerFramework{ Other.mOwnerFramework } {
    }

    LuaBehaviorFramework::BehaviorContext& LuaBehaviorFramework::BehaviorContext::operator=(BehaviorContext&& Other) noexcept {
        if (this == &Other) {
            return *this;
        }

        mWorld = Other.mWorld;
        mOwnerEntity = Other.mOwnerEntity;
        mOwnerFramework = Other.mOwnerFramework;
        return *this;
    }

    void LuaBehaviorFramework::BehaviorContext::Bind(Arche::World* TargetWorld, Arche::EntityID OwnerEntity, LuaBehaviorFramework* OwnerFramework) {
        mWorld = TargetWorld;
        mOwnerEntity = OwnerEntity;
        mOwnerFramework = OwnerFramework;
    }

    Arche::EntityID LuaBehaviorFramework::BehaviorContext::GetEntityId() const {
        return mOwnerEntity;
    }

    bool LuaBehaviorFramework::BehaviorContext::HasComponent(const std::string& ComponentName) const {
        if (mOwnerFramework == nullptr || mWorld == nullptr) {
            return false;
        }

        std::lock_guard<std::recursive_mutex> WorldFlowLock{ mOwnerFramework->mWorldFlowMutex };
        const std::unordered_map<std::string, ComponentGetter>& Getters{ mOwnerFramework->mComponentGetters };
        auto GetterIt{ Getters.find(ComponentName) };

        if (GetterIt == Getters.end()) {
            return false;
        }

        sol::object Value{ GetterIt->second(mOwnerFramework->mLuaState.lua_state(), *mWorld, mOwnerEntity) };
        return Value.get_type() != sol::type::nil;
    }

    sol::object LuaBehaviorFramework::BehaviorContext::GetComponent(const std::string& ComponentName) {
        if (mOwnerFramework == nullptr || mWorld == nullptr) {
            return sol::object{};
        }

        std::lock_guard<std::recursive_mutex> WorldFlowLock{ mOwnerFramework->mWorldFlowMutex };
        std::unordered_map<std::string, ComponentGetter>& Getters{ mOwnerFramework->mComponentGetters };
        auto GetterIt{ Getters.find(ComponentName) };

        if (GetterIt == Getters.end()) {
            return sol::make_object(mOwnerFramework->mLuaState.lua_state(), sol::lua_nil);
        }

        return GetterIt->second(mOwnerFramework->mLuaState.lua_state(), *mWorld, mOwnerEntity);
    }

    LuaBehaviorFramework::LuaBehaviorFramework()
        : mWorld{},
          mLuaState{},
          mComponentGetters{},
          mBehaviorFilePaths{},
          mRuntimeInstances{},
          mLastIssuedBehaviorInstanceId{},
          mLastIssuedBehaviorAssetId{},
          mFixedDeltaSeconds{ 1.0f },
          mFixedUpdateThread{},
          mIsFixedUpdateThreadRunning{ false },
          mFixedUpdateCondition{},
          mRuntimeMutex{},
          mWorldFlowMutex{},
          mLastError{} {
    }

    LuaBehaviorFramework::~LuaBehaviorFramework() {
        StopFixedUpdateThread();
    }

    void LuaBehaviorFramework::Initialize(Arche::World* TargetWorld) {
        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };
        std::lock_guard<std::recursive_mutex> WorldFlowLock{ mWorldFlowMutex };
        mWorld = TargetWorld;
        mLuaState.new_usertype<BehaviorContext>("BehaviorContext", "GetEntityId", &BehaviorContext::GetEntityId, "GetComponent", static_cast<sol::object(BehaviorContext::*)(const std::string&)>(&BehaviorContext::GetComponent), "HasComponent", &BehaviorContext::HasComponent);
        StartFixedUpdateThread();
    }

    void LuaBehaviorFramework::OpenDefaultLibraries() {
        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };
        mLuaState.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::package);
    }

    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::SetFixedUpdateInterval(float FixedDeltaSeconds) {
        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };

        if (FixedDeltaSeconds <= 0.0f) {
            return HandleFailure(BehaviorErrorCode::InvalidFixedDeltaSeconds, "Fixed delta seconds must be greater than zero.", Arche::EntityID{}, std::string{});
        }

        mFixedDeltaSeconds = FixedDeltaSeconds;
        mFixedUpdateCondition.notify_all();
        ClearError();
        return BehaviorOperationResult::Success();
    }

    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::AttachBehavior(Arche::EntityID TargetEntity, const std::string& BehaviorSource) {
        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };
        std::lock_guard<std::recursive_mutex> WorldFlowLock{ mWorldFlowMutex };

        if (mWorld == nullptr) {
            return HandleFailure(BehaviorErrorCode::WorldNotInitialized, "World is not initialized.", TargetEntity, std::string{});
        }

        Game::BehaviorInstanceComponent* ExistingComponent{ mWorld->GetComponent<Game::BehaviorInstanceComponent>(TargetEntity) };

        if (ExistingComponent != nullptr && IsValidBehaviorInstanceId(ExistingComponent->mBehaviorInstanceId) && mRuntimeInstances.find(ExistingComponent->mBehaviorInstanceId) != mRuntimeInstances.end()) {
            return HandleFailure(BehaviorErrorCode::BehaviorAlreadyAttached, "Behavior is already attached to this entity.", TargetEntity, std::string{});
        }

        if (ExistingComponent == nullptr) {
            Game::BehaviorInstanceComponent NewComponent{};
            NewComponent.mOwnerEntity = TargetEntity;
            NewComponent.mBehaviorInstanceId = InvalidBehaviorInstanceId;
            mWorld->AddComponent<Game::BehaviorInstanceComponent>(TargetEntity, NewComponent);
            ExistingComponent = mWorld->GetComponent<Game::BehaviorInstanceComponent>(TargetEntity);
        }

        if (ExistingComponent == nullptr) {
            return HandleFailure(BehaviorErrorCode::RuntimeStateMismatch, "Behavior component could not be prepared for attachment.", TargetEntity, std::string{});
        }

        std::uint32_t BehaviorInstanceId{ GenerateBehaviorInstanceId() };
        ExistingComponent->mOwnerEntity = TargetEntity;
        ExistingComponent->mBehaviorInstanceId = BehaviorInstanceId;
        ExistingComponent->mBehaviorAssetId = GenerateBehaviorAssetId();

        RuntimeBehaviorInstance NewRuntimeInstance{};
        NewRuntimeInstance.mContext.Bind(mWorld, TargetEntity, this);
        NewRuntimeInstance.mEnvironment = sol::environment(mLuaState, sol::create, mLuaState.globals());
        BindContextToEnvironment(NewRuntimeInstance);

        sol::protected_function_result LoadResult{ mLuaState.safe_script(BehaviorSource, NewRuntimeInstance.mEnvironment, sol::script_pass_on_error) };

        if (!LoadResult.valid()) {
            ResetBehaviorComponent(TargetEntity, 0u);
            return HandleFailure(BehaviorErrorCode::LuaLoadFailed, std::string{ LoadResult.get<sol::error>().what() }, TargetEntity, std::string{});
        }

        ResolveBehaviorEntries(NewRuntimeInstance);
        BehaviorOperationResult AwakeResult{ RunEntryWithArguments(NewRuntimeInstance.mEntries.mAwake, TargetEntity, std::string{}, &NewRuntimeInstance.mContext) };

        if (!AwakeResult) {
            ResetBehaviorComponent(TargetEntity, 0u);
            return AwakeResult;
        }

        BehaviorOperationResult EnableResult{ RunEntryWithArguments(NewRuntimeInstance.mEntries.mOnEnable, TargetEntity, std::string{}, &NewRuntimeInstance.mContext) };

        if (!EnableResult) {
            ResetBehaviorComponent(TargetEntity, 0u);
            return EnableResult;
        }

        BehaviorOperationResult StartResult{ RunEntryWithArguments(NewRuntimeInstance.mEntries.mStart, TargetEntity, std::string{}, &NewRuntimeInstance.mContext) };

        if (!StartResult) {
            ResetBehaviorComponent(TargetEntity, 0u);
            return StartResult;
        }

        mRuntimeInstances.emplace(BehaviorInstanceId, std::move(NewRuntimeInstance));
        ClearError();
        return BehaviorOperationResult::Success();
    }

    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::AttachBehaviorFromFile(Arche::EntityID TargetEntity, const std::string& BehaviorFilePath) {
        std::string BehaviorSource{};
        BehaviorOperationResult ReadResult{ ReadBehaviorSourceFromFilePath(BehaviorFilePath, BehaviorSource) };

        if (!ReadResult) {
            return ReadResult;
        }

        BehaviorOperationResult AttachResult{ AttachBehavior(TargetEntity, BehaviorSource) };

        if (!AttachResult) {
            return AttachResult;
        }

        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };
        RuntimeBehaviorInstance* RuntimeInstance{ FindRuntimeInstance(TargetEntity) };

        if (RuntimeInstance == nullptr) {
            return HandleFailure(BehaviorErrorCode::RuntimeStateMismatch, "Runtime instance was not created for attached behavior.", TargetEntity, BehaviorFilePath);
        }

        std::filesystem::path FullPath{ BehaviorFilePath };
        std::string BehaviorFileName{ FullPath.filename().string() };
        RuntimeInstance->mBehaviorFileName = BehaviorFileName;
        mBehaviorFilePaths[BehaviorFileName] = BehaviorFilePath;
        ClearError();
        return BehaviorOperationResult::Success();
    }

    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::HotReloadBehavior(const std::string& BehaviorFileName) {
        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };
        auto PathIt{ mBehaviorFilePaths.find(BehaviorFileName) };

        if (PathIt == mBehaviorFilePaths.end()) {
            return HandleFailure(BehaviorErrorCode::InvalidBehaviorPath, "Behavior file name is not registered.", Arche::EntityID{}, BehaviorFileName);
        }

        std::string BehaviorSource{};
        BehaviorOperationResult ReadResult{ ReadBehaviorSourceFromFilePath(PathIt->second, BehaviorSource) };

        if (!ReadResult) {
            return ReadResult;
        }

        bool IsEveryReloadSucceeded{ true };
        std::size_t ReloadedCount{};

        for (auto& RuntimePair : mRuntimeInstances) {
            RuntimeBehaviorInstance& RuntimeInstance{ RuntimePair.second };

            if (RuntimeInstance.mBehaviorFileName != BehaviorFileName) {
                continue;
            }

            BehaviorOperationResult DisableResult{ RunEntryWithArguments(RuntimeInstance.mEntries.mOnDisable, RuntimeInstance.mContext.GetEntityId(), PathIt->second, &RuntimeInstance.mContext) };

            if (!DisableResult) {
                IsEveryReloadSucceeded = false;
                continue;
            }

            RuntimeInstance.mEnvironment = sol::environment(mLuaState, sol::create, mLuaState.globals());
            BindContextToEnvironment(RuntimeInstance);

            sol::protected_function_result LoadResult{ mLuaState.safe_script(BehaviorSource, RuntimeInstance.mEnvironment, sol::script_pass_on_error) };

            if (!LoadResult.valid()) {
                HandleFailure(BehaviorErrorCode::LuaLoadFailed, std::string{ LoadResult.get<sol::error>().what() }, RuntimeInstance.mContext.GetEntityId(), PathIt->second);
                IsEveryReloadSucceeded = false;
                continue;
            }

            ResolveBehaviorEntries(RuntimeInstance);
            BehaviorOperationResult AwakeResult{ RunEntryWithArguments(RuntimeInstance.mEntries.mAwake, RuntimeInstance.mContext.GetEntityId(), PathIt->second, &RuntimeInstance.mContext) };

            if (!AwakeResult) {
                IsEveryReloadSucceeded = false;
                continue;
            }

            BehaviorOperationResult EnableResult{ RunEntryWithArguments(RuntimeInstance.mEntries.mOnEnable, RuntimeInstance.mContext.GetEntityId(), PathIt->second, &RuntimeInstance.mContext) };

            if (!EnableResult) {
                IsEveryReloadSucceeded = false;
                continue;
            }

            BehaviorOperationResult StartResult{ RunEntryWithArguments(RuntimeInstance.mEntries.mStart, RuntimeInstance.mContext.GetEntityId(), PathIt->second, &RuntimeInstance.mContext) };

            if (!StartResult) {
                IsEveryReloadSucceeded = false;
                continue;
            }

            RuntimeInstance.mIsEnabled = true;
            ReloadedCount = ReloadedCount + 1;
        }

        if (ReloadedCount == 0 || !IsEveryReloadSucceeded) {
            return HandleFailure(BehaviorErrorCode::LuaRuntimeFailed, "One or more runtime instances failed during hot reload.", Arche::EntityID{}, PathIt->second);
        }

        ClearError();
        return BehaviorOperationResult::Success();
    }

    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::DisableBehavior(Arche::EntityID TargetEntity) {
        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };
        RuntimeBehaviorInstance* RuntimeInstance{ FindRuntimeInstance(TargetEntity) };

        if (RuntimeInstance == nullptr) {
            return HandleFailure(BehaviorErrorCode::BehaviorNotFound, "Behavior runtime instance was not found.", TargetEntity, std::string{});
        }

        if (!RuntimeInstance->mIsEnabled) {
            return HandleFailure(BehaviorErrorCode::BehaviorNotFound, "Behavior is already disabled.", TargetEntity, std::string{});
        }

        BehaviorOperationResult DisableResult{ RunEntryWithArguments(RuntimeInstance->mEntries.mOnDisable, TargetEntity, RuntimeInstance->mBehaviorFileName, &RuntimeInstance->mContext) };

        if (!DisableResult) {
            return DisableResult;
        }

        RuntimeInstance->mIsEnabled = false;
        ClearError();
        return BehaviorOperationResult::Success();
    }

    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::DestroyBehavior(Arche::EntityID TargetEntity) {
        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };
        std::lock_guard<std::recursive_mutex> WorldFlowLock{ mWorldFlowMutex };
        RuntimeBehaviorInstance* RuntimeInstance{ FindRuntimeInstance(TargetEntity) };

        if (RuntimeInstance == nullptr) {
            return HandleFailure(BehaviorErrorCode::BehaviorNotFound, "Behavior runtime instance was not found.", TargetEntity, std::string{});
        }

        if (RuntimeInstance->mIsEnabled) {
            BehaviorOperationResult DisableResult{ RunEntryWithArguments(RuntimeInstance->mEntries.mOnDisable, TargetEntity, RuntimeInstance->mBehaviorFileName, &RuntimeInstance->mContext) };

            if (!DisableResult) {
                return DisableResult;
            }

            RuntimeInstance->mIsEnabled = false;
        }

        BehaviorOperationResult DestroyResult{ RunEntryWithArguments(RuntimeInstance->mEntries.mOnDestroy, TargetEntity, RuntimeInstance->mBehaviorFileName, &RuntimeInstance->mContext) };

        if (!DestroyResult) {
            return DestroyResult;
        }

        Game::BehaviorInstanceComponent* InstanceComponent{ mWorld->GetComponent<Game::BehaviorInstanceComponent>(TargetEntity) };

        if (InstanceComponent == nullptr) {
            return HandleFailure(BehaviorErrorCode::RuntimeStateMismatch, "Behavior component was not found in ECS world.", TargetEntity, RuntimeInstance->mBehaviorFileName);
        }

        auto RuntimeIt{ mRuntimeInstances.find(InstanceComponent->mBehaviorInstanceId) };

        if (RuntimeIt == mRuntimeInstances.end()) {
            return HandleFailure(BehaviorErrorCode::RuntimeStateMismatch, "Behavior runtime map entry was not found.", TargetEntity, RuntimeInstance->mBehaviorFileName);
        }

        mRuntimeInstances.erase(RuntimeIt);
        ResetBehaviorComponent(TargetEntity, 0u);
        ClearError();
        return BehaviorOperationResult::Success();
    }

    void LuaBehaviorFramework::DetachBehavior(Arche::EntityID TargetEntity) {
        static_cast<void>(DestroyBehavior(TargetEntity));
    }

    void LuaBehaviorFramework::Update(float DeltaSeconds) {
        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };
        std::lock_guard<std::recursive_mutex> WorldFlowLock{ mWorldFlowMutex };

        for (auto& RuntimePair : mRuntimeInstances) {
            RuntimeBehaviorInstance& RuntimeInstance{ RuntimePair.second };

            if (!RuntimeInstance.mIsEnabled) {
                continue;
            }

            static_cast<void>(RunEntryWithArguments(RuntimeInstance.mEntries.mUpdate, RuntimeInstance.mContext.GetEntityId(), RuntimeInstance.mBehaviorFileName, &RuntimeInstance.mContext, DeltaSeconds));
        }
    }

    void LuaBehaviorFramework::FixedUpdate(float FixedDeltaSeconds) {
        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };
        std::lock_guard<std::recursive_mutex> WorldFlowLock{ mWorldFlowMutex };

        for (auto& RuntimePair : mRuntimeInstances) {
            RuntimeBehaviorInstance& RuntimeInstance{ RuntimePair.second };

            if (!RuntimeInstance.mIsEnabled) {
                continue;
            }

            RuntimeInstance.mFixedTick = RuntimeInstance.mFixedTick + 1;
            static_cast<void>(RunEntryWithArguments(RuntimeInstance.mEntries.mFixedUpdate, RuntimeInstance.mContext.GetEntityId(), RuntimeInstance.mBehaviorFileName, &RuntimeInstance.mContext, FixedDeltaSeconds, RuntimeInstance.mFixedTick));
        }
    }

    void LuaBehaviorFramework::LateUpdate(float DeltaSeconds) {
        std::lock_guard<std::mutex> RuntimeGuard{ mRuntimeMutex };
        std::lock_guard<std::recursive_mutex> WorldFlowLock{ mWorldFlowMutex };

        for (auto& RuntimePair : mRuntimeInstances) {
            RuntimeBehaviorInstance& RuntimeInstance{ RuntimePair.second };

            if (!RuntimeInstance.mIsEnabled) {
                continue;
            }

            static_cast<void>(RunEntryWithArguments(RuntimeInstance.mEntries.mLateUpdate, RuntimeInstance.mContext.GetEntityId(), RuntimeInstance.mBehaviorFileName, &RuntimeInstance.mContext, DeltaSeconds));
        }
    }

    const LuaBehaviorFramework::BehaviorError& LuaBehaviorFramework::GetLastError() const {
        return mLastError;
    }

    sol::state& LuaBehaviorFramework::GetState() {
        return mLuaState;
    }

    const sol::state& LuaBehaviorFramework::GetState() const {
        return mLuaState;
    }

    LuaBehaviorFramework::RuntimeBehaviorInstance* LuaBehaviorFramework::FindRuntimeInstance(Arche::EntityID TargetEntity) {
        if (mWorld == nullptr) {
            return nullptr;
        }

        Game::BehaviorInstanceComponent* InstanceComponent{ mWorld->GetComponent<Game::BehaviorInstanceComponent>(TargetEntity) };

        if (InstanceComponent == nullptr) {
            return nullptr;
        }

        if (!IsValidBehaviorInstanceId(InstanceComponent->mBehaviorInstanceId)) {
            return nullptr;
        }

        auto RuntimeIt{ mRuntimeInstances.find(InstanceComponent->mBehaviorInstanceId) };

        if (RuntimeIt == mRuntimeInstances.end()) {
            return nullptr;
        }

        return &RuntimeIt->second;
    }

    const LuaBehaviorFramework::RuntimeBehaviorInstance* LuaBehaviorFramework::FindRuntimeInstance(Arche::EntityID TargetEntity) const {
        return const_cast<LuaBehaviorFramework*>(this)->FindRuntimeInstance(TargetEntity);
    }

    bool LuaBehaviorFramework::IsValidBehaviorInstanceId(std::uint32_t BehaviorInstanceId) const {
        return BehaviorInstanceId != 0u && BehaviorInstanceId != InvalidBehaviorInstanceId;
    }

    std::uint32_t LuaBehaviorFramework::GenerateBehaviorInstanceId() {
        std::uint32_t NextBehaviorInstanceId{ mLastIssuedBehaviorInstanceId };

        do {
            NextBehaviorInstanceId = NextBehaviorInstanceId + 1u;
        } while (!IsValidBehaviorInstanceId(NextBehaviorInstanceId) || mRuntimeInstances.find(NextBehaviorInstanceId) != mRuntimeInstances.end());

        mLastIssuedBehaviorInstanceId = NextBehaviorInstanceId;
        return NextBehaviorInstanceId;
    }

    std::uint32_t LuaBehaviorFramework::GenerateBehaviorAssetId() {
        mLastIssuedBehaviorAssetId = mLastIssuedBehaviorAssetId + 1u;
        return mLastIssuedBehaviorAssetId;
    }

    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::ReadBehaviorSourceFromFilePath(const std::string& BehaviorFilePath, std::string& OutBehaviorSource) const {
        std::ifstream InputStream{ BehaviorFilePath, std::ios::in | std::ios::binary };

        if (!InputStream.is_open()) {
            return BehaviorOperationResult::Failure(BehaviorErrorCode::BehaviorSourceReadFailed, "Unable to open behavior file.", Arche::EntityID{}, BehaviorFilePath);
        }

        std::stringstream Buffer{};
        Buffer << InputStream.rdbuf();
        OutBehaviorSource = Buffer.str();
        return BehaviorOperationResult::Success();
    }

    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::HandleFailure(BehaviorErrorCode Code, const std::string& Message, Arche::EntityID Entity, const std::string& BehaviorFilePath) {
        mLastError.mCode = Code;
        mLastError.mMessage = Message;
        mLastError.mEntity = Entity;
        mLastError.mBehaviorFilePath = BehaviorFilePath;

        if (Code == BehaviorErrorCode::LuaLoadFailed) {
            std::exit(EXIT_FAILURE);
        }

        return BehaviorOperationResult::Failure(Code, Message, Entity, BehaviorFilePath);
    }

    void LuaBehaviorFramework::ClearError() {
        mLastError = BehaviorError{};
    }

    void LuaBehaviorFramework::ResetBehaviorComponent(Arche::EntityID TargetEntity, std::uint32_t BehaviorAssetId) {
        if (mWorld == nullptr) {
            return;
        }

        Game::BehaviorInstanceComponent* ExistingComponent{ mWorld->GetComponent<Game::BehaviorInstanceComponent>(TargetEntity) };

        if (ExistingComponent == nullptr) {
            return;
        }

        ExistingComponent->mOwnerEntity = TargetEntity;
        ExistingComponent->mBehaviorInstanceId = InvalidBehaviorInstanceId;
        ExistingComponent->mBehaviorAssetId = BehaviorAssetId;
    }

    void LuaBehaviorFramework::StartFixedUpdateThread() {
        if (mIsFixedUpdateThreadRunning.load()) {
            return;
        }

        mIsFixedUpdateThreadRunning.store(true);
        mFixedUpdateThread = std::thread{ &LuaBehaviorFramework::FixedUpdateThreadMain, this };
    }

    void LuaBehaviorFramework::StopFixedUpdateThread() {
        if (!mIsFixedUpdateThreadRunning.load()) {
            return;
        }

        mIsFixedUpdateThreadRunning.store(false);
        mFixedUpdateCondition.notify_all();

        if (mFixedUpdateThread.joinable()) {
            mFixedUpdateThread.join();
        }
    }

    void LuaBehaviorFramework::FixedUpdateThreadMain() {
        while (mIsFixedUpdateThreadRunning.load()) {
            float CurrentFixedDeltaSeconds{};

            {
                std::unique_lock<std::mutex> RuntimeLock{ mRuntimeMutex };
                CurrentFixedDeltaSeconds = mFixedDeltaSeconds;
            }

            FixedUpdate(CurrentFixedDeltaSeconds);

            std::unique_lock<std::mutex> WaitLock{ mRuntimeMutex };
            mFixedUpdateCondition.wait_for(WaitLock, std::chrono::duration<float>{ CurrentFixedDeltaSeconds }, [this]() { return !mIsFixedUpdateThreadRunning.load(); });
        }
    }

    void LuaBehaviorFramework::BindContextToEnvironment(RuntimeBehaviorInstance& TargetInstance) {
        TargetInstance.mEnvironment["This"] = &TargetInstance.mContext;
    }

    void LuaBehaviorFramework::ResolveBehaviorEntries(RuntimeBehaviorInstance& TargetInstance) {
        sol::object CandidateAwake{ TargetInstance.mEnvironment["Awake"] };
        sol::object CandidateOnEnable{ TargetInstance.mEnvironment["OnEnable"] };
        sol::object CandidateStart{ TargetInstance.mEnvironment["Start"] };
        sol::object CandidateUpdate{ TargetInstance.mEnvironment["Update"] };
        sol::object CandidateFixedUpdate{ TargetInstance.mEnvironment["FixedUpdate"] };
        sol::object CandidateLateUpdate{ TargetInstance.mEnvironment["LateUpdate"] };
        sol::object CandidateOnDisable{ TargetInstance.mEnvironment["OnDisable"] };
        sol::object CandidateOnDestroy{ TargetInstance.mEnvironment["OnDestroy"] };

        TargetInstance.mEntries.mAwake = CandidateAwake.get_type() == sol::type::function ? CandidateAwake.as<sol::protected_function>() : sol::protected_function{};
        TargetInstance.mEntries.mOnEnable = CandidateOnEnable.get_type() == sol::type::function ? CandidateOnEnable.as<sol::protected_function>() : sol::protected_function{};
        TargetInstance.mEntries.mStart = CandidateStart.get_type() == sol::type::function ? CandidateStart.as<sol::protected_function>() : sol::protected_function{};
        TargetInstance.mEntries.mUpdate = CandidateUpdate.get_type() == sol::type::function ? CandidateUpdate.as<sol::protected_function>() : sol::protected_function{};
        TargetInstance.mEntries.mFixedUpdate = CandidateFixedUpdate.get_type() == sol::type::function ? CandidateFixedUpdate.as<sol::protected_function>() : sol::protected_function{};
        TargetInstance.mEntries.mLateUpdate = CandidateLateUpdate.get_type() == sol::type::function ? CandidateLateUpdate.as<sol::protected_function>() : sol::protected_function{};
        TargetInstance.mEntries.mOnDisable = CandidateOnDisable.get_type() == sol::type::function ? CandidateOnDisable.as<sol::protected_function>() : sol::protected_function{};
        TargetInstance.mEntries.mOnDestroy = CandidateOnDestroy.get_type() == sol::type::function ? CandidateOnDestroy.as<sol::protected_function>() : sol::protected_function{};
    }
}
