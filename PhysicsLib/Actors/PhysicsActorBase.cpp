#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "PhysicsLib/Actors/PhysicsActorBase.h"

#undef max
#undef min

namespace {
DirectX::BoundingOrientedBox MakeDefaultBoundingOrientedBox() {
    DirectX::BoundingOrientedBox BoundingBoxValue{};
    BoundingBoxValue.Center = DirectX::XMFLOAT3{ 0.0F, 0.0F, 0.0F };
    BoundingBoxValue.Extents = DirectX::XMFLOAT3{ 0.5F, 0.5F, 0.5F };
    BoundingBoxValue.Orientation = DirectX::XMFLOAT4{ 0.0F, 0.0F, 0.0F, 1.0F };
    return BoundingBoxValue;
}

constexpr float ContactNormalLengthEpsilon{ 0.00001F };
constexpr float SupportingContactMinimumDot{ 0.5F };

DirectX::SimpleMath::Matrix MakeZeroMatrix() {
    DirectX::SimpleMath::Matrix ZeroMatrix{ 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F };
    return ZeroMatrix;
}
}

PhysicsActorBase::PhysicsActorBase()
    : mName{ "PhysicsActor" },
      mIsActive{ true },
      mIsSleeping{},
      mSleepThreshold{ 0.05F },
      mBoundingBoxFatMargin{ 0.1F },
      mRigidBody{},
      mContactNormals{},
      mFlags{ PhysicsActorFlags::None },
      mActorType{ PhysicsActorType::Dynamic },
      mLocalBoundingBox{ MakeDefaultBoundingOrientedBox() },
      mWorldBoundingBox{ MakeDefaultBoundingOrientedBox() },
      mFatWorldBoundingBox{ MakeDefaultBoundingOrientedBox() } {
    UpdateWorldBoundingBox();
    UpdateInverseInertiaTensorWorld();
}

PhysicsActorBase::~PhysicsActorBase() {
}

PhysicsActorBase::PhysicsActorBase(const PhysicsActorBase& Other) = default;

PhysicsActorBase& PhysicsActorBase::operator=(const PhysicsActorBase& Other) = default;

PhysicsActorBase::PhysicsActorBase(PhysicsActorBase&& Other) noexcept = default;

PhysicsActorBase& PhysicsActorBase::operator=(PhysicsActorBase&& Other) noexcept = default;

PhysicsActorBase::PhysicsActorBase(std::string Name)
    : PhysicsActorBase{} {
    mName = std::move(Name);
}

PhysicsActorBase::PhysicsActorBase(const ActorDesc& Desc)
    : PhysicsActorBase{} {
    mName = Desc.Name;
    mRigidBody.mPosition = Desc.Position;
    mRigidBody.mOrientation = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Desc.Rotation.y, Desc.Rotation.x, Desc.Rotation.z);
    NormalizeRigidBodyOrientation();
    mRigidBody.mScale = Desc.Scale;
    mRigidBody.mVelocity = Desc.Velocity;
    mRigidBody.mAcceleration = Desc.Acceleration;
    mRigidBody.mRestitution = Desc.Restitution;
    mRigidBody.mLinearDamping = Desc.LinearDamping;
    mRigidBody.mAngularDamping = Desc.AngularDamping;
    mSleepThreshold = Desc.SleepThreshold;
    mBoundingBoxFatMargin = Desc.BoundingBoxFatMargin;

    SetIsActive(Desc.IsActive);
    SetMass(Desc.Mass);
    SetFriction(Desc.Friction);
    SetFlags(Desc.Flags);
    SetActorType(Desc.ActorType);
    SetLocalBoundingBox(Desc.LocalBoundingBox);
    SetVelocity(Desc.Velocity);
    SetAcceleration(Desc.Acceleration);
    UpdateWorldBoundingBox();
    UpdateInverseInertiaTensorWorld();
    SetIsSleeping(Desc.IsSleeping);
}

void PhysicsActorBase::SetName(std::string Name) {
    mName = std::move(Name);
}

const std::string& PhysicsActorBase::GetName() const {
    return mName;
}

void PhysicsActorBase::SetIsActive(bool IsActive) {
    mIsActive = IsActive;
}

