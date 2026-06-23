#include "TerrainSplatMapGenerator.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <execution>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "Utility/MathValidation.h"

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

namespace {
    enum class SplatExpressionNodeType : std::uint8_t {
        Constant,
        Variable,
        Negate,
        Add,
        Subtract,
        Multiply,
        Divide,
        Function
    };

    enum class SplatExpressionFunctionType : std::uint8_t {
        Saturate,
        SmoothStep,
        Min,
        Max,
        Abs,
        Pow,
        Lerp,
        Clamp,
        Step
    };

    constexpr std::size_t InvalidSplatExpressionNodeIndex{ std::numeric_limits<std::size_t>::max() };
    constexpr std::size_t InvalidSplatExpressionVariableIndex{ std::numeric_limits<std::size_t>::max() };
    constexpr std::size_t MaxGeneratedSplatLayerCount{ Terrain::SplatMapData::LayerCount };
    constexpr std::size_t BaseSplatVariableCount{ 8ULL };
    constexpr float SplatExpressionEpsilon{ 0.000001f };

    struct SplatExpressionNode final {
        SplatExpressionNodeType mType{ SplatExpressionNodeType::Constant };
        SplatExpressionFunctionType mFunctionType{ SplatExpressionFunctionType::Saturate };
        float mValue{ 0.0f };
        std::size_t mVariableIndex{ InvalidSplatExpressionVariableIndex };
        std::string mName{};
        std::vector<std::size_t> mChildren{};
    };

    struct SplatExpression final {
        std::vector<SplatExpressionNode> mNodes{};
        std::size_t mRootNodeIndex{ InvalidSplatExpressionNodeIndex };
    };

    struct SplatExpressionParseContext final {
        const std::string* mFormula{ nullptr };
        std::size_t mPosition{ 0ULL };
        SplatExpression* mExpression{ nullptr };
    };

    struct CompiledSplatVariableDesc final {
        std::string mName{};
        SplatExpression mExpression{};
        std::size_t mVariableIndex{ InvalidSplatExpressionVariableIndex };
    };

    struct CompiledSplatLayerDesc final {
        std::string mName{};
        SplatExpression mExpression{};
    };

    struct CompiledSplatMapDesc final {
        std::vector<CompiledSplatVariableDesc> mVariables{};
        std::vector<CompiledSplatLayerDesc> mLayers{};
        std::uint32_t mFallbackLayerIndex{ 0u };
        bool mNormalizeWeights{ true };
        float mMinimumWeightSum{ 0.0001f };
        std::size_t mVariableCount{ BaseSplatVariableCount };
    };

    float Saturate(float Value) {
        return std::clamp(Value, 0.0f, 1.0f);
    }

    float SmoothStep(float Edge0, float Edge1, float Value) {
        const float Range{ Edge1 - Edge0 };
        if (Range <= 0.0f) {
            return Value >= Edge1 ? 1.0f : 0.0f;
        }

        const float T{ Saturate((Value - Edge0) / Range) };
        return T * T * (3.0f - (2.0f * T));
    }

    std::size_t CalculateIndex(std::uint32_t Width, std::uint32_t X, std::uint32_t Z) {
        return (static_cast<std::size_t>(Z) * static_cast<std::size_t>(Width)) + static_cast<std::size_t>(X);
    }

    std::vector<std::uint32_t> CreateRowIndices(std::uint32_t Height) {
        std::vector<std::uint32_t> RowIndices{};
        RowIndices.resize(Height);
        std::iota(RowIndices.begin(), RowIndices.end(), 0u);
        return RowIndices;
    }

    std::uint32_t ClampCoordinate(std::int32_t Value, std::uint32_t MaxValue) {
        const std::int32_t ClampedValue{ std::clamp(Value, 0, static_cast<std::int32_t>(MaxValue)) };
        return static_cast<std::uint32_t>(ClampedValue);
    }

    float SampleHeight01(const Terrain::HeightFieldData& Field, std::int32_t X, std::int32_t Z) {
        const std::uint32_t ClampedX{ ClampCoordinate(X, Field.Width - 1u) };
        const std::uint32_t ClampedZ{ ClampCoordinate(Z, Field.Height - 1u) };
        return Saturate(Field.HeightValues[CalculateIndex(Field.Width, ClampedX, ClampedZ)]);
    }

    float CalculateWorldHeight(const Terrain::HeightFieldData& Field, const Terrain::TerrainBuildDesc& Desc, std::int32_t X, std::int32_t Z) {
        return SampleHeight01(Field, X, Z) * Desc.MaxHeight;
    }

