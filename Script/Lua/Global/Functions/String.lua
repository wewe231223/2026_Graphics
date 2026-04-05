function StringStartsWith(Value, Prefix)
    return string.sub(Value, 1, string.len(Prefix)) == Prefix
end

function StringEndsWith(Value, Suffix)
    local SuffixLength = string.len(Suffix)

    if SuffixLength == 0 then
        return true
    end

    return string.sub(Value, -SuffixLength) == Suffix
end
