#pragma once

namespace Game {
    class SceneWorldSnapshot;
}

class IWidget {
public:
    virtual ~IWidget() = default;

public:
    virtual void Render(const Game::SceneWorldSnapshot* Snapshot) = 0;
};
