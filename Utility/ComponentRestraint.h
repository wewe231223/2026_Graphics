/*
+--------------------------------------------------------------------------------------------------+
|                                  ComponentRestraint 사용 가이드 (최신)
+--------------------------------------------------------------------------------------------------+
| [이 파일이 하는 일]
| - C++ 타입 정의에서 필드/메서드 메타데이터를 추출해 Lua 등록 파이프라인이 사용할 수 있게 만든다.
| - 핵심 결과물은 아래 두 Traits 의 Create() 반환값이다.
|   - LuaComponentDefinitionTraits<T>::Create()
|   - LuaTypeDefinitionTraits<T>::Create()
|
| [언제 무엇을 쓰는가]
| 1) 새 컴포넌트를 선언하면서 동시에 Lua 바인딩 정보를 만들고 싶다.
|    -> ComponentDecl(TypeName, FieldsSeq, MethodsSeq) 사용
|
| 2) 이미 프로젝트에 존재하는 타입을 Lua에 노출하고 싶다.
|    -> LuaTypeDefinitionDecl 또는 LuaTypeDefinitionDeclWithName 사용
|
| [매크로 구성 요소]
| - ComponentField(Type, Name[, DefaultValue])
|   예) ComponentField(float, mX) / ComponentField(int, mCount, 10)
| - ComponentMethod(Signature, Name)
|   예) ComponentMethod(float GetLengthSquared() const, GetLengthSquared)
| - ComponentFields(...) / ComponentMethods(...)
|   여러 항목을 Boost Preprocessor Seq 형태로 묶을 때 사용
| - 메서드가 없으면 MethodsSeq 자리에 BOOST_PP_SEQ_NIL 사용
|
| [빠른 시작: 새 컴포넌트 선언]
| ComponentDecl(Transform,
|     ComponentFields(
|         ComponentField(float, mX, 0.0f)
|         ComponentField(float, mY, 0.0f)
|     ),
|     ComponentMethods(
|         ComponentMethod(float GetLengthSquared() const, GetLengthSquared)
|     ));
|
| 결과:
| - struct Transform 생성
| - LuaComponentDefinitionTraits<Transform>::Create() 자동 생성
| - TrivialComponent 제약 위반 시 static_assert 로 즉시 실패
|
| [빠른 시작: 기존 타입 바인딩]
| struct Vec2 {
|     float mX{};
|     float mY{};
| };
|
| LuaTypeDefinitionDecl(Vec2,
|     ComponentFields(
|         ComponentField(float, mX)
|         ComponentField(float, mY)
|     ),
|     BOOST_PP_SEQ_NIL);
|
| 또는 Lua 이름을 별도로 지정:
| LuaTypeDefinitionDeclWithName(Vec2, "Vector2",
|     ComponentFields(
|         ComponentField(float, mX)
|         ComponentField(float, mY)
|     ),
|     BOOST_PP_SEQ_NIL);
|
| [컴포넌트 정의 시 체크리스트]
| - 필드 이름과 실제 멤버 이름이 정확히 일치하는가
| - 메서드 시그니처와 실제 선언이 일치하는가 (const 포함)
| - 메서드가 없을 때 BOOST_PP_SEQ_NIL 을 전달했는가
| - ComponentDecl 대상 타입이 trivially copyable / trivially destructible / standard layout 인가
|
| [자주 발생하는 실수]
| - ComponentMethod 에서 Name 과 실제 멤버 함수 이름 불일치
| - const 멤버 함수를 non-const 시그니처로 작성
| - BOOST_PP_SEQ_NIL 대신 빈 괄호를 전달
| - 기본값 인자를 넣었지만 타입과 값이 맞지 않음
|
| [내부 표현 참고]
| - 각 바인딩 항목은 std::pair<const char*, decltype(&Type::Member)> 형태로 저장된다.
| - Create() 는 constexpr 이며 tuple 기반 정의 객체를 반환한다.
+--------------------------------------------------------------------------------------------------+
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

#define COMPONENT_DECL_DECLARE_FIELD_HELPER(R, Data, Element) BOOST_PP_TUPLE_ELEM(3, 0, Element) BOOST_PP_TUPLE_ELEM(3, 1, Element){ BOOST_PP_TUPLE_ELEM(3, 2, Element) };
#define COMPONENT_DECL_DECLARE_METHOD_HELPER(R, Data, Element) BOOST_PP_TUPLE_ELEM(2, 0, Element);
#define COMPONENT_DECL_APPEND_FIELD_BINDING_HELPER(R, TypeName, Element) , std::pair<const char*, decltype(&TypeName::BOOST_PP_TUPLE_ELEM(3, 1, Element))>{ BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(3, 1, Element)), &TypeName::BOOST_PP_TUPLE_ELEM(3, 1, Element) }
#define COMPONENT_DECL_APPEND_METHOD_BINDING_HELPER(R, TypeName, Element) , std::pair<const char*, decltype(&TypeName::BOOST_PP_TUPLE_ELEM(2, 1, Element))>{ BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(2, 1, Element)), &TypeName::BOOST_PP_TUPLE_ELEM(2, 1, Element) }
#define ComponentField(Type, Name, ...) ((Type, Name, __VA_ARGS__))
#define ComponentMethod(Signature, Name) ((Signature, Name))
#define ComponentFields(...) __VA_ARGS__
#define ComponentMethods(...) __VA_ARGS__
#define LUA_TYPE_APPEND_FIELD_BINDING_HELPER(R, TypeName, Element) , std::pair<const char*, decltype(&TypeName::BOOST_PP_TUPLE_ELEM(3, 1, Element))>{ BOOST_PP_STRINGIZE(BOOST_PP_TUPLE_ELEM(3, 1, Element)), &TypeName::BOOST_PP_TUPLE_ELEM(3, 1, Element) }
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
