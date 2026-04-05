local KeyW = 87
local KeyA = 65
local KeyS = 83
local KeyD = 68
local KeyQ = 81
local KeyE = 69
local KeyLeftShift = 160

local CameraFlagFreeLook = 2
local CameraFlagCinematic = 4
local CameraFlagThirdPerson = 8

local FreeLookLookSensitivity = 0.0026
local FreeLookMoveSpeed = 6.0
local FreeLookBoostScale = 2.0
local ThirdPersonLookSensitivity = 0.0026
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

local function ProcessThirdPersonMode(CameraComponent, MouseWheelDelta)
    local MouseDeltaX = GetInputMouseDeltaX() * ThirdPersonLookSensitivity
    local MouseDeltaY = GetInputMouseDeltaY() * ThirdPersonLookSensitivity

    CameraComponent.thirdPersonOrbitYaw = CameraComponent.thirdPersonOrbitYaw + MouseDeltaX
    CameraComponent.thirdPersonOrbitPitch = Clamp(CameraComponent.thirdPersonOrbitPitch - MouseDeltaY, MinThirdPersonPitchRadians, MaxThirdPersonPitchRadians)

    local ZoomDeltaDistance = MouseWheelDelta * ThirdPersonZoomDistancePerTick * CameraComponent.thirdPersonZoomSpeed
    CameraComponent.thirdPersonDistance = Clamp(CameraComponent.thirdPersonDistance - ZoomDeltaDistance, CameraComponent.thirdPersonMinDistance, CameraComponent.thirdPersonMaxDistance)
end

local function ProcessFreeLookMode(TransformComponent, CameraComponent, DeltaSeconds, MouseWheelDelta)
    local IsPickingInteraction = IsInputMouseLeftButtonDown()
    if IsPickingInteraction == false then
        local LookDeltaX = GetInputMouseDeltaX() * FreeLookLookSensitivity
        local LookDeltaY = GetInputMouseDeltaY() * FreeLookLookSensitivity

        TransformComponent:RotateRadians(-LookDeltaY, LookDeltaX, 0.0)
        TransformComponent:ClampPitchRadians(MinPitchRadians, MaxPitchRadians)
    end

    local MoveSpeedScale = 1.0
    if IsInputKeyDown(KeyLeftShift) then
        MoveSpeedScale = FreeLookBoostScale
    end

    local MoveDirection = Vector3.new()

    if IsInputKeyDown(KeyD) then
        MoveDirection.x = MoveDirection.x - 1.0
    end

    if IsInputKeyDown(KeyA) then
        MoveDirection.x = MoveDirection.x + 1.0
    end

    if IsInputKeyDown(KeyE) then
        MoveDirection.y = MoveDirection.y + 1.0
    end

    if IsInputKeyDown(KeyQ) then
        MoveDirection.y = MoveDirection.y - 1.0
    end

    if IsInputKeyDown(KeyW) then
        MoveDirection.z = MoveDirection.z - 1.0
    end

    if IsInputKeyDown(KeyS) then
        MoveDirection.z = MoveDirection.z + 1.0
    end

    local MoveDirectionLength = math.sqrt((MoveDirection.x * MoveDirection.x) + (MoveDirection.y * MoveDirection.y) + (MoveDirection.z * MoveDirection.z))

    if MoveDirectionLength > 0.0 then
        MoveDirection.x = MoveDirection.x / MoveDirectionLength
        MoveDirection.y = MoveDirection.y / MoveDirectionLength
        MoveDirection.z = MoveDirection.z / MoveDirectionLength

        local WorldMoveDirection = TransformComponent:TransformDirectionToWorld(MoveDirection)
        local MoveAmount = FreeLookMoveSpeed * DeltaSeconds * MoveSpeedScale
        local Translation = Vector3.new(WorldMoveDirection.x * MoveAmount, WorldMoveDirection.y * MoveAmount, WorldMoveDirection.z * MoveAmount)
        TransformComponent:Translate(Translation)
    end

    CameraComponent.fov = Clamp(CameraComponent.fov - (MouseWheelDelta * ZoomSpeedDegreesPerTick), MinFovDegrees, MaxFovDegrees)
end

function Update(Context, DeltaSeconds)
    local TransformComponent = Context:GetComponent("Transform")
    local CameraComponent = Context:GetComponent("Camera")

    if TransformComponent == nil or CameraComponent == nil then
        return
    end

    local MouseWheelDelta = GetInputMouseWheelDelta() / 120.0
    local IsThirdPersonMode = (CameraComponent.cameraFlags & CameraFlagThirdPerson) ~= 0
    local IsFreeLookMode = (CameraComponent.cameraFlags & CameraFlagFreeLook) ~= 0
    local IsCinematicMode = (CameraComponent.cameraFlags & CameraFlagCinematic) ~= 0

    if IsThirdPersonMode then
        ProcessThirdPersonMode(CameraComponent, MouseWheelDelta)
        return
    end

    if IsCinematicMode and IsFreeLookMode == false then
        return
    end

    ProcessFreeLookMode(TransformComponent, CameraComponent, DeltaSeconds, MouseWheelDelta)
end
