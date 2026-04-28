local WalkMoveSpeed = 4.0
local RunMoveSpeed = 9.0
local WalkMoveForce = 24.0
local RunMoveForce = 52.0
local BrakingForce = 48.0
local StopSpeed = 0.08
local JumpImpulse = 8.0
local MaxRotationDegreesPerSecond = 720.0
local IsMovingParameterIndex = 0
local CurrentSpeedParameterIndex = 1
local DirectionDeltaDegreesParameterIndex = 2
local Pi = math.pi
local TwoPi = Pi * 2.0

local function BuildMoveInput()
    local MoveInput = Vector2.new(0.0, 0.0)
    MoveInput.x = GetAxisFromKeys(A, D)
    MoveInput.y = GetAxisFromKeys(W, S)

    local InputLengthSquared = (MoveInput.x * MoveInput.x) + (MoveInput.y * MoveInput.y)
    if InputLengthSquared > 1.0 then
        local InverseLength = 1.0 / math.sqrt(InputLengthSquared)
        MoveInput.x = MoveInput.x * InverseLength
        MoveInput.y = MoveInput.y * InverseLength
    end

    return MoveInput
end

local function NormalizeAngleRadians(Value)
    local NormalizedValue = Value

    while NormalizedValue > Pi do
        NormalizedValue = NormalizedValue - TwoPi
    end

    while NormalizedValue < -Pi do
        NormalizedValue = NormalizedValue + TwoPi
    end

    return NormalizedValue
end

local function BuildFinalTargetDirection(MoveInput)
    local CameraForward = GetActiveCameraForwardDirection()
    local CameraRight = GetActiveCameraRightDirection()

    local FlatCameraForward = NormalizeVector3(Vector3.new(-CameraForward.x, 0.0, -CameraForward.z))
    local FlatCameraRight = NormalizeVector3(Vector3.new(-CameraRight.x, 0.0, -CameraRight.z))

    local FinalTargetDirection = AddVector3(ScaleVector3(FlatCameraForward, MoveInput.y), ScaleVector3(FlatCameraRight, MoveInput.x))
    return NormalizeVector3(FinalTargetDirection)
end

local function ApplySmoothedYawRotation(TransformComponent, FinalTargetDirection, DeltaSeconds)
    if Vector3LengthSquared(FinalTargetDirection) <= 0.0 then
        return 0.0
    end

    local TargetYawRadians = math.atan(FinalTargetDirection.x, FinalTargetDirection.z)
    local CurrentYawRadians = TransformComponent.rotationEuler.y
    local MaxYawDeltaRadians = math.rad(MaxRotationDegreesPerSecond) * DeltaSeconds
    local YawDeltaRadians = NormalizeAngleRadians(TargetYawRadians - CurrentYawRadians)
    local ClampedYawDeltaRadians = Clamp(YawDeltaRadians, -MaxYawDeltaRadians, MaxYawDeltaRadians)
    local NextYawRadians = CurrentYawRadians + ClampedYawDeltaRadians

    local CurrentEulerRadians = TransformComponent.rotationEuler
    TransformComponent.rotationEuler = Vector3.new(CurrentEulerRadians.x, NextYawRadians, CurrentEulerRadians.z)
    TransformComponent:UpdateRotationFromEulerRadians()

    return NormalizeAngleRadians(TargetYawRadians - NextYawRadians)
end

local function IsRunInputDown()
    return IsInputKeyDown(LeftShift) or IsInputKeyDown(RightShift)
end

local function IsActiveCameraFreeLookMode()
    return IsFlagEnabled(GetActiveCameraFlags(), CameraFlagFreeLook)
end

local function GetHorizontalVelocity(Velocity)
    return Vector3.new(Velocity.x, 0.0, Velocity.z)
end

local function ApplyHorizontalVelocityLimit(PhysicsActorComponent, CurrentVelocity, MaxSpeed)
    local HorizontalVelocity = GetHorizontalVelocity(CurrentVelocity)
    local HorizontalSpeed = Vector3Length(HorizontalVelocity)

    if HorizontalSpeed <= MaxSpeed then
        return HorizontalSpeed
    end

    local ClampedHorizontalVelocity = ScaleVector3(NormalizeVector3(HorizontalVelocity), MaxSpeed)
    PhysicsActorComponent:SetVelocity(Vector3.new(ClampedHorizontalVelocity.x, CurrentVelocity.y, ClampedHorizontalVelocity.z))
    return MaxSpeed
end

