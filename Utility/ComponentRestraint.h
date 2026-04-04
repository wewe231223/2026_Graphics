/*
+----------------------------------------------------------------------------------------------+
|                                  ComponentRestraint 명세
+----------------------------------------------------------------------------------------------+
| [목적]
| - C++ 컴포넌트/타입을 선언하고 Lua 스크립트에서 자동 바인딩 정보로 활용하기 위한 공용 매크로
|   와 트레잇 정의를 제공한다.
|
| [컴포넌트 정의 절차: ComponentDecl]
| 1) ComponentDecl(TypeName, FieldsSeq, MethodsSeq) 형태로 선언한다.
| 2) FieldsSeq 는 ComponentFields( ComponentField(Type, Name) ... ) 로 작성한다.
| 3) MethodsSeq 는 ComponentMethods( ComponentMethod(Signature, Name) ... ) 로 작성한다.
| 4) 메서드가 없으면 MethodsSeq 에 BOOST_PP_SEQ_NIL 을 전달한다.
| 5) 선언된 TypeName 은 TrivialComponent 조건(복사 가능/소멸 가능/표준 레이아웃)을 만족해야
|    하며, 위반 시 static_assert 로 빌드 오류가 발생한다.
| 6) ComponentDecl 실행 시 LuaComponentDefinitionTraits<TypeName>::Create() 가 자동 생성된다.
|
| [이미 정의된 타입을 스크립트에서 사용하기 위한 절차: LuaTypeDefinitionDecl]
| 1) 타입 구조체/클래스를 기존 코드에서 먼저 정의한다.
| 2) LuaTypeDefinitionDecl(TypeName, FieldsSeq, MethodsSeq) 또는
|    LuaTypeDefinitionDeclWithName(TypeName, "LuaName", FieldsSeq, MethodsSeq) 를 선언한다.
| 3) FieldsSeq/MethodsSeq 작성 규칙은 ComponentDecl 과 동일하다.
| 4) 선언 후 LuaTypeDefinitionTraits<TypeName>::Create() 로 이름/필드/메서드 바인딩 정보를
|    가져와 스크립트 등록 파이프라인에서 사용한다.
|
| [빠른 사용 예시]
| ComponentDecl(Transform,
|     ComponentFields(ComponentField(float, mX) ComponentField(float, mY)),
|     ComponentMethods(ComponentMethod(float GetLengthSquared() const, GetLengthSquared)));
|
| struct Vec2 { float mX{}; float mY{}; };
| LuaTypeDefinitionDecl(Vec2,
|     ComponentFields(ComponentField(float, mX) ComponentField(float, mY)),
|     BOOST_PP_SEQ_NIL);
|
| [참고]
| - 멤버 포인터 정보는 std::pair<const char*, decltype(&Type::Member)> 형태로 수집된다.
| - 최종 정보 컨테이너는 tuple 기반이며 constexpr Create() 로 컴파일 타임 생성이 가능하다.
+----------------------------------------------------------------------------------------------+
*/
#pragma once
#include <concepts>
#include <tuple>
#include <utility>
#include <vector>
#include <type_traits>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/seq.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>

namespace Game {
    struct ComponentInspectionField;
}

template <typename T>
concept TrivialComponent =
std::is_trivially_copyable_v<T> &&
std::is_trivially_destructible_v<T> &&
std::is_standard_layout_v<T>;

template <typename TComponent, typename... TMemberPointers>
struct LuaComponentDefinition final {
    using ComponentType = TComponent;
    using FieldTuple = std::tuple<std::pair<const char*, TMemberPointers>...>;

    const char* mTypeName{};
    FieldTuple mFieldBindings{};
};

template <typename TComponent, typename... TMemberPointers>
constexpr LuaComponentDefinition<TComponent, TMemberPointers...> MakeLuaComponentDefinition(const char* TypeName, std::pair<const char*, TMemberPointers>... FieldBindings) {
    LuaComponentDefinition<TComponent, TMemberPointers...> Definition{};
    Definition.mTypeName = TypeName;
    Definition.mFieldBindings = typename LuaComponentDefinition<TComponent, TMemberPointers...>::FieldTuple{ FieldBindings... };
    return Definition;
}

template <typename TComponent>
struct LuaComponentDefinitionTraits;

template <typename TComponent>
concept HasLuaComponentDefinition = requires {
    { LuaComponentDefinitionTraits<TComponent>::Create() };
};

template <typename TType, typename... TMemberPointers>
struct LuaTypeDefinition final {
    using ValueType = TType;
    using BindingTuple = std::tuple<std::pair<const char*, TMemberPointers>...>;

    const char* mTypeName{};
    BindingTuple mBindings{};
};