    float CalculateSlope01(const Terrain::HeightFieldData& Field, const Terrain::TerrainBuildDesc& Desc, std::uint32_t X, std::uint32_t Z) {
        const std::int32_t SampleX{ static_cast<std::int32_t>(X) };
        const std::int32_t SampleZ{ static_cast<std::int32_t>(Z) };
        const float HeightNegativeX{ CalculateWorldHeight(Field, Desc, SampleX - 1, SampleZ) };
        const float HeightPositiveX{ CalculateWorldHeight(Field, Desc, SampleX + 1, SampleZ) };
        const float HeightNegativeZ{ CalculateWorldHeight(Field, Desc, SampleX, SampleZ - 1) };
        const float HeightPositiveZ{ CalculateWorldHeight(Field, Desc, SampleX, SampleZ + 1) };
        const float CellSpanX{ std::max(Desc.CellSizeX * 2.0f, 0.0001f) };
        const float CellSpanZ{ std::max(Desc.CellSizeZ * 2.0f, 0.0001f) };
        const float GradientX{ (HeightPositiveX - HeightNegativeX) / CellSpanX };
        const float GradientZ{ (HeightPositiveZ - HeightNegativeZ) / CellSpanZ };
        const float NormalY{ 1.0f / std::sqrt((GradientX * GradientX) + 1.0f + (GradientZ * GradientZ)) };
        return Saturate((1.0f - NormalY) * 2.5f);
    }

    bool IsIdentifierStart(char Character) {
        return std::isalpha(static_cast<unsigned char>(Character)) != 0 || Character == '_';
    }

    bool IsIdentifierBody(char Character) {
        return std::isalnum(static_cast<unsigned char>(Character)) != 0 || Character == '_';
    }

    void SkipSplatExpressionWhitespace(SplatExpressionParseContext& Context) {
        while (Context.mFormula != nullptr && Context.mPosition < Context.mFormula->size() && std::isspace(static_cast<unsigned char>((*Context.mFormula)[Context.mPosition])) != 0) {
            Context.mPosition += 1ULL;
        }
    }

    bool TryMatchSplatExpressionChar(SplatExpressionParseContext& Context, char ExpectedCharacter) {
        SkipSplatExpressionWhitespace(Context);
        if (Context.mFormula == nullptr || Context.mPosition >= Context.mFormula->size() || (*Context.mFormula)[Context.mPosition] != ExpectedCharacter) {
            return false;
        }

        Context.mPosition += 1ULL;
        return true;
    }

    std::size_t AddSplatExpressionNode(SplatExpressionParseContext& Context, SplatExpressionNode Node) {
        if (Context.mExpression == nullptr) {
            throw std::runtime_error{ "Splat map expression parser has no output expression." };
        }

        const std::size_t NodeIndex{ Context.mExpression->mNodes.size() };
        Context.mExpression->mNodes.push_back(std::move(Node));
        return NodeIndex;
    }

    std::size_t ParseSplatExpression(SplatExpressionParseContext& Context);

    std::size_t ParseSplatNumberExpression(SplatExpressionParseContext& Context) {
        if (Context.mFormula == nullptr) {
            throw std::runtime_error{ "Splat map expression formula is missing." };
        }

        const char* StartPointer{ Context.mFormula->c_str() + Context.mPosition };
        char* EndPointer{ nullptr };
        const float Value{ std::strtof(StartPointer, &EndPointer) };
        if (EndPointer == StartPointer || MathUtility::IsFiniteFloat(Value) == false) {
            throw std::runtime_error{ "Splat map expression number parse failed." };
        }

        Context.mPosition += static_cast<std::size_t>(EndPointer - StartPointer);
        SplatExpressionNode Node{};
        Node.mType = SplatExpressionNodeType::Constant;
        Node.mValue = Value;
        return AddSplatExpressionNode(Context, std::move(Node));
    }

    std::string ParseSplatIdentifier(SplatExpressionParseContext& Context) {
        if (Context.mFormula == nullptr || Context.mPosition >= Context.mFormula->size() || IsIdentifierStart((*Context.mFormula)[Context.mPosition]) == false) {
            throw std::runtime_error{ "Splat map expression identifier parse failed." };
        }

        const std::size_t StartPosition{ Context.mPosition };
        Context.mPosition += 1ULL;
        while (Context.mPosition < Context.mFormula->size() && IsIdentifierBody((*Context.mFormula)[Context.mPosition]) == true) {
            Context.mPosition += 1ULL;
        }

        return Context.mFormula->substr(StartPosition, Context.mPosition - StartPosition);
    }