bool PhysicsActorBase::GetIsActive() const {
    return mIsActive;
}

void PhysicsActorBase::SetMass(float Mass) {
    mRigidBody.mMass = std::max(0.0F, Mass);
    mRigidBody.mInverseMass = mRigidBody.mMass > 0.0F ? (1.0F / mRigidBody.mMass) : 0.0F;
    UpdateInverseInertiaTensorWorld();
}

float PhysicsActorBase::GetMass() const {
    return mRigidBody.mMass;
}

void PhysicsActorBase::SetInverseMass(float InverseMass) {
    mRigidBody.mInverseMass = std::max(0.0F, InverseMass);
    mRigidBody.mMass = mRigidBody.mInverseMass > 0.0F ? (1.0F / mRigidBody.mInverseMass) : 0.0F;
    UpdateInverseInertiaTensorWorld();
}

float PhysicsActorBase::GetInverseMass() const {
    return mRigidBody.mInverseMass;
}

void PhysicsActorBase::SetFriction(float Friction) {
    mRigidBody.mFriction = std::max(0.0F, Friction);
}

float PhysicsActorBase::GetFriction() const {
    return mRigidBody.mFriction;
}

void PhysicsActorBase::SetLocalInertiaTensor(const DirectX::SimpleMath::Matrix& LocalInertiaTensor) {
    mRigidBody.mLocalInertiaTensor = LocalInertiaTensor;
}

const DirectX::SimpleMath::Matrix& PhysicsActorBase::GetLocalInertiaTensor() const {
    return mRigidBody.mLocalInertiaTensor;
}

void PhysicsActorBase::SetLocalInverseInertiaTensor(const DirectX::SimpleMath::Matrix& LocalInverseInertiaTensor) {
    mRigidBody.mLocalInverseInertiaTensor = LocalInverseInertiaTensor;
    UpdateInverseInertiaTensorWorld();
}

const DirectX::SimpleMath::Matrix& PhysicsActorBase::GetLocalInverseInertiaTensor() const {
    return mRigidBody.mLocalInverseInertiaTensor;
}

void PhysicsActorBase::SetLinearMomentum(const DirectX::SimpleMath::Vector3& LinearMomentum) {
    mRigidBody.mLinearMomentum = LinearMomentum;
}

const DirectX::SimpleMath::Vector3& PhysicsActorBase::GetLinearMomentum() const {
    return mRigidBody.mLinearMomentum;
}

void PhysicsActorBase::SetAngularMomentum(const DirectX::SimpleMath::Vector3& AngularMomentum) {
    mRigidBody.mAngularMomentum = AngularMomentum;
}

const DirectX::SimpleMath::Vector3& PhysicsActorBase::GetAngularMomentum() const {
    return mRigidBody.mAngularMomentum;
}

void PhysicsActorBase::SetAngularVelocity(const DirectX::SimpleMath::Vector3& AngularVelocity) {
    mRigidBody.mAngularVelocity = AngularVelocity;
}

const DirectX::SimpleMath::Vector3& PhysicsActorBase::GetAngularVelocity() const {
    return mRigidBody.mAngularVelocity;
}

void PhysicsActorBase::AddTorque(const DirectX::SimpleMath::Vector3& Torque) {
    mRigidBody.mTorque += Torque;
    SetIsSleeping(false);
}

const DirectX::SimpleMath::Vector3& PhysicsActorBase::GetTorque() const {
    return mRigidBody.mTorque;
}

void PhysicsActorBase::ClearTorque() {
    mRigidBody.mTorque = DirectX::SimpleMath::Vector3{};
}

void PhysicsActorBase::AddAngularImpulse(const DirectX::SimpleMath::Vector3& AngularImpulse) {
    DirectX::SimpleMath::Vector3 NextAngularMomentum{ GetAngularMomentum() + AngularImpulse };
    SetAngularMomentum(NextAngularMomentum);
    mRigidBody.mAngularVelocity = DirectX::SimpleMath::Vector3::TransformNormal(NextAngularMomentum, mRigidBody.mInverseInertiaTensorWorld);
    SetIsSleeping(false);
}

