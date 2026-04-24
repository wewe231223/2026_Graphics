local MoveSpeed = 4.0
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

local function ApplyForwardMovement(TransformComponent, MoveInput, DeltaSeconds)
    local InputMagnitude = math.sqrt((MoveInput.x * MoveInput.x) + (MoveInput.y * MoveInput.y))
    if InputMagnitude <= 0.0 then
        return 0.0
    end

    local MoveDistance = MoveSpeed * InputMagnitude * DeltaSeconds
    local CurrentForwardDirection = TransformComponent:GetForwardDirection()
    local FlatForwardDirection = NormalizeVector3(Vector3.new(CurrentForwardDirection.x, 0.0, CurrentForwardDirection.z))
    if IsZeroVector3(FlatForwardDirection) then
        return 0.0
    end

    local Translation = ScaleVector3(FlatForwardDirection, MoveDistance)
    TransformComponent:Translate(Vector3.new(Translation.x, 0.0, Translation.z))

    return MoveSpeed * InputMagnitude
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
    local MoveInput = BuildMoveInput()
    local FinalTargetDirection = BuildFinalTargetDirection(MoveInput)
    local RemainingDirectionDeltaRadians = ApplySmoothedYawRotation(TransformComponent, FinalTargetDirection, DeltaSeconds)
    local CurrentSpeed = ApplyForwardMovement(TransformComponent, MoveInput, DeltaSeconds)
    local IsMoving = CurrentSpeed > 0.0

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
