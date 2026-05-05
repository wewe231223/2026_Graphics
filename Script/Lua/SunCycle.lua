local CycleAngularSpeed = 0.025
local CyclePhase = 1.05
local HorizonBlendHeight = 0.28
local VisibilityRiseHeight = 0.30
local VisibilityOffset = 0.05
local AmbientRiseHeight = 0.80
local AmbientOffset = 0.20
local DayIntensity = 1.7
local NightIntensity = 0.0
local DayAmbientIntensity = 0.45
local NightAmbientIntensity = 0.05
local DayColor = Vector3.new(1.0, 0.97, 0.92)
local SunsetColor = Vector3.new(1.0, 0.46, 0.18)
local NightColor = Vector3.new(0.12, 0.16, 0.28)
local SunAzimuthDirection = NormalizeVector3(Vector3.new(-0.66, 0.0, -0.75))
local ShadowVisibilityThreshold = 0.08
local TwoPi = math.pi * 2.0

local function WrapRadians(Value)
    local WrappedValue = Value

    while WrappedValue >= TwoPi do
        WrappedValue = WrappedValue - TwoPi
    end

    while WrappedValue < 0.0 do
        WrappedValue = WrappedValue + TwoPi
    end

    return WrappedValue
end

local function LerpVector3(StartValue, EndValue, Ratio)
    return Vector3.new(Lerp(StartValue.x, EndValue.x, Ratio), Lerp(StartValue.y, EndValue.y, Ratio), Lerp(StartValue.z, EndValue.z, Ratio))
end

local function BuildLightDirection(SunHeight, SunHorizontalDistance)
    local SunPositionDirection = NormalizeVector3(Vector3.new(SunAzimuthDirection.x * SunHorizontalDistance, SunHeight, SunAzimuthDirection.z * SunHorizontalDistance))
    return Vector3.new(-SunPositionDirection.x, -SunPositionDirection.y, -SunPositionDirection.z)
end

local function ApplySunCycle(DirectionalLightComponent)
    local SunHeight = math.sin(CyclePhase)
    local SunHorizontalDistance = math.cos(CyclePhase)
    local VisibilityRatio = Saturate((SunHeight + VisibilityOffset) / VisibilityRiseHeight)
    local AmbientRatio = Saturate((SunHeight + AmbientOffset) / AmbientRiseHeight)
    local HorizonRatio = Saturate(1.0 - (math.abs(SunHeight) / HorizonBlendHeight))
    local WarmDayColor = LerpVector3(DayColor, SunsetColor, HorizonRatio)
    local FinalColor = LerpVector3(NightColor, WarmDayColor, VisibilityRatio)

    DirectionalLightComponent.mIsActive = true
    DirectionalLightComponent.mCastsShadow = VisibilityRatio > ShadowVisibilityThreshold
    DirectionalLightComponent.mUseTransformDirection = false
    DirectionalLightComponent.mDirection = BuildLightDirection(SunHeight, SunHorizontalDistance)
    DirectionalLightComponent.mColor = FinalColor
    DirectionalLightComponent.mIntensity = Lerp(NightIntensity, DayIntensity, VisibilityRatio)
    DirectionalLightComponent.mAmbientIntensity = Lerp(NightAmbientIntensity, DayAmbientIntensity, AmbientRatio)
end

function Start(Context)
    local DirectionalLightComponent = Context:GetComponent("DirectionalLight")
    if DirectionalLightComponent == nil then
        return
    end

    ApplySunCycle(DirectionalLightComponent)
end

function Update(Context, DeltaSeconds)
    local DirectionalLightComponent = Context:GetComponent("DirectionalLight")
    if DirectionalLightComponent == nil then
        return
    end

    CyclePhase = WrapRadians(CyclePhase + (CycleAngularSpeed * DeltaSeconds))
    ApplySunCycle(DirectionalLightComponent)
end