void PhysicsActorBase::ApplyImpulseAtPoint(const DirectX::SimpleMath::Vector3& Impulse, const DirectX::SimpleMath::Vector3& WorldPoint) {
    AddImpulse(Impulse);
    DirectX::SimpleMath::Vector3 ContactOffset{ WorldPoint - GetPosition() };
    DirectX::SimpleMath::Vector3 AngularImpulse{ ContactOffset.Cross(Impulse) };
    AddAngularImpulse(AngularImpulse);
}

const DirectX::SimpleMath::Matrix& PhysicsActorBase::GetInverseInertiaTensorWorld() const {
    return mRigidBody.mInverseInertiaTensorWorld;
}

void PhysicsActorBase::SetRigidBody(const RigidBody& RigidBodyState) {
    mRigidBody = RigidBodyState;
    NormalizeRigidBodyOrientation();
    UpdateInverseInertiaTensorWorld();
    UpdateWorldBoundingBox();
}

const RigidBody& PhysicsActorBase::GetRigidBody() const {
    return mRigidBody;
}

void PhysicsActorBase::SetFlags(PhysicsActorFlags Flags) {
    mFlags = Flags;
}

PhysicsActorBase::PhysicsActorFlags PhysicsActorBase::GetFlags() const {
    return mFlags;
}

bool PhysicsActorBase::HasFlag(PhysicsActorFlags Flag) const {
    PhysicsActorFlags BitAndResult{ mFlags & Flag };
    return BitAndResult != PhysicsActorFlags::None;
}

void PhysicsActorBase::SetActorType(PhysicsActorType ActorType) {
    mActorType = ActorType;
}

PhysicsActorBase::PhysicsActorType PhysicsActorBase::GetActorType() const {
    return mActorType;
}

bool PhysicsActorBase::IsTerrainActor() const {
    return false;
}

void PhysicsActorBase::SetLocalBoundingBox(const DirectX::BoundingOrientedBox& LocalBoundingBox) {
    mLocalBoundingBox = LocalBoundingBox;
    UpdateWorldBoundingBox();
}

const DirectX::BoundingOrientedBox& PhysicsActorBase::GetLocalBoundingBox() const {
    return mLocalBoundingBox;
}

const DirectX::BoundingOrientedBox& PhysicsActorBase::GetWorldBoundingBox() const {
    return mWorldBoundingBox;
}

const DirectX::BoundingOrientedBox& PhysicsActorBase::GetFatWorldBoundingBox() const {
    return mFatWorldBoundingBox;
}

void PhysicsActorBase::SetPosition(const DirectX::SimpleMath::Vector3& Position) {
    mRigidBody.mPosition = Position;
    UpdateWorldBoundingBox();
}

const DirectX::SimpleMath::Vector3& PhysicsActorBase::GetPosition() const {
    return mRigidBody.mPosition;
}

void PhysicsActorBase::SetRotation(const DirectX::SimpleMath::Vector3& Rotation) {
    mRigidBody.mOrientation = DirectX::SimpleMath::Quaternion::CreateFromYawPitchRoll(Rotation.y, Rotation.x, Rotation.z);
    NormalizeRigidBodyOrientation();
    UpdateInverseInertiaTensorWorld();
    UpdateWorldBoundingBox();
}

DirectX::SimpleMath::Vector3 PhysicsActorBase::GetRotation() const {
    DirectX::SimpleMath::Quaternion NormalizedOrientation{ mRigidBody.mOrientation };
    if (NormalizedOrientation.LengthSquared() <= 0.0F) {
        NormalizedOrientation = DirectX::SimpleMath::Quaternion{ 0.0F, 0.0F, 0.0F, 1.0F };
    } else {
        NormalizedOrientation.Normalize();
    }

    DirectX::SimpleMath::Vector3 Rotation{ NormalizedOrientation.ToEuler() };
    return Rotation;
}

void PhysicsActorBase::SetOrientation(const DirectX::SimpleMath::Quaternion& Orientation) {
    mRigidBody.mOrientation = Orientation;
    NormalizeRigidBodyOrientation();
    UpdateInverseInertiaTensorWorld();
    UpdateWorldBoundingBox();
}

const DirectX::SimpleMath::Quaternion& PhysicsActorBase::GetOrientation() const {
    return mRigidBody.mOrientation;
}

