#pragma once
#include <thornote/preferences/layout.h>
#include <string>
#include <vector>

namespace tn
{
struct RunOptions
{
    bool fresh = false;
    bool settings = false, layoutSpecified = false;
    Layout layout = Layout::Memo;
    std::string snapshot, layoutDirectory, settingsDirectory;
    std::vector<std::string> paths;
};
int run(const RunOptions&);
}  // namespace tn
