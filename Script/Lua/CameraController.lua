local KeyW = 87
local KeyA = 65
local KeyS = 83
local KeyD = 68
local KeyQ = 81
local KeyE = 69

local CameraFlagThirdPerson = 8

local LookSensitivity = 0.0026
local FreeLookMoveSpeed = 6.0
local ThirdPersonZoomDistancePerTick = 0.75
local MinPitchRadians = -1.55334306
local MaxPitchRadians = 1.55334306
local MinThirdPersonPitchRadians = -1.2
local MaxThirdPersonPitchRadians = 1.2
local MinFovDegrees = 20.0
local MaxFovDegrees = 120.0
local ZoomSpeedDegreesPerTick = 2.0

local function Clamp(Value, MinValue, MaxValue)
    if Value < MinValue then
        return MinValue
    end

    if Value > MaxValue then
        return MaxValue
    end

    return Value
end

local function BuildMoveDirection()
    local Direction = Vector3.new()

    if IsInputKeyDown(KeyD) then
        Direction.x = Direction.x + 1.0
    end

    if IsInputKeyDown(KeyA) then
        Direction.x = Direction.x - 1.0
    end

    if IsInputKeyDown(KeyE) then
        Direction.y = Direction.y + 1.0
    end

    if IsInputKeyDown(KeyQ) then
        Direction.y = Direction.y - 1.0
    end

    if IsInputKeyDown(KeyW) then
        Direction.z = Direction.z + 1.0
    end

    if IsInputKeyDown(KeyS) then
        Direction.z = Direction.z - 1.0
    end

    return Direction
end

function Update(Context, DeltaSeconds)
    local TransformComponent = Context:GetComponent("Transform")
    local CameraComponent = Context:GetComponent("Camera")

    if TransformComponent == nil or CameraComponent == nil then
        return
    end

    local MouseDeltaX = GetInputMouseDeltaX() * LookSensitivity
    local MouseDeltaY = GetInputMouseDeltaY() * LookSensitivity
    local MouseWheelDelta = GetInputMouseWheelDelta() / 120.0

    local IsThirdPersonMode = (CameraComponent.cameraFlags & CameraFlagThirdPerson) ~= 0
    if IsThirdPersonMode then
        CameraComponent.thirdPersonOrbitYaw = CameraComponent.thirdPersonOrbitYaw + MouseDeltaX
        CameraComponent.thirdPersonOrbitPitch = Clamp(CameraComponent.thirdPersonOrbitPitch - MouseDeltaY, MinThirdPersonPitchRadians, MaxThirdPersonPitchRadians)

        local ZoomDeltaDistance = MouseWheelDelta * ThirdPersonZoomDistancePerTick * CameraComponent.thirdPersonZoomSpeed
        CameraComponent.thirdPersonDistance = Clamp(CameraComponent.thirdPersonDistance - ZoomDeltaDistance, CameraComponent.thirdPersonMinDistance, CameraComponent.thirdPersonMaxDistance)
        return
    end

    TransformComponent:RotateRadians(-MouseDeltaY, MouseDeltaX, 0.0)
    TransformComponent:ClampPitchRadians(MinPitchRadians, MaxPitchRadians)

    local MoveDirection = BuildMoveDirection()
    local MoveDirectionLength = math.sqrt((MoveDirection.x * MoveDirection.x) + (MoveDirection.y * MoveDirection.y) + (MoveDirection.z * MoveDirection.z))

    if MoveDirectionLength > 0.0 then
        MoveDirection.x = MoveDirection.x / MoveDirectionLength
        MoveDirection.y = MoveDirection.y / MoveDirectionLength
        MoveDirection.z = MoveDirection.z / MoveDirectionLength

        local WorldMoveDirection = TransformComponent:TransformDirectionToWorld(MoveDirection)
        local MoveAmount = FreeLookMoveSpeed * DeltaSeconds
        TransformComponent:Translate(WorldMoveDirection.x * MoveAmount, WorldMoveDirection.y * MoveAmount, WorldMoveDirection.z * MoveAmount)
    end

    CameraComponent.fov = Clamp(CameraComponent.fov - (MouseWheelDelta * ZoomSpeedDegreesPerTick), MinFovDegrees, MaxFovDegrees)
end