void PhysicsActorBase::SetScale(const DirectX::SimpleMath::Vector3& Scale) {
    mRigidBody.mScale = Scale;
    UpdateWorldBoundingBox();
}

const DirectX::SimpleMath::Vector3& PhysicsActorBase::GetScale() const {
    return mRigidBody.mScale;
}

void PhysicsActorBase::SetVelocity(const DirectX::SimpleMath::Vector3& Velocity) {
    mRigidBody.mVelocity = Velocity;
    SetLinearMomentum(mRigidBody.mVelocity * GetMass());
    UpdateFatWorldBoundingBox();
}

const DirectX::SimpleMath::Vector3& PhysicsActorBase::GetVelocity() const {
    return mRigidBody.mVelocity;
}

void PhysicsActorBase::SetAcceleration(const DirectX::SimpleMath::Vector3& Acceleration) {
    mRigidBody.mAcceleration = Acceleration;
}

const DirectX::SimpleMath::Vector3& PhysicsActorBase::GetAcceleration() const {
    return mRigidBody.mAcceleration;
}

void PhysicsActorBase::AddForce(const DirectX::SimpleMath::Vector3& Force) {
    mRigidBody.mAccumulatedForce += Force;
    SetIsSleeping(false);
}

const DirectX::SimpleMath::Vector3& PhysicsActorBase::GetAccumulatedForce() const {
    return mRigidBody.mAccumulatedForce;
}

void PhysicsActorBase::ClearAccumulatedForce() {
    mRigidBody.mAccumulatedForce = DirectX::SimpleMath::Vector3{};
}

void PhysicsActorBase::AddImpulse(const DirectX::SimpleMath::Vector3& Impulse) {
    DirectX::SimpleMath::Vector3 NextLinearMomentum{ GetLinearMomentum() + Impulse };
    SetLinearMomentum(NextLinearMomentum);
    float InverseMass{ GetInverseMass() };
    mRigidBody.mVelocity = InverseMass > 0.0F ? NextLinearMomentum * InverseMass : DirectX::SimpleMath::Vector3{};
    UpdateFatWorldBoundingBox();
    SetIsSleeping(false);
}

void PhysicsActorBase::SetRestitution(float Restitution) {
    mRigidBody.mRestitution = std::clamp(Restitution, 0.0F, 1.0F);
}

float PhysicsActorBase::GetRestitution() const {
    return mRigidBody.mRestitution;
}

void PhysicsActorBase::SetLinearDamping(float LinearDamping) {
    mRigidBody.mLinearDamping = std::max(LinearDamping, 0.0F);
}

float PhysicsActorBase::GetLinearDamping() const {
    return mRigidBody.mLinearDamping;
}

void PhysicsActorBase::SetAngularDamping(float AngularDamping) {
    mRigidBody.mAngularDamping = std::max(AngularDamping, 0.0F);
}

float PhysicsActorBase::GetAngularDamping() const {
    return mRigidBody.mAngularDamping;
}

void PhysicsActorBase::SetSleepThreshold(float SleepThreshold) {
    mSleepThreshold = std::max(SleepThreshold, 0.0F);
}

float PhysicsActorBase::GetSleepThreshold() const {
    return mSleepThreshold;
}

void PhysicsActorBase::SetBoundingBoxFatMargin(float BoundingBoxFatMargin) {
    mBoundingBoxFatMargin = std::max(BoundingBoxFatMargin, 0.0F);
    UpdateFatWorldBoundingBox();
}

float PhysicsActorBase::GetBoundingBoxFatMargin() const {
    return mBoundingBoxFatMargin;
}

