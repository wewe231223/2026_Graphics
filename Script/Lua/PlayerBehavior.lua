local WalkMoveSpeed = 5.0
local RunMoveSpeed = 15.0
local GroundRayLength = 1.35
local GroundedDistance = 0.98
local MaxRotationDegreesPerSecond = 720.0
local IsMovingParameterIndex = 0
local CurrentSpeedParameterIndex = 1
local DirectionDeltaDegreesParameterIndex = 2
local JumpParameterIndex = 3
local IsLandingParameterIndex = 4
local IsRunningParameterIndex = 5
local LandAnimationClipIndex = 3
local Pi = math.pi
local TwoPi = Pi * 2.0
local HasAnimationGroundState = false
local WasAnimationGrounded = false

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

local function IsLandClipPlaying(Context)
    local AnimatorGraphPlayerComponent = Context:GetComponent("AnimatorGraphPlayer")
    if AnimatorGraphPlayerComponent == nil then
        return false
    end

    return AnimatorGraphPlayerComponent.SampleSourceClipIndex == LandAnimationClipIndex or AnimatorGraphPlayerComponent.SampleDestinationClipIndex == LandAnimationClipIndex
end

local function GetHorizontalVelocity(Velocity)
    return Vector3.new(Velocity.x, 0.0, Velocity.z)
end

local function ApplyCharacterMovement(CharacterControllerComponent, TransformComponent, MoveInput, IsRunningInput)
    if CharacterControllerComponent == nil then
        return 0.0
    end

    local InputMagnitude = math.sqrt((MoveInput.x * MoveInput.x) + (MoveInput.y * MoveInput.y))
    local CurrentForwardDirection = TransformComponent:GetForwardDirection()
    local FlatForwardDirection = NormalizeVector3(Vector3.new(CurrentForwardDirection.x, 0.0, CurrentForwardDirection.z))
    local CurrentVelocity = CharacterControllerComponent:GetVelocity()
    local HorizontalVelocity = GetHorizontalVelocity(CurrentVelocity)
    local HorizontalSpeed = Vector3Length(HorizontalVelocity)

    if InputMagnitude <= 0.0 or IsZeroVector3(FlatForwardDirection) then
        CharacterControllerComponent:SetDesiredHorizontalVelocity(Vector3.new(0.0, 0.0, 0.0))
        return HorizontalSpeed
    end

    local TargetSpeed = WalkMoveSpeed
    if IsRunningInput then
        TargetSpeed = RunMoveSpeed
    end

    TargetSpeed = TargetSpeed * InputMagnitude
    local DesiredVelocity = ScaleVector3(FlatForwardDirection, TargetSpeed)
    CharacterControllerComponent:SetDesiredHorizontalVelocity(DesiredVelocity)

    return HorizontalSpeed
end

local function GetAnimationGroundDistance(TransformComponent)
    if TransformComponent == nil then
        return -1.0
    end

    return RaycastTerrainDistance(TransformComponent.position, Vector3.new(0.0, -1.0, 0.0), GroundRayLength)
end

local function UpdateAnimationGroundState(TransformComponent)
    local GroundDistance = GetAnimationGroundDistance(TransformComponent)
    local IsAnimationGrounded = GroundDistance >= 0.0 and GroundDistance <= GroundedDistance
    local IsLandingDetected = HasAnimationGroundState == true and WasAnimationGrounded == false and IsAnimationGrounded == true

    WasAnimationGrounded = IsAnimationGrounded
    HasAnimationGroundState = true
    return IsLandingDetected
end

local function ApplyJumpRequest(CharacterControllerComponent, IsGrounded, IsJumpInputLocked)
    if IsJumpInputLocked == true then
        return false
    end

    if IsInputKeyPressed(Space) == false then
        return false
    end

    if IsGrounded == false then
        return false
    end

    if CharacterControllerComponent == nil then
        return false
    end

    CharacterControllerComponent:RequestJump()
    return true
end

local function SetMovingState(RuntimeVariableTableComponent, IsMoving)
    if RuntimeVariableTableComponent == nil then
        return
    end

    RuntimeVariableTableComponent.BoolValues:Set(IsMovingParameterIndex, IsMoving)
end

local function SetRunningState(RuntimeVariableTableComponent, IsRunning)
    if RuntimeVariableTableComponent == nil then
        return
    end

    RuntimeVariableTableComponent.BoolValues:Set(IsRunningParameterIndex, IsRunning)
