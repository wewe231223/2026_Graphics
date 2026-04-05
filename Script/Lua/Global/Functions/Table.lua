function TableContains(SourceTable, Value)
    for _, Element in ipairs(SourceTable) do
        if Element == Value then
            return true
        end
    end

    return false
end

function TableCount(SourceTable)
    local Count = 0

    for _, _ in pairs(SourceTable) do
        Count = Count + 1
    end

    return Count
end
