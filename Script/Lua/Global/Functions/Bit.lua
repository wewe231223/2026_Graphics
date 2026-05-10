function IsFlagEnabled(Flags, Flag)
    return (Flags & Flag) ~= 0
end

function SetFlagEnabled(Flags, Flag, IsEnabled)
    if IsEnabled then
        return Flags | Flag
    end

    return Flags & (~Flag)
end

function ToggleFlag(Flags, Flag)
    return Flags ~ Flag
end