end

local function SetMotionParameters(RuntimeVariableTableComponent, CurrentSpeed, DirectionDeltaDegrees)
    if RuntimeVariableTableComponent == nil then
        return
    end

    RuntimeVariableTableComponent.FloatValues:Set(CurrentSpeedParameterIndex, CurrentSpeed)
    RuntimeVariableTableComponent.FloatValues:Set(DirectionDeltaDegreesParameterIndex, DirectionDeltaDegrees)
end

local function SetJumpTrigger(RuntimeVariableTableComponent)
    if RuntimeVariableTableComponent == nil then
        return
    end

    RuntimeVariableTableComponent.BoolValues:Set(JumpParameterIndex, true)
    RuntimeVariableTableComponent.TriggerConsumed:Set(JumpParameterIndex, false)
end

local function SetLandingTrigger(RuntimeVariableTableComponent)
    if RuntimeVariableTableComponent == nil then
        return
    end

    RuntimeVariableTableComponent.BoolValues:Set(IsLandingParameterIndex, true)
    RuntimeVariableTableComponent.TriggerConsumed:Set(IsLandingParameterIndex, false)
end

local function ClearLandingTrigger(RuntimeVariableTableComponent)
    if RuntimeVariableTableComponent == nil then
        return
    end

    RuntimeVariableTableComponent.BoolValues:Set(IsLandingParameterIndex, false)
    RuntimeVariableTableComponent.TriggerConsumed:Set(IsLandingParameterIndex, false)
end

function Awake(Context)
end

function OnEnable(Context)
    HasAnimationGroundState = false
    WasAnimationGrounded = false
end

function Start(Context)
end

function Update(Context, DeltaSeconds)
    local TransformComponent = Context:GetComponent("Transform")
    if TransformComponent == nil then
        return
    end

    local RuntimeVariableTableComponent = Context:GetComponent("RuntimeVariableTable")
    local CharacterControllerComponent = Context:GetComponent("CharacterController")
    if IsActiveCameraFreeLookMode() then
        UpdateAnimationGroundState(TransformComponent)

        if CharacterControllerComponent ~= nil then
            CharacterControllerComponent:SetDesiredHorizontalVelocity(Vector3.new(0.0, 0.0, 0.0))
        end

        SetMovingState(RuntimeVariableTableComponent, false)
        SetRunningState(RuntimeVariableTableComponent, false)
        SetMotionParameters(RuntimeVariableTableComponent, 0.0, 0.0)
        ClearLandingTrigger(RuntimeVariableTableComponent)
        return
    end

    local IsLandClipActive = IsLandClipPlaying(Context)
    local IsLandingDetected = UpdateAnimationGroundState(TransformComponent)
    local IsControllerGrounded = CharacterControllerComponent ~= nil and CharacterControllerComponent:GetIsGrounded()
    local IsInputLocked = IsLandClipActive
    local IsRunningInput = IsInputLocked == false and IsRunInputDown()
    local MoveInput = BuildMoveInput()
    if IsInputLocked == true then
        MoveInput = Vector2.new(0.0, 0.0)
    end

    local FinalTargetDirection = BuildFinalTargetDirection(MoveInput)
    local RemainingDirectionDeltaRadians = ApplySmoothedYawRotation(TransformComponent, FinalTargetDirection, DeltaSeconds)
    local CurrentSpeed = ApplyCharacterMovement(CharacterControllerComponent, TransformComponent, MoveInput, IsRunningInput)
    local IsMoving = CurrentSpeed > 0.0
    local IsRunning = IsMoving and IsRunningInput
    local IsJumpStarted = ApplyJumpRequest(CharacterControllerComponent, IsControllerGrounded, IsInputLocked)

    if IsJumpStarted == true then
        SetJumpTrigger(RuntimeVariableTableComponent)
        ClearLandingTrigger(RuntimeVariableTableComponent)
    elseif IsLandingDetected == true then
        SetLandingTrigger(RuntimeVariableTableComponent)
    else
        ClearLandingTrigger(RuntimeVariableTableComponent)
    end

    SetMovingState(RuntimeVariableTableComponent, IsMoving)
    SetRunningState(RuntimeVariableTableComponent, IsRunning)
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