    SplatExpressionFunctionType ResolveSplatExpressionFunctionType(const std::string& Name) {
        if (Name == "Saturate") {
            return SplatExpressionFunctionType::Saturate;
        }

        if (Name == "SmoothStep") {
            return SplatExpressionFunctionType::SmoothStep;
        }

        if (Name == "Min") {
            return SplatExpressionFunctionType::Min;
        }

        if (Name == "Max") {
            return SplatExpressionFunctionType::Max;
        }

        if (Name == "Abs") {
            return SplatExpressionFunctionType::Abs;
        }

        if (Name == "Pow") {
            return SplatExpressionFunctionType::Pow;
        }

        if (Name == "Lerp") {
            return SplatExpressionFunctionType::Lerp;
        }

        if (Name == "Clamp") {
            return SplatExpressionFunctionType::Clamp;
        }

        if (Name == "Step") {
            return SplatExpressionFunctionType::Step;
        }

        throw std::runtime_error{ "Splat map expression has an unknown function." };
    }

    std::size_t ParseSplatIdentifierExpression(SplatExpressionParseContext& Context) {
        std::string Identifier{ ParseSplatIdentifier(Context) };
        if (TryMatchSplatExpressionChar(Context, '(') == true) {
            std::vector<std::size_t> Arguments{};
            if (TryMatchSplatExpressionChar(Context, ')') == false) {
                while (true) {
                    Arguments.push_back(ParseSplatExpression(Context));
                    if (TryMatchSplatExpressionChar(Context, ',') == true) {
                        continue;
                    }

                    if (TryMatchSplatExpressionChar(Context, ')') == false) {
                        throw std::runtime_error{ "Splat map expression function call is missing ')'." };
                    }

                    break;
                }
            }

            SplatExpressionNode Node{};
            Node.mType = SplatExpressionNodeType::Function;
            Node.mFunctionType = ResolveSplatExpressionFunctionType(Identifier);
            Node.mName = std::move(Identifier);
            Node.mChildren = std::move(Arguments);
            return AddSplatExpressionNode(Context, std::move(Node));
        }

        SplatExpressionNode Node{};
        Node.mType = SplatExpressionNodeType::Variable;
        Node.mName = std::move(Identifier);
        return AddSplatExpressionNode(Context, std::move(Node));
    }

    std::size_t ParseSplatPrimaryExpression(SplatExpressionParseContext& Context) {
        SkipSplatExpressionWhitespace(Context);
        if (Context.mFormula == nullptr || Context.mPosition >= Context.mFormula->size()) {
            throw std::runtime_error{ "Splat map expression ended unexpectedly." };
        }

        const char Character{ (*Context.mFormula)[Context.mPosition] };
        if (TryMatchSplatExpressionChar(Context, '(') == true) {
            const std::size_t ExpressionIndex{ ParseSplatExpression(Context) };
            if (TryMatchSplatExpressionChar(Context, ')') == false) {
                throw std::runtime_error{ "Splat map expression is missing ')'." };
            }

            return ExpressionIndex;
        }

        if (std::isdigit(static_cast<unsigned char>(Character)) != 0 || Character == '.') {
            return ParseSplatNumberExpression(Context);
        }

        if (IsIdentifierStart(Character) == true) {
            return ParseSplatIdentifierExpression(Context);
        }

        throw std::runtime_error{ "Splat map expression has an invalid token." };
    }

    std::size_t ParseSplatUnaryExpression(SplatExpressionParseContext& Context) {
        if (TryMatchSplatExpressionChar(Context, '+') == true) {
            return ParseSplatUnaryExpression(Context);
        }

        if (TryMatchSplatExpressionChar(Context, '-') == true) {
            SplatExpressionNode Node{};
            Node.mType = SplatExpressionNodeType::Negate;
            Node.mChildren.push_back(ParseSplatUnaryExpression(Context));
            return AddSplatExpressionNode(Context, std::move(Node));
        }

        return ParseSplatPrimaryExpression(Context);
    }

    std::size_t ParseSplatMultiplicativeExpression(SplatExpressionParseContext& Context) {
        std::size_t LeftNodeIndex{ ParseSplatUnaryExpression(Context) };
        while (true) {
            if (TryMatchSplatExpressionChar(Context, '*') == true) {
                SplatExpressionNode Node{};
                Node.mType = SplatExpressionNodeType::Multiply;
                Node.mChildren.push_back(LeftNodeIndex);
                Node.mChildren.push_back(ParseSplatUnaryExpression(Context));
                LeftNodeIndex = AddSplatExpressionNode(Context, std::move(Node));
                continue;
            }

            if (TryMatchSplatExpressionChar(Context, '/') == true) {
                SplatExpressionNode Node{};
                Node.mType = SplatExpressionNodeType::Divide;
                Node.mChildren.push_back(LeftNodeIndex);
                Node.mChildren.push_back(ParseSplatUnaryExpression(Context));
                LeftNodeIndex = AddSplatExpressionNode(Context, std::move(Node));
                continue;
            }

            break;
        }

        return LeftNodeIndex;
    }

