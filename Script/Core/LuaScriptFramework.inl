#pragma once

namespace Script {
    template <typename TUsertype, typename TBindings, std::size_t... TIndices>
    void ApplyComponentBindingsToUsertypeImpl(TUsertype& TargetUsertype, const TBindings& TargetBindings, std::index_sequence<TIndices...>) {
        (static_cast<void>(TargetUsertype[std::get<TIndices>(TargetBindings).first] = std::get<TIndices>(TargetBindings).second), ...);
    }

    template <typename TDefinition, std::size_t... TIndices>
    void RegisterComponentUsertypeByDefinitionImpl(sol::state& LuaState, const TDefinition& Definition, std::index_sequence<TIndices...>) {
        auto Usertype{ LuaState.new_usertype<typename TDefinition::ComponentType>(Definition.mTypeName, sol::constructors<typename TDefinition::ComponentType()>()) };
        ApplyComponentBindingsToUsertypeImpl(Usertype, Definition.mFieldBindings, std::index_sequence<TIndices...>{});
    }

    template <typename TDefinition, std::size_t... TIndices>
    void RegisterTypeUsertypeByDefinitionImpl(sol::state& LuaState, const TDefinition& Definition, std::index_sequence<TIndices...>) {
        auto Usertype{ LuaState.new_usertype<typename TDefinition::ValueType>(Definition.mTypeName, sol::constructors<typename TDefinition::ValueType()>()) };
        ApplyComponentBindingsToUsertypeImpl(Usertype, Definition.mBindings, std::index_sequence<TIndices...>{});
    }

    template <typename TElement, std::size_t TElementCount>
    void RegisterArrayTypeUsertypeByDefinitionImpl(sol::state& LuaState, const LuaArrayTypeDefinition<TElement, TElementCount>& Definition) {
        LuaState.new_usertype<std::array<TElement, TElementCount>>(
            Definition.mTypeName,
            sol::constructors<std::array<TElement, TElementCount>()>(),
            sol::meta_function::index,
            [](const std::array<TElement, TElementCount>& TargetArray, std::size_t LuaIndex) -> TElement {
                if (LuaIndex >= TElementCount) {
                    return TElement{};
                }

                return TargetArray[LuaIndex];
            },
            sol::meta_function::new_index,
            [](std::array<TElement, TElementCount>& TargetArray, std::size_t LuaIndex, const TElement& Value) {
                if (LuaIndex >= TElementCount) {
                    return;
                }

                TargetArray[LuaIndex] = Value;
            },
            "Get",
            [](const std::array<TElement, TElementCount>& TargetArray, std::size_t Index) -> TElement {
                if (Index >= TElementCount) {
                    return TElement{};
                }

                return TargetArray[Index];
            },
            "Set",
            [](std::array<TElement, TElementCount>& TargetArray, std::size_t Index, const TElement& Value) {
                if (Index >= TElementCount) {
                    return;
                }

                TargetArray[Index] = Value;
            },
            "Size",
            []() -> std::size_t {
                return static_cast<std::size_t>(TElementCount);
            }
        );
    }

    template <TrivialComponent T>
    T* LuaBehaviorFramework::BehaviorContext::GetComponent() {
        if (mWorld == nullptr || mOwnerFramework == nullptr) {
            return nullptr;
        }

        std::lock_guard<std::recursive_mutex> WorldFlowLock{ mOwnerFramework->mWorldFlowMutex };
        return mWorld->GetComponent<T>(mOwnerEntity);
    }

    template <TrivialComponent T>
    const T* LuaBehaviorFramework::BehaviorContext::GetComponent() const {
        if (mWorld == nullptr || mOwnerFramework == nullptr) {
            return nullptr;
        }

        std::lock_guard<std::recursive_mutex> WorldFlowLock{ mOwnerFramework->mWorldFlowMutex };
        return mWorld->GetComponent<T>(mOwnerEntity);
    }

    template <TrivialComponent T>
    void LuaBehaviorFramework::RegisterComponent(const std::string& ComponentName) {
        mComponentGetters[ComponentName] = [](sol::state_view TargetState, Arche::World& TargetWorld, Arche::EntityID TargetEntity) {
            T* TargetComponent{ TargetWorld.GetComponent<T>(TargetEntity) };

            if (TargetComponent == nullptr) {
                return sol::make_object(TargetState, sol::lua_nil);
            }

            return sol::make_object(TargetState, std::ref(*TargetComponent));
        };
    }

    template <TrivialComponent T>
    requires HasLuaComponentDefinition<T>
    void LuaBehaviorFramework::RegisterComponentByDefinition() {
        constexpr auto Definition{ LuaComponentDefinitionTraits<T>::Create() };
        RegisterComponentUsertypeByDefinitionImpl(mLuaState, Definition, std::make_index_sequence<std::tuple_size_v<typename decltype(Definition)::FieldTuple>>{});
        RegisterComponent<T>(Definition.mTypeName);
    }

    template <typename T>
    requires HasLuaTypeDefinition<T>
    void LuaBehaviorFramework::RegisterTypeByDefinition() {
        constexpr auto Definition{ LuaTypeDefinitionTraits<T>::Create() };

        if constexpr (requires { typename decltype(Definition)::BindingTuple; }) {
            RegisterTypeUsertypeByDefinitionImpl(mLuaState, Definition, std::make_index_sequence<std::tuple_size_v<typename decltype(Definition)::BindingTuple>>{});
        }
        else {
            RegisterArrayTypeUsertypeByDefinitionImpl(mLuaState, Definition);
        }
    }

    template <typename T, typename... TArgs>
    void LuaBehaviorFramework::RegisterComponentUsertype(const std::string& ComponentName, TArgs&&... UsertypeArguments) {
        mLuaState.new_usertype<T>(ComponentName, std::forward<TArgs>(UsertypeArguments)...);
    }

    template <typename T, typename... TArgs>
    void LuaBehaviorFramework::RegisterTypeUsertype(const std::string& TypeName, TArgs&&... UsertypeArguments) {
        mLuaState.new_usertype<T>(TypeName, std::forward<TArgs>(UsertypeArguments)...);
    }

    template <typename... TArgs>
    LuaBehaviorFramework::BehaviorOperationResult LuaBehaviorFramework::RunEntryWithArguments(sol::protected_function& EntryFunction, Arche::EntityID TargetEntity, const std::string& BehaviorFilePath, TArgs&&... Arguments) {
        if (!EntryFunction.valid()) {
            return BehaviorOperationResult::Success();
        }

        sol::protected_function_result EntryResult{ EntryFunction(std::forward<TArgs>(Arguments)...) };

        if (!EntryResult.valid()) {
            return HandleFailure(BehaviorErrorCode::LuaRuntimeFailed, std::string{ EntryResult.get<sol::error>().what() }, TargetEntity, BehaviorFilePath);
        }

        return BehaviorOperationResult::Success();
    }
}
