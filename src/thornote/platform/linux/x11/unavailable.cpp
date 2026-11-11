#include <thornote/platform/linux/title_bar.h>

namespace tn
{
std::unique_ptr<LinuxTitleBarInput> createX11TitleBarInput(SDL_Window*, std::function<bool(float, float)>, std::function<void()>)
{
    return {};
}
}  // namespace tn