    std::size_t ParseSplatExpression(SplatExpressionParseContext& Context) {
        std::size_t LeftNodeIndex{ ParseSplatMultiplicativeExpression(Context) };
        while (true) {
            if (TryMatchSplatExpressionChar(Context, '+') == true) {
                SplatExpressionNode Node{};
                Node.mType = SplatExpressionNodeType::Add;
                Node.mChildren.push_back(LeftNodeIndex);
                Node.mChildren.push_back(ParseSplatMultiplicativeExpression(Context));
                LeftNodeIndex = AddSplatExpressionNode(Context, std::move(Node));
                continue;
            }

            if (TryMatchSplatExpressionChar(Context, '-') == true) {
                SplatExpressionNode Node{};
                Node.mType = SplatExpressionNodeType::Subtract;
                Node.mChildren.push_back(LeftNodeIndex);
                Node.mChildren.push_back(ParseSplatMultiplicativeExpression(Context));
                LeftNodeIndex = AddSplatExpressionNode(Context, std::move(Node));
                continue;
            }

            break;
        }

        return LeftNodeIndex;
    }

    SplatExpression CompileSplatExpression(const std::string& Formula) {
        if (Formula.empty() == true) {
            throw std::runtime_error{ "Splat map expression formula is empty." };
        }

        SplatExpression Expression{};
        SplatExpressionParseContext Context{ &Formula, 0ULL, &Expression };
        Expression.mRootNodeIndex = ParseSplatExpression(Context);
        SkipSplatExpressionWhitespace(Context);
        if (Context.mPosition != Formula.size()) {
            throw std::runtime_error{ "Splat map expression has trailing tokens." };
        }

        return Expression;
    }

    float EnsureFiniteSplatExpressionValue(float Value) {
        if (MathUtility::IsFiniteFloat(Value) == false) {
            throw std::runtime_error{ "Splat map expression evaluated to a non-finite value." };
        }

        return Value;
    }

    std::vector<std::string> CreateBaseSplatVariableNames() {
        std::vector<std::string> VariableNames{};
        VariableNames.reserve(BaseSplatVariableCount);
        VariableNames.push_back("Height");
        VariableNames.push_back("Slope");
        VariableNames.push_back("WorldHeight");
        VariableNames.push_back("X");
        VariableNames.push_back("Z");
        VariableNames.push_back("U");
        VariableNames.push_back("V");
        VariableNames.push_back("MaxHeight");
        return VariableNames;
    }

    bool HasSplatExpressionVariableName(const std::vector<std::string>& VariableNames, const std::string& Name) {
        for (const std::string& VariableName : VariableNames) {
            if (VariableName == Name) {
                return true;
            }
        }

        return false;
    }

    std::size_t ResolveSplatExpressionVariableIndex(const std::vector<std::string>& VariableNames, const std::string& Name) {
        for (std::size_t VariableIndex{ 0ULL }; VariableIndex < VariableNames.size(); ++VariableIndex) {
            if (VariableNames[VariableIndex] == Name) {
                return VariableIndex;
            }
        }

        throw std::runtime_error{ "Splat map expression has an unknown variable." };
    }

    void ValidateSplatExpressionFunctionArgumentCount(const SplatExpressionNode& Node) {
        const std::size_t ArgumentCount{ Node.mChildren.size() };
        if (Node.mFunctionType == SplatExpressionFunctionType::Saturate && ArgumentCount == 1ULL) {
            return;
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::SmoothStep && ArgumentCount == 3ULL) {
            return;
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Min && ArgumentCount == 2ULL) {
            return;
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Max && ArgumentCount == 2ULL) {
            return;
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Abs && ArgumentCount == 1ULL) {
            return;
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Pow && ArgumentCount == 2ULL) {
            return;
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Lerp && ArgumentCount == 3ULL) {
            return;
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Clamp && ArgumentCount == 3ULL) {
            return;
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Step && ArgumentCount == 2ULL) {
            return;
        }

        throw std::runtime_error{ "Splat map expression function has invalid argument count." };
    }

    void BindSplatExpressionVariables(SplatExpression& Expression, const std::vector<std::string>& VariableNames) {
        for (SplatExpressionNode& Node : Expression.mNodes) {
            if (Node.mType == SplatExpressionNodeType::Variable) {
                Node.mVariableIndex = ResolveSplatExpressionVariableIndex(VariableNames, Node.mName);
            }
            else if (Node.mType == SplatExpressionNodeType::Function) {
                ValidateSplatExpressionFunctionArgumentCount(Node);
            }
        }
    }

