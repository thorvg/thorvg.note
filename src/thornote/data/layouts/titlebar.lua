-- Shared by every layout and the welcome screen. All sizes are logical pixels.
---@type ThorNote.UI.LayoutModule
local definition = {
  schema = 1,
  build = function(state)
    local memo = state.mode == "memo"
    local left = {}
    if memo then
      left = {
        ui.button {id = "commands", icon = ui.icons.menu, tooltip = "All Commands", command = "ui.commands.open",
          width = 30, height = 30, style = "quiet", enabled = not state.dialog},
      }
    else
      local menu = ui.menu {
        id = "main-menu", icon = ui.icons.menu, tooltip = "Main Menu",
        style = "quiet", width = 30, height = 30, active = state.menuRoot == "main-menu",
        items = {
          {text = "Save", command = "file.save", icon = ui.icons.save},
          {text = "Save As…", command = "file.saveAs"},
          {text = "Find…", command = "edit.find", icon = ui.icons.search},
          {text = "Replace…", command = "edit.replace"},
          {separator = true},
          {id = "layout", text = "Layout", items = {
            {text = "Memo", command = "layout.memo", checked = state.mode == "memo"},
            {text = "Note", command = "layout.note", checked = state.mode == "note"},
            {separator = true},
            {text = "Edit Current Layout Script…", command = "layout.script.edit"},
            {text = "Edit Title Bar Script…", command = "layout.titlebar.edit"},
            {text = "Reload Layout Script", command = "layout.script.reload"},
          }},
          {id = "view", text = "View", items = {
            {text = "Command List", command = "view.commands.toggle", checked = state.commands},
          }},
          {id = "appearance", text = "Appearance", items = {
            {text = "Light", command = "theme.light", checked = not state.dark},
            {text = "Dark", command = "theme.dark", checked = state.dark},
            {separator = true},
            {text = "Edit Current Theme…", command = "theme.edit"},
            {text = "Reload Theme", command = "theme.reload"},
            {separator = true},
            {id = "tabs", text = "Tab Style", items = {
              {text = "Connected", command = "tabs.connected", checked = state.tabStyle == 1},
              {text = "Segmented", command = "tabs.segmented", checked = state.tabStyle == 0},
              {text = "Underline", command = "tabs.underline", checked = state.tabStyle == 2},
            }},
          }},
          {separator = true},
          {text = "All Commands…", command = "ui.commands.open", icon = ui.icons.search},
          {text = "Settings…", command = "settings.open", icon = ui.icons.settings},
          {text = "Close Tab", command = "file.close"},
        },
      }
      left = {
        menu,
        ui.button {id = "open", icon = ui.icons.folder, tooltip = "Open file", command = "file.open",
          width = 30, height = 30, style = "quiet", enabled = not state.dialog},
        ui.button {id = "new", icon = ui.icons.new, tooltip = "New document", command = "file.new",
          width = 30, height = 30, style = "quiet", enabled = not state.dialog},
      }
      menu.enabled = not state.dialog
    end
    local right = {}
    if state.customTitleBar and not state.headerOnly then
      local function windowButton(action)
        return ui.view {id = "window-" .. action, source = "window-" .. action,
          width = state.windowControlsLeading and 22 or 46, height = 40}
      end
      local controls = ui.row {
        id = "window-controls", width = state.windowControlsWidth, height = 40, overflow = "clip",
        children = state.windowControlsLeading and {
          ui.view {id = "window-leading", source = "window-drag", width = 8},
          windowButton("close"), windowButton("minimize"), windowButton("zoom"),
          ui.view {id = "window-gap", source = "window-drag", width = 4},
        } or {
          windowButton("minimize"), windowButton("zoom"), windowButton("close"),
        },
      }
      if state.windowControlsLeading then
        table.insert(left, 1, controls)
      else
        if not memo then
          table.insert(left, 1, ui.view {id = "window-icon", source = "window-icon", width = 36, height = 40})
        end
        table.insert(right, controls)
      end
    end
    if not state.customTitleBar or state.headerOnly then
      table.insert(left, 1, ui.view {id = "menu-gap", source = "window-drag", width = 4, height = 40})
    end
    local function group(id, children)
      local width = 0
      for _, child in ipairs(children) do width = width + child.width end
      return ui.row {id = id, width = width + math.max(0, #children - 1) * 8,
        gap = 8, align = "center", overflow = "clip", visible = #children > 0, children = children}
    end
    return ui.row {
      id = "titlebar", gap = 8, align = "center", overflow = "clip",
      children = {
        group("left", left),
        ui.view {id = "caption", source = memo and "window-tabs" or "window-title"},
        group("right", right),
        ui.menu {id = "folder-menu", visible = false, compact = true, items = {
          {text = "Remove", command = "folder.remove"},
        }},
      },
    }
  end,
}

return definition
