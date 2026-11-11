#include <thornote/platform/display_text.h>

namespace tn
{
// Other platforms hand out file names as they are stored, which in practice is already composed.
std::string composedText(std::string_view text)
{
    return std::string(text);
}
}  // namespace tn
