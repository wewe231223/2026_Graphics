#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#ifdef max
#undef max
#endif
#include "Arche/Common.h"
#include "sol/forward.hpp"
#include "Utility/ComponentRestraint.h"

namespace Arche {
    class World;
}

namespace Script {
    class LuaBehaviorFramework final {

    public:
        enum class BehaviorErrorCode {
            None,
            WorldNotInitialized,
            BehaviorAlreadyAttached,
            BehaviorNotFound,
            BehaviorSourceReadFailed,
            LuaLoadFailed,
            LuaRuntimeFailed,
            InvalidBehaviorPath,
            RuntimeStateMismatch,
            InvalidFixedDeltaSeconds,
            Unknown
        };

        struct BehaviorError final {
            BehaviorErrorCode mCode{ BehaviorErrorCode::None };
            std::string mMessage{};
            Arche::EntityID mEntity{};
            std::string mBehaviorFilePath{};
        };

        struct BehaviorOperationResult final {
            bool mIsSuccess{ false };
            BehaviorError mError{};

            static BehaviorOperationResult Success();
            static BehaviorOperationResult Failure(BehaviorErrorCode Code, const std::string& Message, Arche::EntityID Entity, const std::string& BehaviorFilePath);
            explicit operator bool() const;
        };

    public:
        class BehaviorContext final {
        public:
            BehaviorContext();
            ~BehaviorContext();
            BehaviorContext(const BehaviorContext& Other) = delete;
            BehaviorContext& operator=(const BehaviorContext& Other) = delete;
            BehaviorContext(BehaviorContext&& Other) noexcept;
            BehaviorContext& operator=(BehaviorContext&& Other) noexcept;

        public:
            void Bind(Arche::World* TargetWorld, Arche::EntityID OwnerEntity, LuaBehaviorFramework* OwnerFramework);
            Arche::EntityID GetEntityId() const;

            template <TrivialComponent T>
            T* GetComponent();

            template <TrivialComponent T>
            const T* GetComponent() const;

            bool HasComponent(const std::string& ComponentName) const;
            sol::object GetComponent(const std::string& ComponentName);

        private:
            Arche::World* mWorld{};
            Arche::EntityID mOwnerEntity{};
            LuaBehaviorFramework* mOwnerFramework{};
        };

    private:
        struct RuntimeBehaviorInstance;
        struct Impl;

    public:
        static constexpr std::uint32_t InvalidBehaviorInstanceId{ std::numeric_limits<std::uint32_t>::max() };

        LuaBehaviorFramework();
        ~LuaBehaviorFramework();
        LuaBehaviorFramework(const LuaBehaviorFramework& Other) = delete;
        LuaBehaviorFramework& operator=(const LuaBehaviorFramework& Other) = delete;
        LuaBehaviorFramework(LuaBehaviorFramework&& Other) noexcept = delete;
        LuaBehaviorFramework& operator=(LuaBehaviorFramework&& Other) noexcept = delete;

    public:
        void Initialize(Arche::World* TargetWorld);
        void OpenDefaultLibraries();

        BehaviorOperationResult SetFixedUpdateInterval(float FixedDeltaSeconds);

        template <TrivialComponent T>
        void RegisterComponent(const std::string& ComponentName);

        template <TrivialComponent T>
        requires HasLuaComponentDefinition<T>
        void RegisterComponentByDefinition();

        template <typename T>
        requires HasLuaTypeDefinition<T>
        void RegisterTypeByDefinition();

        template <typename T, typename... TArgs>
        void RegisterComponentUsertype(const std::string& ComponentName, TArgs&&... UsertypeArguments);

        template <typename T, typename... TArgs>
        void RegisterTypeUsertype(const std::string& TypeName, TArgs&&... UsertypeArguments);

        BehaviorOperationResult AttachBehavior(Arche::EntityID TargetEntity, const std::string& BehaviorSource);
        BehaviorOperationResult AttachBehaviorFromFile(Arche::EntityID TargetEntity, const std::string& BehaviorFilePath);
        BehaviorOperationResult HotReloadBehavior(const std::string& BehaviorFileName);
        BehaviorOperationResult DisableBehavior(Arche::EntityID TargetEntity);
        BehaviorOperationResult DestroyBehavior(Arche::EntityID TargetEntity);

        void DetachBehavior(Arche::EntityID TargetEntity);
        void Update(float DeltaSeconds);
        void FixedUpdate(float FixedDeltaSeconds);
        void LateUpdate(float DeltaSeconds);

        const BehaviorError& GetLastError() const;

        sol::state& GetState();
        const sol::state& GetState() const;

    private:
        RuntimeBehaviorInstance* FindRuntimeInstance(Arche::EntityID TargetEntity);
        const RuntimeBehaviorInstance* FindRuntimeInstance(Arche::EntityID TargetEntity) const;
        bool IsValidBehaviorInstanceId(std::uint32_t BehaviorInstanceId) const;
        std::uint32_t GenerateBehaviorInstanceId();
        std::uint32_t GenerateBehaviorAssetId();
        BehaviorOperationResult ReadBehaviorSourceFromFilePath(const std::string& BehaviorFilePath, std::string& OutBehaviorSource) const;

        BehaviorOperationResult HandleFailure(BehaviorErrorCode Code, const std::string& Message, Arche::EntityID Entity, const std::string& BehaviorFilePath);
        void ClearError();
        void ResetBehaviorComponent(Arche::EntityID TargetEntity, std::uint32_t BehaviorAssetId);

        void StartFixedUpdateThread();
        void StopFixedUpdateThread();
        void FixedUpdateThreadMain();
        void BindContextToEnvironment(RuntimeBehaviorInstance& TargetInstance);
        void ResolveBehaviorEntries(RuntimeBehaviorInstance& TargetInstance);

        template <typename... TArgs>
        BehaviorOperationResult RunEntryWithArguments(sol::protected_function& EntryFunction, Arche::EntityID TargetEntity, const std::string& BehaviorFilePath, TArgs&&... Arguments);

    private:
        Arche::World* mWorld{};
        std::unique_ptr<sol::state> mLuaState{};
        std::unique_ptr<Impl> mImpl{};

        float mFixedDeltaSeconds{ 1.0f };
        std::thread mFixedUpdateThread{};
        std::atomic<bool> mIsFixedUpdateThreadRunning{ false };
        std::condition_variable mFixedUpdateCondition{};

        std::mutex mRuntimeMutex{};
        std::recursive_mutex mWorldFlowMutex{};

        BehaviorError mLastError{};
    };
}
