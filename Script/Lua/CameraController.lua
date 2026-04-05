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
    if IsInputKeyDown(LeftShift) then
        MoveSpeedScale = FreeLookBoostScale
    end

    local MoveDirection = Vector3.new()
    MoveDirection.x = GetAxisFromKeys(A, D)
    MoveDirection.y = GetAxisFromKeys(E, Q)
    MoveDirection.z = GetAxisFromKeys(S, W)

    local NormalizedMoveDirection = NormalizeVector3(MoveDirection)

    if IsZeroVector3(NormalizedMoveDirection) == false then
        local WorldMoveDirection = TransformComponent:TransformDirectionToWorld(NormalizedMoveDirection)
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
    local IsThirdPersonMode = IsFlagEnabled(CameraComponent.cameraFlags, CameraFlagThirdPerson)
    local IsFreeLookMode = IsFlagEnabled(CameraComponent.cameraFlags, CameraFlagFreeLook)
    local IsCinematicMode = IsFlagEnabled(CameraComponent.cameraFlags, CameraFlagCinematic)

    if IsThirdPersonMode then
        ProcessThirdPersonMode(CameraComponent, MouseWheelDelta)
        return
    end

    if IsCinematicMode and IsFreeLookMode == false then
        return
    end

    ProcessFreeLookMode(TransformComponent, CameraComponent, DeltaSeconds, MouseWheelDelta)
end
