function Clamp(Value, MinValue, MaxValue)
    if Value < MinValue then
        return MinValue
    end

    if Value > MaxValue then
        return MaxValue
    end

    return Value
end

function Saturate(Value)
    return Clamp(Value, 0.0, 1.0)
end

function Lerp(StartValue, EndValue, Ratio)
    return StartValue + ((EndValue - StartValue) * Ratio)
end

function InverseLerp(StartValue, EndValue, Value)
    local Delta = EndValue - StartValue

    if Delta == 0.0 then
        return 0.0
    end

    return (Value - StartValue) / Delta
end

function Sign(Value)
    if Value > 0.0 then
        return 1.0
    end

    if Value < 0.0 then
        return -1.0
    end

    return 0.0
end

function Round(Value)
    return math.floor(Value + 0.5)
end

function Approximately(LeftValue, RightValue, Epsilon)
    local Tolerance = Epsilon

    if Tolerance == nil then
        Tolerance = 0.00001
    end

    return math.abs(LeftValue - RightValue) <= Tolerance
end
