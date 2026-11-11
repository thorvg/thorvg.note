#include "engine.h"
#include <thorvg.h>
#include <stdexcept>

namespace tn::test
{
Engine::Engine()
{
    if (tvg::Initializer::init(0) != tvg::Result::Success) throw std::runtime_error("cannot initialize ThorVG for tests");
}
Engine::~Engine()
{
    tvg::Initializer::term();
}
}  // namespace tn::test
