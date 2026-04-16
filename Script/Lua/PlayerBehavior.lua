local MoveSpeed = 10.0
local YawLookSensitivity = 0.0026
local IsMovingParameterIndex = 0

local function BuildMoveDirection()
    local MoveDirection = Vector3.new()
    MoveDirection.x = GetAxisFromKeys(A, D)
    MoveDirection.z = GetAxisFromKeys(S, W)
    return NormalizeVector3(MoveDirection)
end

local function ApplyYawRotation(TransformComponent)
    local YawDelta = GetInputMouseDeltaX() * YawLookSensitivity
    TransformComponent:RotateRadians(0.0, YawDelta, 0.0)
end

local function ApplyMovement(TransformComponent, MoveDirection, DeltaSeconds)
    if IsZeroVector3(MoveDirection) then
        return false
    end

    local MoveDistance = MoveSpeed * DeltaSeconds
    local WorldMoveDirection = TransformComponent:TransformDirectionToWorld(MoveDirection)
    local Translation = Vector3.new(WorldMoveDirection.x * MoveDistance, 0.0, WorldMoveDirection.z * MoveDistance)
    TransformComponent:Translate(Translation)

    return true
end

local function SetMovingState(RuntimeVariableTableComponent, IsMoving)
    if RuntimeVariableTableComponent == nil then
        return
    end

    RuntimeVariableTableComponent.BoolValues:Set(IsMovingParameterIndex, IsMoving)
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

    ApplyYawRotation(TransformComponent)

    local RuntimeVariableTableComponent = Context:GetComponent("RuntimeVariableTable")
    local MoveDirection = BuildMoveDirection()
    local IsMoving = ApplyMovement(TransformComponent, MoveDirection, DeltaSeconds)

    SetMovingState(RuntimeVariableTableComponent, IsMoving)
end

function FixedUpdate(Context, FixedDeltaSeconds, FixedTick)
end

function LateUpdate(Context, DeltaSeconds)
end

function OnDisable(Context)
end

function OnDestroy(Context)
end
