#pragma once

#include <initializer_list>
#include <sstream>
#include <string>
#include <ryml.hpp>
#include "Game/Scene/Components/BoundingBox.h"
#include "Game/Scene/Components/PhysicsActor.h"
#include "PhysicsLib/Actors/PhysicsActorBase.h"
#include "SceneYamlTypes.h"

namespace Game::SceneYaml {
    bool TryParsePhysicsActorTypeText(const std::string& ActorTypeText, PhysicsActorBase::PhysicsActorType& OutActorType);
    const char* ResolvePhysicsActorTypeYamlText(PhysicsActorBase::PhysicsActorType ActorType);
    PhysicsActorBase::PhysicsActorFlags FilterPhysicsActorFlags(PhysicsActorBase::PhysicsActorFlags Flags);
    bool IsObsoletePhysicsActorFlagText(const std::string& FlagText);
    bool TryParsePhysicsActorFlagText(const std::string& FlagText, PhysicsActorBase::PhysicsActorFlags& OutFlag);
    bool TryAppendPhysicsActorFlagsFromText(const std::string& FlagsText, PhysicsActorBase::PhysicsActorFlags& InOutFlags);
    bool TryReadPhysicsActorFlagsNode(c4::yml::ConstNodeRef FlagsNode, PhysicsActorBase::PhysicsActorFlags& OutFlags);
    bool TryReadPhysicsActorFlagsChild(c4::yml::ConstNodeRef TargetNode, std::initializer_list<const char*> Keys, PhysicsActorBase::PhysicsActorFlags& OutFlags);
    bool HasPhysicsActorFlag(PhysicsActorBase::PhysicsActorFlags Flags, PhysicsActorBase::PhysicsActorFlags TargetFlag);
    std::string BuildPhysicsActorFlagsYamlText(PhysicsActorBase::PhysicsActorFlags Flags);
    bool TryReadPhysicsActorSettings(c4::yml::ConstNodeRef PhysicsNode, Game::PhysicsActorSettings& OutSettings, std::string& OutErrorText);
    void AppendPhysicsActorSettings(std::ostringstream& Stream, std::size_t IndentLevel, const Game::PhysicsActorSettings& SettingsComponent, const Game::BoundingBox* BoundingBoxComponent);
}
