function IsAnyKeyDown(Keys)
    local KeyCount = #Keys
    local Index = 1

    while Index <= KeyCount do
        if IsInputKeyDown(Keys[Index]) then
            return true
        end

        Index = Index + 1
    end

    return false
end

function GetAxisFromKeys(PositiveKey, NegativeKey)
    local Axis = 0.0

    if IsInputKeyDown(PositiveKey) then
        Axis = Axis + 1.0
    end

    if IsInputKeyDown(NegativeKey) then
        Axis = Axis - 1.0
    end

    return Axis
end