template <typename TType, typename... TMemberPointers>
constexpr LuaTypeDefinition<TType, TMemberPointers...> MakeLuaTypeDefinition(const char* TypeName, std::pair<const char*, TMemberPointers>... Bindings) {
    LuaTypeDefinition<TType, TMemberPointers...> Definition{};
    Definition.mTypeName = TypeName;
    Definition.mBindings = typename LuaTypeDefinition<TType, TMemberPointers...>::BindingTuple{ Bindings... };
    return Definition;
}

template <typename TType>
struct LuaTypeDefinitionTraits;

template <typename TType>
concept HasLuaTypeDefinition = requires {
    { LuaTypeDefinitionTraits<TType>::Create() };
};

#define COMPONENT_DECL_DECLARE_FIELD_HELPER(R, Data, Element) BOOST_PP_TUPLE_ELEM(2, 0, Element) BOOST_PP_TUPLE_ELEM(2, 1, Element){};
#define COMPONENT_DECL_DECLARE_METHOD_HELPER(R, Data, Element) BOOST_PP_TUPLE_ELEM(2, 0, Element);
#define COMPONENT_DECL_APPEND_FIELD_BINDING_HELPER(R, TypeName, Element) , std::pair<const char*, decltype(&TypeName::BOOST_PP_TUPLE_ELEM(2, 1, Element))>{ BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(2, 1, Element)), &TypeName::BOOST_PP_TUPLE_ELEM(2, 1, Element) }
#define COMPONENT_DECL_APPEND_METHOD_BINDING_HELPER(R, TypeName, Element) , std::pair<const char*, decltype(&TypeName::BOOST_PP_TUPLE_ELEM(2, 1, Element))>{ BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(2, 1, Element)), &TypeName::BOOST_PP_TUPLE_ELEM(2, 1, Element) }
#define ComponentField(Type, Name) ((Type, Name))
#define ComponentMethod(Signature, Name) ((Signature, Name))
#define ComponentFields(...) __VA_ARGS__
#define ComponentMethods(...) __VA_ARGS__
#define LUA_TYPE_APPEND_FIELD_BINDING_HELPER(R, TypeName, Element) , std::pair<const char*, decltype(&TypeName::BOOST_PP_TUPLE_ELEM(2, 1, Element))>{ BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(2, 1, Element)), &TypeName::BOOST_PP_TUPLE_ELEM(2, 1, Element) }
#define LUA_TYPE_APPEND_METHOD_BINDING_HELPER(R, TypeName, Element) , std::pair<const char*, decltype(&TypeName::BOOST_PP_TUPLE_ELEM(2, 1, Element))>{ BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(2, 1, Element)), &TypeName::BOOST_PP_TUPLE_ELEM(2, 1, Element) }

#define ComponentDecl(TypeName, FieldsSeq, MethodsSeq) \
struct TypeName { \
    BOOST_PP_SEQ_FOR_EACH(COMPONENT_DECL_DECLARE_FIELD_HELPER, _, FieldsSeq) \
    static const char* GetComponentInspectionName(); \
    void BuildComponentInspectionFields(std::vector<Game::ComponentInspectionField>& OutFields) const; \
    BOOST_PP_SEQ_FOR_EACH(COMPONENT_DECL_DECLARE_METHOD_HELPER, _, MethodsSeq) \
}; \
static_assert(TrivialComponent<TypeName>, \
"\n\n[FATAL ERROR] Invalid Component Layout: " #TypeName \
"\n- Reason: All components must be Trivially Copyable and have Standard Layout.\n"); \
template <> \
struct LuaComponentDefinitionTraits<TypeName> final { \
    static constexpr auto Create() { \
        return MakeLuaComponentDefinition<TypeName>(BOOST_PP_STRINGIZE(TypeName) BOOST_PP_SEQ_FOR_EACH(COMPONENT_DECL_APPEND_FIELD_BINDING_HELPER, TypeName, FieldsSeq) BOOST_PP_SEQ_FOR_EACH(COMPONENT_DECL_APPEND_METHOD_BINDING_HELPER, TypeName, MethodsSeq)); \
    } \
}

#define LuaTypeDefinitionDeclWithName(TypeName, LuaTypeName, FieldsSeq, MethodsSeq) \
template <> \
struct LuaTypeDefinitionTraits<TypeName> final { \
    static constexpr auto Create() { \
        return MakeLuaTypeDefinition<TypeName>(LuaTypeName BOOST_PP_SEQ_FOR_EACH(LUA_TYPE_APPEND_FIELD_BINDING_HELPER, TypeName, FieldsSeq) BOOST_PP_SEQ_FOR_EACH(LUA_TYPE_APPEND_METHOD_BINDING_HELPER, TypeName, MethodsSeq)); \
    } \
}

#define LuaTypeDefinitionDecl(TypeName, FieldsSeq, MethodsSeq) LuaTypeDefinitionDeclWithName(TypeName, BOOST_PP_STRINGIZE(TypeName), FieldsSeq, MethodsSeq)
