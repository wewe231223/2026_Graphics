namespace {
    enum class DynamicSatAxisType : std::uint32_t {
        FaceA = 0U,
        FaceB = 1U,
        Edge = 2U
    };

    struct DynamicObb {
        DirectX::SimpleMath::Vector3 mCenter;
        DirectX::SimpleMath::Vector3 mExtents;
        DirectX::XMFLOAT4 mOrientation;
        std::array<DirectX::SimpleMath::Vector3, 3U> mAxes;
    };

    struct DynamicSatResult {
        bool mIntersect;
        DirectX::SimpleMath::Vector3 mNormal;
        float mPenetration;
        DynamicSatAxisType mAxisType;
        std::uint32_t mAxisIndexA;
        std::uint32_t mAxisIndexB;
    };

    constexpr float DynamicSatAxisEpsilon{ 0.00001F };
    constexpr float DynamicRestitutionThreshold{ 1.25F };
    constexpr float DynamicPenetrationVelocityFactor{ 0.08F };
    constexpr float DynamicPositionCorrectionFactor{ 0.1F };
    constexpr float DynamicPositionCorrectionSlop{ 0.004F };

    float GetVectorComponent(const DirectX::SimpleMath::Vector3& Value, std::uint32_t AxisIndex) {
        if (AxisIndex == 0U) {
            return Value.x;
        }

        if (AxisIndex == 1U) {
            return Value.y;
        }

        return Value.z;
    }

    bool IsNearlyZeroVector(const DirectX::SimpleMath::Vector3& Value, float Epsilon) {
        return Value.LengthSquared() <= (Epsilon * Epsilon);
    }

    DirectX::SimpleMath::Vector3 NormalizeOrZero(const DirectX::SimpleMath::Vector3& Value) {
        float LengthSquared{ Value.LengthSquared() };
        if (LengthSquared <= (DynamicSatAxisEpsilon * DynamicSatAxisEpsilon)) {
            return DirectX::SimpleMath::Vector3{};
        }

        return Value / std::sqrt(LengthSquared);
    }

    DirectX::SimpleMath::Vector3 RotateVectorByOrientation(const DirectX::XMFLOAT4& Orientation, const DirectX::SimpleMath::Vector3& Direction) {
        DirectX::XMVECTOR DirectionVector{ DirectX::XMVectorSet(Direction.x, Direction.y, Direction.z, 0.0F) };
        DirectX::XMVECTOR OrientationQuaternion{ DirectX::XMLoadFloat4(&Orientation) };
        DirectX::XMVECTOR RotatedVector{ DirectX::XMVector3Rotate(DirectionVector, OrientationQuaternion) };
        DirectX::XMFLOAT3 RotatedValue{};
        DirectX::XMStoreFloat3(&RotatedValue, RotatedVector);
        return DirectX::SimpleMath::Vector3{ RotatedValue.x, RotatedValue.y, RotatedValue.z };
    }

    DynamicObb CreateDynamicObb(const DirectX::BoundingOrientedBox& BoundingBox) {
        DynamicObb ObbValue{};
        ObbValue.mCenter = DirectX::SimpleMath::Vector3{ BoundingBox.Center.x, BoundingBox.Center.y, BoundingBox.Center.z };
        ObbValue.mExtents = DirectX::SimpleMath::Vector3{ BoundingBox.Extents.x, BoundingBox.Extents.y, BoundingBox.Extents.z };
        ObbValue.mOrientation = BoundingBox.Orientation;
        ObbValue.mAxes[0U] = NormalizeOrZero(RotateVectorByOrientation(ObbValue.mOrientation, DirectX::SimpleMath::Vector3{ 1.0F, 0.0F, 0.0F }));
        ObbValue.mAxes[1U] = NormalizeOrZero(RotateVectorByOrientation(ObbValue.mOrientation, DirectX::SimpleMath::Vector3{ 0.0F, 1.0F, 0.0F }));
        ObbValue.mAxes[2U] = NormalizeOrZero(RotateVectorByOrientation(ObbValue.mOrientation, DirectX::SimpleMath::Vector3{ 0.0F, 0.0F, 1.0F }));
        return ObbValue;
    }

    bool ComputeObbSatResult(const DynamicObb& FirstObb, const DynamicObb& SecondObb, DynamicSatResult& OutSatResult) {
        std::array<std::array<float, 3U>, 3U> Rotation{};
        std::array<std::array<float, 3U>, 3U> AbsoluteRotation{};
        for (std::uint32_t AxisAIndex{ 0U }; AxisAIndex < 3U; ++AxisAIndex) {
            for (std::uint32_t AxisBIndex{ 0U }; AxisBIndex < 3U; ++AxisBIndex) {
                float DotValue{ FirstObb.mAxes[AxisAIndex].Dot(SecondObb.mAxes[AxisBIndex]) };
                Rotation[AxisAIndex][AxisBIndex] = DotValue;
                AbsoluteRotation[AxisAIndex][AxisBIndex] = std::abs(DotValue) + DynamicSatAxisEpsilon;
            }
        }

        DirectX::SimpleMath::Vector3 CenterDelta{ SecondObb.mCenter - FirstObb.mCenter };
        std::array<float, 3U> TranslationInFirstBasis{};
        TranslationInFirstBasis[0U] = CenterDelta.Dot(FirstObb.mAxes[0U]);
        TranslationInFirstBasis[1U] = CenterDelta.Dot(FirstObb.mAxes[1U]);
        TranslationInFirstBasis[2U] = CenterDelta.Dot(FirstObb.mAxes[2U]);

        DynamicSatResult BestResult{};
        BestResult.mIntersect = true;
        BestResult.mNormal = DirectX::SimpleMath::Vector3{ 1.0F, 0.0F, 0.0F };
        BestResult.mPenetration = std::numeric_limits<float>::max();
        BestResult.mAxisType = DynamicSatAxisType::FaceA;
        BestResult.mAxisIndexA = 0U;
        BestResult.mAxisIndexB = 0U;

        for (std::uint32_t AxisIndex{ 0U }; AxisIndex < 3U; ++AxisIndex) {
            float RadiusFirst{ GetVectorComponent(FirstObb.mExtents, AxisIndex) };
            float RadiusSecond{ (SecondObb.mExtents.x * AbsoluteRotation[AxisIndex][0U]) + (SecondObb.mExtents.y * AbsoluteRotation[AxisIndex][1U]) + (SecondObb.mExtents.z * AbsoluteRotation[AxisIndex][2U]) };
            float Projection{ std::abs(TranslationInFirstBasis[AxisIndex]) };
            float Overlap{ (RadiusFirst + RadiusSecond) - Projection };
            if (Overlap < 0.0F) {
                OutSatResult = DynamicSatResult{};
                OutSatResult.mIntersect = false;
                return false;
            }

            if (Overlap < BestResult.mPenetration) {
                DirectX::SimpleMath::Vector3 AxisValue{ FirstObb.mAxes[AxisIndex] };
                if (AxisValue.Dot(CenterDelta) < 0.0F) {
                    AxisValue = -AxisValue;
                }

                BestResult.mPenetration = Overlap;
                BestResult.mNormal = NormalizeOrZero(AxisValue);
                BestResult.mAxisType = DynamicSatAxisType::FaceA;
                BestResult.mAxisIndexA = AxisIndex;
                BestResult.mAxisIndexB = 0U;
            }
        }

        for (std::uint32_t AxisIndex{ 0U }; AxisIndex < 3U; ++AxisIndex) {
            float RadiusFirst{ (FirstObb.mExtents.x * AbsoluteRotation[0U][AxisIndex]) + (FirstObb.mExtents.y * AbsoluteRotation[1U][AxisIndex]) + (FirstObb.mExtents.z * AbsoluteRotation[2U][AxisIndex]) };
            float RadiusSecond{ GetVectorComponent(SecondObb.mExtents, AxisIndex) };
            float Projection{ std::abs((TranslationInFirstBasis[0U] * Rotation[0U][AxisIndex]) + (TranslationInFirstBasis[1U] * Rotation[1U][AxisIndex]) + (TranslationInFirstBasis[2U] * Rotation[2U][AxisIndex])) };
            float Overlap{ (RadiusFirst + RadiusSecond) - Projection };
            if (Overlap < 0.0F) {
                OutSatResult = DynamicSatResult{};
                OutSatResult.mIntersect = false;
                return false;
            }

            if (Overlap < BestResult.mPenetration) {
                DirectX::SimpleMath::Vector3 AxisValue{ SecondObb.mAxes[AxisIndex] };
                if (AxisValue.Dot(CenterDelta) < 0.0F) {
                    AxisValue = -AxisValue;
                }

                BestResult.mPenetration = Overlap;
                BestResult.mNormal = NormalizeOrZero(AxisValue);
                BestResult.mAxisType = DynamicSatAxisType::FaceB;
                BestResult.mAxisIndexA = 0U;
                BestResult.mAxisIndexB = AxisIndex;
            }
        }

        for (std::uint32_t AxisAIndex{ 0U }; AxisAIndex < 3U; ++AxisAIndex) {
            std::uint32_t AxisAFirstPerpendicular{ (AxisAIndex + 1U) % 3U };
            std::uint32_t AxisASecondPerpendicular{ (AxisAIndex + 2U) % 3U };
            for (std::uint32_t AxisBIndex{ 0U }; AxisBIndex < 3U; ++AxisBIndex) {
                std::uint32_t AxisBFirstPerpendicular{ (AxisBIndex + 1U) % 3U };
                std::uint32_t AxisBSecondPerpendicular{ (AxisBIndex + 2U) % 3U };
                float RadiusFirst{ (GetVectorComponent(FirstObb.mExtents, AxisAFirstPerpendicular) * AbsoluteRotation[AxisASecondPerpendicular][AxisBIndex]) + (GetVectorComponent(FirstObb.mExtents, AxisASecondPerpendicular) * AbsoluteRotation[AxisAFirstPerpendicular][AxisBIndex]) };
                float RadiusSecond{ (GetVectorComponent(SecondObb.mExtents, AxisBFirstPerpendicular) * AbsoluteRotation[AxisAIndex][AxisBSecondPerpendicular]) + (GetVectorComponent(SecondObb.mExtents, AxisBSecondPerpendicular) * AbsoluteRotation[AxisAIndex][AxisBFirstPerpendicular]) };
                float Projection{ std::abs((TranslationInFirstBasis[AxisASecondPerpendicular] * Rotation[AxisAFirstPerpendicular][AxisBIndex]) - (TranslationInFirstBasis[AxisAFirstPerpendicular] * Rotation[AxisASecondPerpendicular][AxisBIndex])) };
                float Overlap{ (RadiusFirst + RadiusSecond) - Projection };
                if (Overlap < 0.0F) {
                    OutSatResult = DynamicSatResult{};
                    OutSatResult.mIntersect = false;
                    return false;
                }

                DirectX::SimpleMath::Vector3 AxisValue{ FirstObb.mAxes[AxisAIndex].Cross(SecondObb.mAxes[AxisBIndex]) };
                float AxisLengthSquared{ AxisValue.LengthSquared() };
                if (AxisLengthSquared <= (DynamicSatAxisEpsilon * DynamicSatAxisEpsilon)) {
                    continue;
                }

                AxisValue /= std::sqrt(AxisLengthSquared);
                if (AxisValue.Dot(CenterDelta) < 0.0F) {
                    AxisValue = -AxisValue;
                }

                if (Overlap < BestResult.mPenetration) {
                    BestResult.mPenetration = Overlap;
                    BestResult.mNormal = AxisValue;
                    BestResult.mAxisType = DynamicSatAxisType::Edge;
                    BestResult.mAxisIndexA = AxisAIndex;
                    BestResult.mAxisIndexB = AxisBIndex;
                }
            }
        }

        BestResult.mNormal = NormalizeOrZero(BestResult.mNormal);
        OutSatResult = BestResult;
        return true;
    }

    void ApplyImpulseToVelocityPair(DirectX::SimpleMath::Vector3& InOutFirstVelocity, DirectX::SimpleMath::Vector3& InOutSecondVelocity, float FirstInverseMass, float SecondInverseMass, const DirectX::SimpleMath::Vector3& ImpulseValue) {
        InOutFirstVelocity -= ImpulseValue * FirstInverseMass;
        InOutSecondVelocity += ImpulseValue * SecondInverseMass;
    }

    bool ResolveCollisionFromSatResult(PhysicsActorBase& FirstActor, PhysicsActorBase& SecondActor, const DynamicSatResult& SatResult, float DeltaTime) {
        if (!SatResult.mIntersect) {
            return false;
        }

        DirectX::SimpleMath::Vector3 CollisionNormal{ NormalizeOrZero(SatResult.mNormal) };
        if (IsNearlyZeroVector(CollisionNormal, DynamicSatAxisEpsilon)) {
            return false;
        }

        float FirstInverseMass{ FirstActor.GetInverseMass() };
        float SecondInverseMass{ SecondActor.GetInverseMass() };
        float CombinedInverseMass{ FirstInverseMass + SecondInverseMass };
        if (CombinedInverseMass <= 0.0F) {
            return false;
        }

        DirectX::SimpleMath::Vector3 FirstVelocity{ FirstActor.GetVelocity() };
        DirectX::SimpleMath::Vector3 SecondVelocity{ SecondActor.GetVelocity() };
        DirectX::SimpleMath::Vector3 RelativeVelocity{ SecondVelocity - FirstVelocity };
        float RelativeNormalVelocity{ RelativeVelocity.Dot(CollisionNormal) };
        float EffectiveRestitution{ std::min(FirstActor.GetRestitution(), SecondActor.GetRestitution()) };
        float EffectiveFriction{ std::sqrt(std::max(0.0F, FirstActor.GetFriction() * SecondActor.GetFriction())) };

        float RestitutionFactor{};
        if (RelativeNormalVelocity < -DynamicRestitutionThreshold) {
            RestitutionFactor = EffectiveRestitution;
        }

        float InverseDeltaTime{ DeltaTime > DynamicSatAxisEpsilon ? (1.0F / DeltaTime) : 0.0F };
        float PositionBiasVelocity{ std::max(0.0F, SatResult.mPenetration - DynamicPositionCorrectionSlop) * DynamicPenetrationVelocityFactor * InverseDeltaTime };
        float ClosingVelocity{ std::min(RelativeNormalVelocity, 0.0F) };
        float TargetSeparationVelocity{ (-ClosingVelocity * (1.0F + RestitutionFactor)) + PositionBiasVelocity };
        float NormalImpulseMagnitude{ TargetSeparationVelocity / CombinedInverseMass };
        if (NormalImpulseMagnitude < 0.0F) {
            NormalImpulseMagnitude = 0.0F;
        }

        DirectX::SimpleMath::Vector3 NormalImpulse{ CollisionNormal * NormalImpulseMagnitude };
        ApplyImpulseToVelocityPair(FirstVelocity, SecondVelocity, FirstInverseMass, SecondInverseMass, NormalImpulse);

        RelativeVelocity = SecondVelocity - FirstVelocity;
        float VelocityAlongNormal{ RelativeVelocity.Dot(CollisionNormal) };
        DirectX::SimpleMath::Vector3 TangentialVelocity{ RelativeVelocity - (CollisionNormal * VelocityAlongNormal) };
        float TangentialVelocityLengthSquared{ TangentialVelocity.LengthSquared() };
        if (TangentialVelocityLengthSquared > (DynamicSatAxisEpsilon * DynamicSatAxisEpsilon)) {
            DirectX::SimpleMath::Vector3 Tangent{ TangentialVelocity / std::sqrt(TangentialVelocityLengthSquared) };
            float TangentialImpulseMagnitude{ -RelativeVelocity.Dot(Tangent) / CombinedInverseMass };
            float MaximumFrictionImpulse{ EffectiveFriction * NormalImpulseMagnitude };
            TangentialImpulseMagnitude = std::clamp(TangentialImpulseMagnitude, -MaximumFrictionImpulse, MaximumFrictionImpulse);
            DirectX::SimpleMath::Vector3 TangentialImpulse{ Tangent * TangentialImpulseMagnitude };
            ApplyImpulseToVelocityPair(FirstVelocity, SecondVelocity, FirstInverseMass, SecondInverseMass, TangentialImpulse);
        }

        FirstActor.SetVelocity(FirstVelocity);
        SecondActor.SetVelocity(SecondVelocity);

        float CorrectedPenetration{ std::max(0.0F, SatResult.mPenetration - DynamicPositionCorrectionSlop) };
        if (CorrectedPenetration > 0.0F) {
            float CorrectionMagnitude{ (CorrectedPenetration / CombinedInverseMass) * DynamicPositionCorrectionFactor };
            DirectX::SimpleMath::Vector3 CorrectionVector{ CollisionNormal * CorrectionMagnitude };
            FirstActor.SetPosition(FirstActor.GetPosition() - (CorrectionVector * FirstInverseMass));
            SecondActor.SetPosition(SecondActor.GetPosition() + (CorrectionVector * SecondInverseMass));
        }

        return true;
    }
}