local function ApplyMovementForce(Context, TransformComponent, MoveInput)
    local PhysicsActorComponent = Context:GetComponent("PhysicsActor")
    if PhysicsActorComponent == nil or PhysicsActorComponent:HasActor() == false then
        return 0.0
    end

    local InputMagnitude = math.sqrt((MoveInput.x * MoveInput.x) + (MoveInput.y * MoveInput.y))
    local CurrentForwardDirection = TransformComponent:GetForwardDirection()
    local FlatForwardDirection = NormalizeVector3(Vector3.new(CurrentForwardDirection.x, 0.0, CurrentForwardDirection.z))
    local CurrentVelocity = PhysicsActorComponent:GetVelocity()
    local HorizontalVelocity = GetHorizontalVelocity(CurrentVelocity)
    local HorizontalSpeed = Vector3Length(HorizontalVelocity)

    if InputMagnitude <= 0.0 or IsZeroVector3(FlatForwardDirection) then
        if HorizontalSpeed <= StopSpeed then
            PhysicsActorComponent:SetVelocity(Vector3.new(0.0, CurrentVelocity.y, 0.0))
            return 0.0
        end

        local BrakeForce = ScaleVector3(HorizontalVelocity, -BrakingForce)
        PhysicsActorComponent:AddForce(Vector3.new(BrakeForce.x, 0.0, BrakeForce.z))
        return HorizontalSpeed
    end

    local IsRunning = IsRunInputDown()
    local TargetSpeed = WalkMoveSpeed
    local MoveForce = WalkMoveForce
    if IsRunning then
        TargetSpeed = RunMoveSpeed
        MoveForce = RunMoveForce
    end

    TargetSpeed = TargetSpeed * InputMagnitude
    HorizontalSpeed = ApplyHorizontalVelocityLimit(PhysicsActorComponent, CurrentVelocity, TargetSpeed * 1.35)
    CurrentVelocity = PhysicsActorComponent:GetVelocity()
    HorizontalVelocity = GetHorizontalVelocity(CurrentVelocity)

    local DesiredVelocity = ScaleVector3(FlatForwardDirection, TargetSpeed)
    local VelocityDelta = SubtractVector3(DesiredVelocity, HorizontalVelocity)
    local MovementForce = ScaleVector3(VelocityDelta, MoveForce)
    PhysicsActorComponent:AddForce(Vector3.new(MovementForce.x, 0.0, MovementForce.z))

    return HorizontalSpeed
end

local function ApplyJumpImpulse(Context)
    if IsInputKeyPressed(Space) == false then
        return
    end

    local PhysicsActorComponent = Context:GetComponent("PhysicsActor")
    if PhysicsActorComponent == nil or PhysicsActorComponent:HasActor() == false then
        return
    end

    PhysicsActorComponent:AddImpulse(Vector3.new(0.0, JumpImpulse, 0.0))
end

local function SetMovingState(RuntimeVariableTableComponent, IsMoving)
    if RuntimeVariableTableComponent == nil then
        return
    end

    RuntimeVariableTableComponent.BoolValues:Set(IsMovingParameterIndex, IsMoving)
end

local function SetMotionParameters(RuntimeVariableTableComponent, CurrentSpeed, DirectionDeltaDegrees)
    if RuntimeVariableTableComponent == nil then
        return
    end

    RuntimeVariableTableComponent.FloatValues:Set(CurrentSpeedParameterIndex, CurrentSpeed)
    RuntimeVariableTableComponent.FloatValues:Set(DirectionDeltaDegreesParameterIndex, DirectionDeltaDegrees)
end

function Awake(Context)
end

function OnEnable(Context)
end

function Start(Context)
end

function Update(Context, DeltaSeconds)
    local TransformComponent = Context:GetComponent("Transform")
    if TransformComponent == nil then
        return
    end

    local RuntimeVariableTableComponent = Context:GetComponent("RuntimeVariableTable")
    if IsActiveCameraFreeLookMode() then
        SetMovingState(RuntimeVariableTableComponent, false)
        SetMotionParameters(RuntimeVariableTableComponent, 0.0, 0.0)
        return
    end

    local MoveInput = BuildMoveInput()
    local FinalTargetDirection = BuildFinalTargetDirection(MoveInput)
    local RemainingDirectionDeltaRadians = ApplySmoothedYawRotation(TransformComponent, FinalTargetDirection, DeltaSeconds)
    local CurrentSpeed = ApplyMovementForce(Context, TransformComponent, MoveInput)
    local IsMoving = CurrentSpeed > 0.0

    ApplyJumpImpulse(Context)
    SetMovingState(RuntimeVariableTableComponent, IsMoving)
    SetMotionParameters(RuntimeVariableTableComponent, CurrentSpeed, math.deg(RemainingDirectionDeltaRadians))
end

function FixedUpdate(Context, FixedDeltaSeconds, FixedTick)
end

function LateUpdate(Context, DeltaSeconds)
end

function OnDisable(Context)
end

function OnDestroy(Context)
end
