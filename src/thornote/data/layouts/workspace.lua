-- This file describes the UI. Documents and commands remain owned by the app.
-- Edit widths, visibility, order, or add ui.button/ui.label elements, then save.
---@type ThorNote.UI.LayoutModule
local definition = {
  schema = 1,
  build = function(state)
    local sidebarTitle = state.mode == "note" and state.navigation
    local categoryMinimum = state.customTitleBar and not state.windowControlsLeading and 200 or 150
    local categories = {
      ui.view {id = "collection-list", source = "collections"},
    }
    local documents = {
      ui.view {id = "document-list", source = "documents"},
    }
    if sidebarTitle then
      local titlebar = ui.view {id = "sidebar-titlebar", source = "titlebar", height = 40}
      table.insert(state.collections and categories or documents, 1, titlebar)
    end
    local content = {
      ui.view {id = "tabs", source = "tabs", height = 34, visible = state.tabs},
      ui.view {id = "note-heading", source = "note-heading", height = 40, visible = state.notes and not state.settings},
      ui.view {id = "content", source = "content"},
    }
    if state.mode == "note" then
      table.insert(content, ui.view {id = "status", source = "status", height = 22})
    end
    local children = {
      ui.row {
        id = "body",
        children = {
          ui.column {
            id = "collections", width = ui.preferred(state.sidebarWidth, categoryMinimum, 260),
            visible = state.collections, resizable = true, children = categories,
          },
          ui.column {
            id = "documents", width = ui.preferred(state.documentWidth, 180, 340),
            visible = state.navigation, resizable = true, children = documents,
          },
          ui.column {
            id = "main",
            children = content,
          },
        },
      },
    }
    if state.mode ~= "note" then
      table.insert(children, ui.view {id = "status", source = "status", height = 22})
    end
    if not sidebarTitle then
      table.insert(children, 1, ui.view {id = "titlebar", source = "titlebar", height = 40})
    end
    return ui.column {id = "workspace", children = children}
  end,
}

return definition