void PhysicsActorBase::SetIsSleeping(bool IsSleeping) {
    mIsSleeping = IsSleeping;
    if (mIsSleeping) {
        SetFlags(GetFlags() | PhysicsActorFlags::Sleeping);
        mRigidBody.mVelocity = DirectX::SimpleMath::Vector3{};
        mRigidBody.mAcceleration = DirectX::SimpleMath::Vector3{};
        mRigidBody.mAccumulatedForce = DirectX::SimpleMath::Vector3{};
        mRigidBody.mAngularVelocity = DirectX::SimpleMath::Vector3{};
        mRigidBody.mTorque = DirectX::SimpleMath::Vector3{};
        SetLinearMomentum(DirectX::SimpleMath::Vector3{});
        SetAngularMomentum(DirectX::SimpleMath::Vector3{});
        UpdateFatWorldBoundingBox();
        return;
    }

    PhysicsActorFlags UpdatedFlags{ GetFlags() };
    std::uint32_t UpdatedValue{ static_cast<std::uint32_t>(UpdatedFlags) & ~static_cast<std::uint32_t>(PhysicsActorFlags::Sleeping) };
    SetFlags(static_cast<PhysicsActorFlags>(UpdatedValue));
}

bool PhysicsActorBase::GetIsSleeping() const {
    return mIsSleeping;
}

void PhysicsActorBase::ClearContactState() {
    mContactNormals.clear();
}

void PhysicsActorBase::RegisterContactNormal(const DirectX::SimpleMath::Vector3& ContactNormal) {
    float ContactNormalLengthSquared{ ContactNormal.LengthSquared() };
    if (ContactNormalLengthSquared <= (ContactNormalLengthEpsilon * ContactNormalLengthEpsilon)) {
        return;
    }

    DirectX::SimpleMath::Vector3 NormalizedContactNormal{ ContactNormal / std::sqrt(ContactNormalLengthSquared) };
    mContactNormals.push_back(NormalizedContactNormal);
}

bool PhysicsActorBase::HasSupportingContact(const DirectX::SimpleMath::Vector3& Gravity) const {
    float GravityLengthSquared{ Gravity.LengthSquared() };
    if (GravityLengthSquared <= (ContactNormalLengthEpsilon * ContactNormalLengthEpsilon)) {
        return false;
    }

    DirectX::SimpleMath::Vector3 AntiGravityDirection{ -Gravity / std::sqrt(GravityLengthSquared) };
    std::size_t ContactNormalCount{ mContactNormals.size() };
    for (std::size_t ContactNormalIndex{ 0U }; ContactNormalIndex < ContactNormalCount; ++ContactNormalIndex) {
        float SupportDot{ mContactNormals[ContactNormalIndex].Dot(AntiGravityDirection) };
        if (SupportDot >= SupportingContactMinimumDot) {
            return true;
        }
    }

    return false;
}

void PhysicsActorBase::MoveToTarget(const DirectX::SimpleMath::Vector3& TargetPosition, float DeltaTime) {
    if (DeltaTime <= 0.0F) {
        return;
    }

    DirectX::SimpleMath::Vector3 NextVelocity{ (TargetPosition - mRigidBody.mPosition) / DeltaTime };
    SetVelocity(NextVelocity);
    SetPosition(TargetPosition);
}

void PhysicsActorBase::UpdateSleepState(const DirectX::SimpleMath::Vector3& Gravity) {
    float VelocityLengthSquared{ mRigidBody.mVelocity.LengthSquared() };
    float AngularVelocityLengthSquared{ mRigidBody.mAngularVelocity.LengthSquared() };
    float AccelerationLengthSquared{ mRigidBody.mAcceleration.LengthSquared() };
    DirectX::SimpleMath::Vector3 ForceAcceleration{ mRigidBody.mInverseMass > 0.0F ? mRigidBody.mAccumulatedForce * mRigidBody.mInverseMass : DirectX::SimpleMath::Vector3{} };
    float ForceAccelerationLengthSquared{ ForceAcceleration.LengthSquared() };
    DirectX::SimpleMath::Vector3 AngularAcceleration{ DirectX::SimpleMath::Vector3::TransformNormal(mRigidBody.mTorque, mRigidBody.mInverseInertiaTensorWorld) };
    float AngularAccelerationLengthSquared{ AngularAcceleration.LengthSquared() };
    float GravityLengthSquared{ Gravity.LengthSquared() };
    float ThresholdSquared{ mSleepThreshold * mSleepThreshold };
    bool HasUnsupportedGravity{ GravityLengthSquared > ThresholdSquared && !HasSupportingContact(Gravity) };
    bool HasAcceleration{ AccelerationLengthSquared > ThresholdSquared || ForceAccelerationLengthSquared > ThresholdSquared || AngularAccelerationLengthSquared > ThresholdSquared || HasUnsupportedGravity };
    bool ShouldSleep{ VelocityLengthSquared <= ThresholdSquared && AngularVelocityLengthSquared <= ThresholdSquared && !HasAcceleration };
    SetIsSleeping(ShouldSleep);
}

