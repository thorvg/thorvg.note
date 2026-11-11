#pragma once

namespace tn::test
{
class Engine
{
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
};
}  // namespace tn::test
