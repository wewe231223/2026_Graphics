function Vector3LengthSquared(Value)
    return (Value.x * Value.x) + (Value.y * Value.y) + (Value.z * Value.z)
end

function Vector3Length(Value)
    return math.sqrt(Vector3LengthSquared(Value))
end

function IsZeroVector3(Value)
    return Value.x == 0.0 and Value.y == 0.0 and Value.z == 0.0
end

function NormalizeVector3(Value)
    local Length = Vector3Length(Value)

    if Length <= 0.0 then
        return Vector3.new(0.0, 0.0, 0.0)
    end

    return Vector3.new(Value.x / Length, Value.y / Length, Value.z / Length)
end

function AddVector3(LeftValue, RightValue)
    return Vector3.new(LeftValue.x + RightValue.x, LeftValue.y + RightValue.y, LeftValue.z + RightValue.z)
end

function SubtractVector3(LeftValue, RightValue)
    return Vector3.new(LeftValue.x - RightValue.x, LeftValue.y - RightValue.y, LeftValue.z - RightValue.z)
end

function ScaleVector3(Value, Scalar)
    return Vector3.new(Value.x * Scalar, Value.y * Scalar, Value.z * Scalar)
end

function DotVector3(LeftValue, RightValue)
    return (LeftValue.x * RightValue.x) + (LeftValue.y * RightValue.y) + (LeftValue.z * RightValue.z)
end

function CrossVector3(LeftValue, RightValue)
    local ResultX = (LeftValue.y * RightValue.z) - (LeftValue.z * RightValue.y)
    local ResultY = (LeftValue.z * RightValue.x) - (LeftValue.x * RightValue.z)
    local ResultZ = (LeftValue.x * RightValue.y) - (LeftValue.y * RightValue.x)
    return Vector3.new(ResultX, ResultY, ResultZ)
end