void PhysicsActorBase::UpdateWorldBoundingBox() {
    NormalizeRigidBodyOrientation();
    DirectX::SimpleMath::Matrix ScalingMatrix{ DirectX::SimpleMath::Matrix::CreateScale(mRigidBody.mScale) };
    DirectX::SimpleMath::Matrix RotationMatrix{ DirectX::SimpleMath::Matrix::CreateFromQuaternion(mRigidBody.mOrientation) };
    DirectX::SimpleMath::Matrix TranslationMatrix{ DirectX::SimpleMath::Matrix::CreateTranslation(mRigidBody.mPosition) };
    DirectX::SimpleMath::Matrix WorldMatrix{ ScalingMatrix * RotationMatrix * TranslationMatrix };
    mLocalBoundingBox.Transform(mWorldBoundingBox, WorldMatrix);
    UpdateFatWorldBoundingBox();
}

void PhysicsActorBase::NormalizeRigidBodyOrientation() {
    if (mRigidBody.mOrientation.LengthSquared() <= 0.0F) {
        mRigidBody.mOrientation = DirectX::SimpleMath::Quaternion{ 0.0F, 0.0F, 0.0F, 1.0F };
        return;
    }

    mRigidBody.mOrientation.Normalize();
}

void PhysicsActorBase::UpdateInverseInertiaTensorWorld() {
    if (mRigidBody.mInverseMass <= 0.0F) {
        mRigidBody.mInverseInertiaTensorWorld = MakeZeroMatrix();
        mRigidBody.mAngularVelocity = DirectX::SimpleMath::Vector3{};
        mRigidBody.mAngularMomentum = DirectX::SimpleMath::Vector3{};
        mRigidBody.mTorque = DirectX::SimpleMath::Vector3{};
        return;
    }

    NormalizeRigidBodyOrientation();
    DirectX::SimpleMath::Matrix RotationMatrix{ DirectX::SimpleMath::Matrix::CreateFromQuaternion(mRigidBody.mOrientation) };
    DirectX::SimpleMath::Matrix RotationTransposeMatrix{ RotationMatrix.Transpose() };
    mRigidBody.mInverseInertiaTensorWorld = RotationTransposeMatrix * mRigidBody.mLocalInverseInertiaTensor * RotationMatrix;
}

void PhysicsActorBase::UpdateFatWorldBoundingBox() {
    mFatWorldBoundingBox = mWorldBoundingBox;
    float VelocityBasedMargin{ std::sqrt(mRigidBody.mVelocity.LengthSquared()) * 0.05F };
    float FinalMargin{ mBoundingBoxFatMargin + VelocityBasedMargin };
    mFatWorldBoundingBox.Extents.x += FinalMargin;
    mFatWorldBoundingBox.Extents.y += FinalMargin;
    mFatWorldBoundingBox.Extents.z += FinalMargin;
}

PhysicsActorBase::PhysicsActorFlags operator|(PhysicsActorBase::PhysicsActorFlags Left, PhysicsActorBase::PhysicsActorFlags Right) {
    std::uint32_t LeftValue{ static_cast<std::uint32_t>(Left) };
    std::uint32_t RightValue{ static_cast<std::uint32_t>(Right) };
    PhysicsActorBase::PhysicsActorFlags Combined{ static_cast<PhysicsActorBase::PhysicsActorFlags>(LeftValue | RightValue) };
    return Combined;
}

PhysicsActorBase::PhysicsActorFlags operator&(PhysicsActorBase::PhysicsActorFlags Left, PhysicsActorBase::PhysicsActorFlags Right) {
    std::uint32_t LeftValue{ static_cast<std::uint32_t>(Left) };
    std::uint32_t RightValue{ static_cast<std::uint32_t>(Right) };
    PhysicsActorBase::PhysicsActorFlags Intersected{ static_cast<PhysicsActorBase::PhysicsActorFlags>(LeftValue & RightValue) };
    return Intersected;
}