    float EvaluateSplatExpressionNode(const SplatExpression& Expression, std::size_t NodeIndex, const std::vector<float>& Variables);

    float EvaluateSplatExpressionChild(const SplatExpression& Expression, const SplatExpressionNode& Node, std::size_t ChildIndex, const std::vector<float>& Variables) {
        if (ChildIndex >= Node.mChildren.size()) {
            throw std::runtime_error{ "Splat map expression node child index is invalid." };
        }

        return EvaluateSplatExpressionNode(Expression, Node.mChildren[ChildIndex], Variables);
    }

    float EvaluateSplatExpressionFunction(const SplatExpression& Expression, const SplatExpressionNode& Node, const std::vector<float>& Variables) {
        if (Node.mFunctionType == SplatExpressionFunctionType::Saturate) {
            return Saturate(EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables));
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::SmoothStep) {
            const float Edge0{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float Edge1{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            const float Value{ EvaluateSplatExpressionChild(Expression, Node, 2ULL, Variables) };
            return SmoothStep(Edge0, Edge1, Value);
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Min) {
            const float Left{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float Right{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            return std::min(Left, Right);
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Max) {
            const float Left{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float Right{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            return std::max(Left, Right);
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Abs) {
            return std::abs(EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables));
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Pow) {
            const float Base{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float Exponent{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            return std::pow(Base, Exponent);
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Lerp) {
            const float Start{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float End{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            const float Alpha{ EvaluateSplatExpressionChild(Expression, Node, 2ULL, Variables) };
            return Start + ((End - Start) * Alpha);
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Clamp) {
            const float Value{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float MinValue{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            const float MaxValue{ EvaluateSplatExpressionChild(Expression, Node, 2ULL, Variables) };
            return std::clamp(Value, MinValue, MaxValue);
        }

        if (Node.mFunctionType == SplatExpressionFunctionType::Step) {
            const float Edge{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float Value{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            return Value < Edge ? 0.0f : 1.0f;
        }

        throw std::runtime_error{ "Splat map expression has an unknown function." };
    }

    float EvaluateSplatExpressionNode(const SplatExpression& Expression, std::size_t NodeIndex, const std::vector<float>& Variables) {
        if (NodeIndex >= Expression.mNodes.size()) {
            throw std::runtime_error{ "Splat map expression node index is invalid." };
        }

        const SplatExpressionNode& Node{ Expression.mNodes[NodeIndex] };
        if (Node.mType == SplatExpressionNodeType::Constant) {
            return EnsureFiniteSplatExpressionValue(Node.mValue);
        }

        if (Node.mType == SplatExpressionNodeType::Variable) {
            if (Node.mVariableIndex >= Variables.size()) {
                throw std::runtime_error{ "Splat map expression variable index is invalid." };
            }

            return EnsureFiniteSplatExpressionValue(Variables[Node.mVariableIndex]);
        }

        if (Node.mType == SplatExpressionNodeType::Negate) {
            return EnsureFiniteSplatExpressionValue(-EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables));
        }

        if (Node.mType == SplatExpressionNodeType::Add) {
            const float Left{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float Right{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            return EnsureFiniteSplatExpressionValue(Left + Right);
        }

        if (Node.mType == SplatExpressionNodeType::Subtract) {
            const float Left{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float Right{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            return EnsureFiniteSplatExpressionValue(Left - Right);
        }

        if (Node.mType == SplatExpressionNodeType::Multiply) {
            const float Left{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float Right{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            return EnsureFiniteSplatExpressionValue(Left * Right);
        }

        if (Node.mType == SplatExpressionNodeType::Divide) {
            const float Left{ EvaluateSplatExpressionChild(Expression, Node, 0ULL, Variables) };
            const float Right{ EvaluateSplatExpressionChild(Expression, Node, 1ULL, Variables) };
            if (std::abs(Right) <= SplatExpressionEpsilon) {
                throw std::runtime_error{ "Splat map expression division by zero." };
            }

            return EnsureFiniteSplatExpressionValue(Left / Right);
        }

        if (Node.mType == SplatExpressionNodeType::Function) {
            return EnsureFiniteSplatExpressionValue(EvaluateSplatExpressionFunction(Expression, Node, Variables));
        }

        throw std::runtime_error{ "Splat map expression node type is invalid." };
    }

    float EvaluateSplatExpression(const SplatExpression& Expression, const std::vector<float>& Variables) {
        if (Expression.mRootNodeIndex == InvalidSplatExpressionNodeIndex) {
            throw std::runtime_error{ "Splat map expression has no root node." };
        }

        return EvaluateSplatExpressionNode(Expression, Expression.mRootNodeIndex, Variables);
    }

    CompiledSplatMapDesc CompileSplatMapDesc(const Terrain::TerrainProceduralHeightFieldDesc::TerrainSplatMapDesc& Desc) {
        if (Desc.mLayers.empty() == true) {
            throw std::runtime_error{ "Splat map config must define at least one layer." };
        }

        if (Desc.mLayers.size() > MaxGeneratedSplatLayerCount) {
            throw std::runtime_error{ "Generated splat map supports up to eight layers." };
        }

        if (Desc.mFallbackLayerIndex >= Desc.mLayers.size()) {
            throw std::runtime_error{ "Splat map fallback layer index is out of range." };
        }

        if (Desc.mMinimumWeightSum <= 0.0f) {
            throw std::runtime_error{ "Splat map minimum weight sum must be greater than zero." };
        }

        CompiledSplatMapDesc CompiledDesc{};
        CompiledDesc.mFallbackLayerIndex = Desc.mFallbackLayerIndex;
        CompiledDesc.mNormalizeWeights = Desc.mNormalizeWeights;
        CompiledDesc.mMinimumWeightSum = Desc.mMinimumWeightSum;
        CompiledDesc.mVariables.reserve(Desc.mVariables.size());

        std::vector<std::string> VariableNames{ CreateBaseSplatVariableNames() };
        for (const Terrain::TerrainProceduralHeightFieldDesc::TerrainSplatMapVariableDesc& VariableDesc : Desc.mVariables) {
            if (VariableDesc.mName.empty() == true || VariableDesc.mFormula.empty() == true) {
                throw std::runtime_error{ "Splat map variables must have Name and Formula." };
            }

            if (HasSplatExpressionVariableName(VariableNames, VariableDesc.mName) == true) {
                throw std::runtime_error{ "Splat map variable name is duplicated." };
            }

            CompiledSplatVariableDesc CompiledVariableDesc{};
            CompiledVariableDesc.mName = VariableDesc.mName;
            CompiledVariableDesc.mVariableIndex = VariableNames.size();
            CompiledVariableDesc.mExpression = CompileSplatExpression(VariableDesc.mFormula);
            BindSplatExpressionVariables(CompiledVariableDesc.mExpression, VariableNames);
            VariableNames.push_back(CompiledVariableDesc.mName);
            CompiledDesc.mVariables.push_back(std::move(CompiledVariableDesc));
        }

        CompiledDesc.mLayers.reserve(Desc.mLayers.size());
        for (const Terrain::TerrainProceduralHeightFieldDesc::TerrainSplatMapLayerDesc& LayerDesc : Desc.mLayers) {
            if (LayerDesc.mName.empty() == true || LayerDesc.mFormula.empty() == true) {
                throw std::runtime_error{ "Splat map layers must have Name and Formula." };
            }

            CompiledSplatLayerDesc CompiledLayerDesc{};
            CompiledLayerDesc.mName = LayerDesc.mName;
            CompiledLayerDesc.mExpression = CompileSplatExpression(LayerDesc.mFormula);
            BindSplatExpressionVariables(CompiledLayerDesc.mExpression, VariableNames);
            CompiledDesc.mLayers.push_back(std::move(CompiledLayerDesc));
        }

        CompiledDesc.mVariableCount = VariableNames.size();
        return CompiledDesc;
    }

    std::array<float, MaxGeneratedSplatLayerCount> NormalizeWeights(const std::array<float, MaxGeneratedSplatLayerCount>& Weights, const CompiledSplatMapDesc& Desc) {
        std::array<float, MaxGeneratedSplatLayerCount> SaturatedWeights{};
        float WeightSum{ 0.0f };
        for (std::size_t LayerIndex{ 0ULL }; LayerIndex < Desc.mLayers.size(); ++LayerIndex) {
            SaturatedWeights[LayerIndex] = Saturate(Weights[LayerIndex]);
            WeightSum += SaturatedWeights[LayerIndex];
        }

        if (WeightSum <= Desc.mMinimumWeightSum) {
            std::array<float, MaxGeneratedSplatLayerCount> FallbackWeights{};
            FallbackWeights[static_cast<std::size_t>(Desc.mFallbackLayerIndex)] = 1.0f;
            return FallbackWeights;
        }

        if (Desc.mNormalizeWeights == false) {
            return SaturatedWeights;
        }

        const float InverseWeightSum{ 1.0f / WeightSum };
        for (std::size_t LayerIndex{ 0ULL }; LayerIndex < Desc.mLayers.size(); ++LayerIndex) {
            SaturatedWeights[LayerIndex] *= InverseWeightSum;
        }

        return SaturatedWeights;
    }

    std::array<float, MaxGeneratedSplatLayerCount> BuildSplatWeights(const Terrain::HeightFieldData& Field, const Terrain::TerrainBuildDesc& Desc, const CompiledSplatMapDesc& SplatMapDesc, std::vector<float>& Variables, std::uint32_t X, std::uint32_t Z) {
        const float HeightValue{ SampleHeight01(Field, static_cast<std::int32_t>(X), static_cast<std::int32_t>(Z)) };
        const float SlopeValue{ CalculateSlope01(Field, Desc, X, Z) };
        const float WorldHeightValue{ HeightValue * Desc.MaxHeight };
        const float UValue{ Field.Width > 1u ? static_cast<float>(X) / static_cast<float>(Field.Width - 1u) : 0.0f };
        const float VValue{ Field.Height > 1u ? static_cast<float>(Z) / static_cast<float>(Field.Height - 1u) : 0.0f };

        Variables[0ULL] = HeightValue;
        Variables[1ULL] = SlopeValue;
        Variables[2ULL] = WorldHeightValue;
        Variables[3ULL] = static_cast<float>(X);
        Variables[4ULL] = static_cast<float>(Z);
        Variables[5ULL] = UValue;
        Variables[6ULL] = VValue;
        Variables[7ULL] = Desc.MaxHeight;

        for (const CompiledSplatVariableDesc& VariableDesc : SplatMapDesc.mVariables) {
            Variables[VariableDesc.mVariableIndex] = EvaluateSplatExpression(VariableDesc.mExpression, Variables);
        }

        std::array<float, MaxGeneratedSplatLayerCount> Weights{};
        for (std::size_t LayerIndex{ 0ULL }; LayerIndex < SplatMapDesc.mLayers.size(); ++LayerIndex) {
            Weights[LayerIndex] = EvaluateSplatExpression(SplatMapDesc.mLayers[LayerIndex].mExpression, Variables);
        }

        return NormalizeWeights(Weights, SplatMapDesc);
    }

    void StoreSplatWeights(Terrain::SplatMapData& SplatMap, std::size_t PixelIndex, const std::array<float, MaxGeneratedSplatLayerCount>& Weights) {
        for (std::size_t WeightMapIndex{ 0ULL }; WeightMapIndex < Terrain::SplatMapData::WeightMapCount; ++WeightMapIndex) {
            const std::size_t LayerIndex{ WeightMapIndex * 4ULL };
            SplatMap.WeightMapValues[WeightMapIndex][PixelIndex] = asset::Vec4{ Weights[LayerIndex], Weights[LayerIndex + 1ULL], Weights[LayerIndex + 2ULL], Weights[LayerIndex + 3ULL] };
        }
    }

    asset::Vec4 AverageSplatWeights(const asset::Vec4& Weight00, const asset::Vec4& Weight10, const asset::Vec4& Weight01, const asset::Vec4& Weight11) {
        return asset::Vec4{ (Weight00.x + Weight10.x + Weight01.x + Weight11.x) * 0.25f, (Weight00.y + Weight10.y + Weight01.y + Weight11.y) * 0.25f, (Weight00.z + Weight10.z + Weight01.z + Weight11.z) * 0.25f, (Weight00.w + Weight10.w + Weight01.w + Weight11.w) * 0.25f };
    }

    void GenerateSplatMapMipLevels(Terrain::SplatMapData& SplatMap) {
        for (std::size_t WeightMapIndex{ 0ULL }; WeightMapIndex < Terrain::SplatMapData::WeightMapCount; ++WeightMapIndex) {
            std::vector<Terrain::SplatMapMipLevelData>& MipLevels{ SplatMap.WeightMapMipLevels[WeightMapIndex] };
            MipLevels.clear();

            const std::vector<asset::Vec4>* ParentWeightValues{ &SplatMap.WeightMapValues[WeightMapIndex] };
            std::uint32_t ParentWidth{ SplatMap.Width };
            std::uint32_t ParentHeight{ SplatMap.Height };
            while (ParentWidth > 1u || ParentHeight > 1u) {
                Terrain::SplatMapMipLevelData MipLevel{};
                MipLevel.Width = std::max(ParentWidth / 2u, 1u);
                MipLevel.Height = std::max(ParentHeight / 2u, 1u);
                MipLevel.WeightValues.resize(static_cast<std::size_t>(MipLevel.Width) * static_cast<std::size_t>(MipLevel.Height));

                for (std::uint32_t Z{ 0u }; Z < MipLevel.Height; ++Z) {
                    const std::uint32_t ParentZ0{ std::min(Z * 2u, ParentHeight - 1u) };
                    const std::uint32_t ParentZ1{ std::min(ParentZ0 + 1u, ParentHeight - 1u) };
                    for (std::uint32_t X{ 0u }; X < MipLevel.Width; ++X) {
                        const std::uint32_t ParentX0{ std::min(X * 2u, ParentWidth - 1u) };
                        const std::uint32_t ParentX1{ std::min(ParentX0 + 1u, ParentWidth - 1u) };
                        const asset::Vec4& Weight00{ (*ParentWeightValues)[CalculateIndex(ParentWidth, ParentX0, ParentZ0)] };
                        const asset::Vec4& Weight10{ (*ParentWeightValues)[CalculateIndex(ParentWidth, ParentX1, ParentZ0)] };
                        const asset::Vec4& Weight01{ (*ParentWeightValues)[CalculateIndex(ParentWidth, ParentX0, ParentZ1)] };
                        const asset::Vec4& Weight11{ (*ParentWeightValues)[CalculateIndex(ParentWidth, ParentX1, ParentZ1)] };
                        MipLevel.WeightValues[CalculateIndex(MipLevel.Width, X, Z)] = AverageSplatWeights(Weight00, Weight10, Weight01, Weight11);
                    }
                }

                MipLevels.push_back(std::move(MipLevel));
                ParentWeightValues = &MipLevels.back().WeightValues;
                ParentWidth = MipLevels.back().Width;
                ParentHeight = MipLevels.back().Height;
            }
        }
    }

    void ValidateSplatMapInput(const Terrain::HeightFieldData& Field, const Terrain::TerrainBuildDesc& Desc) {
        if (Field.Width < 2u || Field.Height < 2u) {
            throw std::runtime_error{ "Splat map height field size must be at least 2x2." };
        }

        const std::size_t ExpectedSize{ static_cast<std::size_t>(Field.Width) * static_cast<std::size_t>(Field.Height) };
        if (Field.HeightValues.size() != ExpectedSize) {
            throw std::runtime_error{ "Splat map height field buffer size mismatch." };
        }

        if (Desc.MaxHeight <= 0.0f || Desc.CellSizeX <= 0.0f || Desc.CellSizeZ <= 0.0f) {
            throw std::runtime_error{ "Splat map terrain scale must be valid." };
        }
    }
}

namespace Terrain {
    TerrainSplatMapGenerator::TerrainSplatMapGenerator() {
    }

    TerrainSplatMapGenerator::~TerrainSplatMapGenerator() {
    }

    TerrainSplatMapGenerator::TerrainSplatMapGenerator(const TerrainSplatMapGenerator& Other) {
        (void)Other;
    }

    TerrainSplatMapGenerator& TerrainSplatMapGenerator::operator=(const TerrainSplatMapGenerator& Other) {
        (void)Other;
        return *this;
    }

    TerrainSplatMapGenerator::TerrainSplatMapGenerator(TerrainSplatMapGenerator&& Other) noexcept {
        (void)Other;
    }

    TerrainSplatMapGenerator& TerrainSplatMapGenerator::operator=(TerrainSplatMapGenerator&& Other) noexcept {
        (void)Other;
        return *this;
    }

    SplatMapData TerrainSplatMapGenerator::Generate(const HeightFieldData& Field, const TerrainBuildDesc& Desc) const {
        ValidateSplatMapInput(Field, Desc);
        const CompiledSplatMapDesc SplatMapDesc{ CompileSplatMapDesc(Desc.mProceduralHeightFieldDesc.mSplatMapDesc) };

        SplatMapData SplatMap{};
        SplatMap.Width = Field.Width;
        SplatMap.Height = Field.Height;
        const std::size_t PixelCount{ static_cast<std::size_t>(SplatMap.Width) * static_cast<std::size_t>(SplatMap.Height) };
        for (std::vector<asset::Vec4>& WeightMapValues : SplatMap.WeightMapValues) {
            WeightMapValues.resize(PixelCount);
        }

        const std::vector<std::uint32_t> RowIndices{ CreateRowIndices(Field.Height) };
        std::for_each(std::execution::par, RowIndices.cbegin(), RowIndices.cend(), [&](std::uint32_t Z) {
            std::vector<float> Variables{};
            Variables.resize(SplatMapDesc.mVariableCount);

            for (std::uint32_t X{ 0u }; X < Field.Width; ++X) {
                const std::size_t Index{ CalculateIndex(Field.Width, X, Z) };
                const std::array<float, MaxGeneratedSplatLayerCount> Weights{ BuildSplatWeights(Field, Desc, SplatMapDesc, Variables, X, Z) };
                StoreSplatWeights(SplatMap, Index, Weights);
            }
        });

        GenerateSplatMapMipLevels(SplatMap);

        return SplatMap;
    }
}
