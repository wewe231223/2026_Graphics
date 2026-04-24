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
    constexpr std::size_t DynamicContactManifoldMaximumPointCount{ 4U };
    constexpr float DynamicContactPointMergeDistanceSquared{ 0.0001F };

    struct DynamicContactPoint {
        DirectX::SimpleMath::Vector3 mPosition;
    };

    struct DynamicContactManifold {
        std::array<DynamicContactPoint, DynamicContactManifoldMaximumPointCount> mContactPoints;
        std::size_t mContactPointCount;
    };

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

                float AxisLength{ std::sqrt(AxisLengthSquared) };
                AxisValue /= AxisLength;
                if (AxisValue.Dot(CenterDelta) < 0.0F) {
                    AxisValue = -AxisValue;
                }

                float NormalizedOverlap{ Overlap / AxisLength };
                if (NormalizedOverlap < BestResult.mPenetration) {
                    BestResult.mPenetration = NormalizedOverlap;
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

    DirectX::SimpleMath::Vector3 GetObbSupportPoint(const DynamicObb& ObbValue, const DirectX::SimpleMath::Vector3& Direction) {
        DirectX::SimpleMath::Vector3 SupportPoint{ ObbValue.mCenter };
        for (std::uint32_t AxisIndex{ 0U }; AxisIndex < 3U; ++AxisIndex) {
            float DirectionDotAxis{ Direction.Dot(ObbValue.mAxes[AxisIndex]) };
            float DirectionSign{ DirectionDotAxis >= 0.0F ? 1.0F : -1.0F };
            float AxisExtent{ GetVectorComponent(ObbValue.mExtents, AxisIndex) };
            SupportPoint += ObbValue.mAxes[AxisIndex] * AxisExtent * DirectionSign;
        }

        return SupportPoint;
    }

    bool IsPointInsideObb(const DynamicObb& ObbValue, const DirectX::SimpleMath::Vector3& Point) {
        DirectX::SimpleMath::Vector3 CenterDelta{ Point - ObbValue.mCenter };
        for (std::uint32_t AxisIndex{ 0U }; AxisIndex < 3U; ++AxisIndex) {
            float ProjectedDistance{ CenterDelta.Dot(ObbValue.mAxes[AxisIndex]) };
            float AxisExtent{ GetVectorComponent(ObbValue.mExtents, AxisIndex) };
            if (std::abs(ProjectedDistance) > AxisExtent + DynamicSatAxisEpsilon) {
                return false;
            }
        }

        return true;
    }

    void AddContactPoint(DynamicContactManifold& InOutManifold, const DirectX::SimpleMath::Vector3& ContactPoint) {
        for (std::size_t ContactPointIndex{ 0U }; ContactPointIndex < InOutManifold.mContactPointCount; ++ContactPointIndex) {
            DirectX::SimpleMath::Vector3 PointDelta{ InOutManifold.mContactPoints[ContactPointIndex].mPosition - ContactPoint };
            if (PointDelta.LengthSquared() <= DynamicContactPointMergeDistanceSquared) {
                return;
            }
        }

        if (InOutManifold.mContactPointCount >= DynamicContactManifoldMaximumPointCount) {
            return;
        }

        InOutManifold.mContactPoints[InOutManifold.mContactPointCount] = DynamicContactPoint{ ContactPoint };
        ++InOutManifold.mContactPointCount;
    }

    void AddObbCornersInsideOther(DynamicContactManifold& InOutManifold, const DirectX::BoundingOrientedBox& SourceBounds, const DynamicObb& TargetObb) {
        DirectX::XMFLOAT3 Corners[8]{};
        SourceBounds.GetCorners(Corners);
        for (std::size_t CornerIndex{ 0U }; CornerIndex < 8U; ++CornerIndex) {
            DirectX::SimpleMath::Vector3 CornerPoint{ Corners[CornerIndex].x, Corners[CornerIndex].y, Corners[CornerIndex].z };
            if (!IsPointInsideObb(TargetObb, CornerPoint)) {
                continue;
            }

            AddContactPoint(InOutManifold, CornerPoint);
        }
    }

    DynamicContactManifold BuildContactManifold(const DirectX::BoundingOrientedBox& FirstBounds, const DirectX::BoundingOrientedBox& SecondBounds, const DynamicObb& FirstObb, const DynamicObb& SecondObb, const DynamicSatResult& SatResult) {
        DynamicContactManifold Manifold{};
        AddObbCornersInsideOther(Manifold, FirstBounds, SecondObb);
        AddObbCornersInsideOther(Manifold, SecondBounds, FirstObb);
        if (Manifold.mContactPointCount == 0U) {
            DirectX::SimpleMath::Vector3 FirstSupportPoint{ GetObbSupportPoint(FirstObb, SatResult.mNormal) };
            DirectX::SimpleMath::Vector3 SecondSupportPoint{ GetObbSupportPoint(SecondObb, -SatResult.mNormal) };
            DirectX::SimpleMath::Vector3 ContactPoint{ (FirstSupportPoint + SecondSupportPoint) * 0.5F };
            AddContactPoint(Manifold, ContactPoint);
        }

        return Manifold;
    }

    DirectX::SimpleMath::Vector3 CalculateVelocityAtPoint(const PhysicsActorBase& Actor, const DirectX::SimpleMath::Vector3& WorldPoint) {
        DirectX::SimpleMath::Vector3 ContactOffset{ WorldPoint - Actor.GetPosition() };
        DirectX::SimpleMath::Vector3 AngularVelocityContribution{ Actor.GetAngularVelocity().Cross(ContactOffset) };
        DirectX::SimpleMath::Vector3 PointVelocity{ Actor.GetVelocity() + AngularVelocityContribution };
        return PointVelocity;
    }

    float CalculateSingleActorContactImpulseDenominator(const PhysicsActorBase& Actor, const DirectX::SimpleMath::Vector3& ContactOffset, const DirectX::SimpleMath::Vector3& Direction) {
        DirectX::SimpleMath::Vector3 RadiusCrossDirection{ ContactOffset.Cross(Direction) };
        DirectX::SimpleMath::Vector3 AngularVelocityDelta{ DirectX::SimpleMath::Vector3::TransformNormal(RadiusCrossDirection, Actor.GetInverseInertiaTensorWorld()) };
        DirectX::SimpleMath::Vector3 ContactVelocityDelta{ AngularVelocityDelta.Cross(ContactOffset) };
        float Denominator{ Actor.GetInverseMass() + Direction.Dot(ContactVelocityDelta) };
        return std::max(0.0F, Denominator);
    }

    float CalculateContactImpulseDenominator(const PhysicsActorBase& FirstActor, const PhysicsActorBase& SecondActor, const DirectX::SimpleMath::Vector3& ContactPoint, const DirectX::SimpleMath::Vector3& Direction) {
        DirectX::SimpleMath::Vector3 FirstContactOffset{ ContactPoint - FirstActor.GetPosition() };
        DirectX::SimpleMath::Vector3 SecondContactOffset{ ContactPoint - SecondActor.GetPosition() };
        float FirstDenominator{ CalculateSingleActorContactImpulseDenominator(FirstActor, FirstContactOffset, Direction) };
        float SecondDenominator{ CalculateSingleActorContactImpulseDenominator(SecondActor, SecondContactOffset, Direction) };
        float Denominator{ FirstDenominator + SecondDenominator };
        return Denominator;
    }

    void ApplyImpulseToActorPair(PhysicsActorBase& FirstActor, PhysicsActorBase& SecondActor, const DirectX::SimpleMath::Vector3& ContactPoint, const DirectX::SimpleMath::Vector3& ImpulseValue) {
        FirstActor.ApplyImpulseAtPoint(-ImpulseValue, ContactPoint);
        SecondActor.ApplyImpulseAtPoint(ImpulseValue, ContactPoint);
    }

    bool ResolveContactPointImpulse(PhysicsActorBase& FirstActor, PhysicsActorBase& SecondActor, const DirectX::SimpleMath::Vector3& ContactPoint, const DirectX::SimpleMath::Vector3& CollisionNormal, float PenetrationDepth, float EffectiveRestitution, float EffectiveFriction, float DeltaTime, std::size_t ContactPointCount) {
        DirectX::SimpleMath::Vector3 FirstContactVelocity{ CalculateVelocityAtPoint(FirstActor, ContactPoint) };
        DirectX::SimpleMath::Vector3 SecondContactVelocity{ CalculateVelocityAtPoint(SecondActor, ContactPoint) };
        DirectX::SimpleMath::Vector3 RelativeVelocity{ SecondContactVelocity - FirstContactVelocity };
        float RelativeNormalVelocity{ RelativeVelocity.Dot(CollisionNormal) };

        float RestitutionFactor{};
        if (RelativeNormalVelocity < -DynamicRestitutionThreshold) {
            RestitutionFactor = EffectiveRestitution;
        }

        float SafeContactPointCount{ static_cast<float>(std::max<std::size_t>(ContactPointCount, 1U)) };
        float InverseDeltaTime{ DeltaTime > DynamicSatAxisEpsilon ? (1.0F / DeltaTime) : 0.0F };
        float PositionBiasVelocity{ std::max(0.0F, PenetrationDepth - DynamicPositionCorrectionSlop) * DynamicPenetrationVelocityFactor * InverseDeltaTime / SafeContactPointCount };
        float ClosingVelocity{ std::min(RelativeNormalVelocity, 0.0F) };
        float TargetSeparationVelocity{ (-ClosingVelocity * (1.0F + RestitutionFactor)) + PositionBiasVelocity };
        float NormalDenominator{ CalculateContactImpulseDenominator(FirstActor, SecondActor, ContactPoint, CollisionNormal) };
        if (NormalDenominator <= DynamicSatAxisEpsilon) {
            return false;
        }

        float NormalImpulseMagnitude{ TargetSeparationVelocity / NormalDenominator };
        if (NormalImpulseMagnitude < 0.0F) {
            NormalImpulseMagnitude = 0.0F;
        }

        bool HasAppliedImpulse{};
        if (NormalImpulseMagnitude > 0.0F) {
            DirectX::SimpleMath::Vector3 NormalImpulse{ CollisionNormal * NormalImpulseMagnitude };
            ApplyImpulseToActorPair(FirstActor, SecondActor, ContactPoint, NormalImpulse);
            HasAppliedImpulse = true;
        }

        FirstContactVelocity = CalculateVelocityAtPoint(FirstActor, ContactPoint);
        SecondContactVelocity = CalculateVelocityAtPoint(SecondActor, ContactPoint);
        RelativeVelocity = SecondContactVelocity - FirstContactVelocity;
        float VelocityAlongNormal{ RelativeVelocity.Dot(CollisionNormal) };
        DirectX::SimpleMath::Vector3 TangentialVelocity{ RelativeVelocity - (CollisionNormal * VelocityAlongNormal) };
        float TangentialVelocityLengthSquared{ TangentialVelocity.LengthSquared() };
        if (TangentialVelocityLengthSquared > (DynamicSatAxisEpsilon * DynamicSatAxisEpsilon) && NormalImpulseMagnitude > 0.0F) {
            DirectX::SimpleMath::Vector3 Tangent{ TangentialVelocity / std::sqrt(TangentialVelocityLengthSquared) };
            float TangentialDenominator{ CalculateContactImpulseDenominator(FirstActor, SecondActor, ContactPoint, Tangent) };
            if (TangentialDenominator > DynamicSatAxisEpsilon) {
                float TangentialImpulseMagnitude{ -RelativeVelocity.Dot(Tangent) / TangentialDenominator };
                float MaximumFrictionImpulse{ EffectiveFriction * NormalImpulseMagnitude };
                TangentialImpulseMagnitude = std::clamp(TangentialImpulseMagnitude, -MaximumFrictionImpulse, MaximumFrictionImpulse);
                if (std::abs(TangentialImpulseMagnitude) > DynamicSatAxisEpsilon) {
                    DirectX::SimpleMath::Vector3 TangentialImpulse{ Tangent * TangentialImpulseMagnitude };
                    ApplyImpulseToActorPair(FirstActor, SecondActor, ContactPoint, TangentialImpulse);
                    HasAppliedImpulse = true;
                }
            }
        }

        return HasAppliedImpulse;
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

        DirectX::BoundingOrientedBox FirstBounds{ FirstActor.GetWorldBoundingBox() };
        DirectX::BoundingOrientedBox SecondBounds{ SecondActor.GetWorldBoundingBox() };
        DynamicObb FirstObb{ CreateDynamicObb(FirstBounds) };
        DynamicObb SecondObb{ CreateDynamicObb(SecondBounds) };
        DynamicContactManifold ContactManifold{ BuildContactManifold(FirstBounds, SecondBounds, FirstObb, SecondObb, SatResult) };
        if (ContactManifold.mContactPointCount == 0U) {
            return false;
        }

        float EffectiveRestitution{ std::min(FirstActor.GetRestitution(), SecondActor.GetRestitution()) };
        float EffectiveFriction{ std::sqrt(std::max(0.0F, FirstActor.GetFriction() * SecondActor.GetFriction())) };

        bool HasAppliedImpulse{};
        for (std::size_t ContactPointIndex{ 0U }; ContactPointIndex < ContactManifold.mContactPointCount; ++ContactPointIndex) {
            const DynamicContactPoint& ContactPoint{ ContactManifold.mContactPoints[ContactPointIndex] };
            bool HasResolvedContact{ ResolveContactPointImpulse(FirstActor, SecondActor, ContactPoint.mPosition, CollisionNormal, SatResult.mPenetration, EffectiveRestitution, EffectiveFriction, DeltaTime, ContactManifold.mContactPointCount) };
            HasAppliedImpulse = HasAppliedImpulse || HasResolvedContact;
        }

        float CorrectedPenetration{ std::max(0.0F, SatResult.mPenetration - DynamicPositionCorrectionSlop) };
        if (CorrectedPenetration > 0.0F) {
            float CorrectionMagnitude{ (CorrectedPenetration / CombinedInverseMass) * DynamicPositionCorrectionFactor };
            DirectX::SimpleMath::Vector3 CorrectionVector{ CollisionNormal * CorrectionMagnitude };
            FirstActor.SetPosition(FirstActor.GetPosition() - (CorrectionVector * FirstInverseMass));
            SecondActor.SetPosition(SecondActor.GetPosition() + (CorrectionVector * SecondInverseMass));
        }

        return HasAppliedImpulse || CorrectedPenetration > 0.0F;
    }
}
