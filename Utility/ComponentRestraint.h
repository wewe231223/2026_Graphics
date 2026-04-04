/*
+--------------------------------------------------------------------------------------------------+
|                               ComponentRestraint 사용 가이드 (2026-04)
+--------------------------------------------------------------------------------------------------+
| [이 파일의 역할]
| - C++ 타입 정의에서 필드/메서드 메타데이터를 추출해 Lua 등록 파이프라인에서 재사용한다.
| - 핵심 진입점은 아래 Traits 의 Create() 반환값이다.
|   - LuaComponentDefinitionTraits<T>::Create()
|   - LuaTypeDefinitionTraits<T>::Create()
|
| [무엇을 써야 하는가]
| 1) 새 컴포넌트를 선언하면서 Lua 바인딩까지 한 번에 만들고 싶다.
|    -> ComponentDecl(TypeName, FieldsSeq, MethodsSeq)
|
| 2) 이미 존재하는 타입(구조체/클래스)을 Lua에 노출하고 싶다.
|    -> LuaTypeDefinitionDecl / LuaTypeDefinitionDeclWithName
|
| 3) std::array 를 Lua 스크립트에서 배열처럼 사용하고 싶다.
|    -> LuaStdArrayTypeDefinitionDeclWithName(ElementType, ElementCount, LuaTypeName)
|
| [매크로 구성 요소]
| - ComponentField(Type, Name[, DefaultValue])
|   예) ComponentField(float, mX) / ComponentField(int, mCount, 10)
| - ComponentMethod(Signature, Name)
|   예) ComponentMethod(float GetLengthSquared() const, GetLengthSquared)
| - ComponentFields(...) / ComponentMethods(...)
|   여러 항목을 Boost Preprocessor Seq 로 묶을 때 사용
| - 메서드가 없으면 MethodsSeq 자리에 BOOST_PP_SEQ_NIL 전달
|
| [빠른 시작 1: 새 컴포넌트 선언]
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
| [빠른 시작 2: 기존 타입 바인딩]
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
| Lua 이름을 별도로 지정하려면:
| LuaTypeDefinitionDeclWithName(Vec2, "Vector2",
|     ComponentFields(
|         ComponentField(float, mX)
|         ComponentField(float, mY)
|     ),
|     BOOST_PP_SEQ_NIL);
|
| [빠른 시작 3: std::array 를 Lua 에서 쓰기]
| 1) C++ 등록
| LuaStdArrayTypeDefinitionDeclWithName(float, 3, "Float3Array");
|
| 2) Lua 스크립트 사용
| local Values = Float3Array.new()
| Values[0] = 1.25
| Values[1] = 2.5
| Values[2] = 3.75
|
| local Sum = 0.0
| local Count = Values:Size()    -- 현재 배열 크기(예: 3)
| local Index = 0
| while Index < Count do
|     Sum = Sum + Values[Index]
|     Index = Index + 1
| end
|
| 3) 인덱스 규칙
| - [] 연산자는 0 기반 인덱스를 사용한다. (0 ~ ElementCount-1)
| - Get(Index), Set(Index, Value) 도 동일하게 0 기반 인덱스다.
| - 범위를 벗어난 접근은 std::out_of_range 예외를 발생시킨다.
|
| [컴포넌트 정의 체크리스트]
| - 필드 이름과 실제 멤버 이름이 정확히 일치하는가
| - 메서드 시그니처가 실제 선언과 일치하는가 (const 포함)
| - 메서드가 없으면 BOOST_PP_SEQ_NIL 을 전달했는가
| - ComponentDecl 대상 타입이 trivially copyable / trivially destructible / standard layout 인가
|
| [자주 발생하는 실수]
| - ComponentMethod 의 Name 과 실제 멤버 함수 이름 불일치
| - const 멤버 함수를 non-const 시그니처로 작성
| - BOOST_PP_SEQ_NIL 대신 빈 괄호 전달
| - 배열 인덱스를 Lua 관성대로 1부터 사용해서 첫 원소를 건너뜀
| - 기본값 인자 타입과 값 불일치
|
| [내부 표현 참고]
| - 바인딩 항목은 std::pair<const char*, decltype(&Type::Member)> 형태로 저장된다.
| - Create() 는 constexpr 이며 tuple 기반 정의 객체를 반환한다.
+--------------------------------------------------------------------------------------------------+
*/
#pragma once
#include <array>
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

template <typename TElement, std::size_t TElementCount>
struct LuaArrayTypeDefinition final {
    using ValueType = std::array<TElement, TElementCount>;

    const char* mTypeName{};
};

template <typename TElement, std::size_t TElementCount>
constexpr LuaArrayTypeDefinition<TElement, TElementCount> MakeLuaArrayTypeDefinition(const char* TypeName) {
    LuaArrayTypeDefinition<TElement, TElementCount> Definition{};
    Definition.mTypeName = TypeName;
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
#define LuaStdArrayTypeDefinitionDeclWithName(ElementType, ElementCount, LuaTypeName) \
template <> \
struct LuaTypeDefinitionTraits<std::array<ElementType, ElementCount>> final { \
    static constexpr auto Create() { \
        return MakeLuaArrayTypeDefinition<ElementType, ElementCount>(LuaTypeName); \
    } \
}
