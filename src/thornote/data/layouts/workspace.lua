-- This file describes the UI. Documents and commands remain owned by the app.
-- Edit widths, visibility, order, or add ui.button/ui.label elements, then save.
---@type ThorNote.UI.LayoutModule
local definition = {
  schema = 1,
  build = function(state)
    return ui.column {
      id = "workspace",
      children = {
        -- Buttons and menus are shared through titlebar.lua.
        ui.view {id = "titlebar", source = "titlebar", height = 40},
        ui.row {
          id = "body",
          children = {
            ui.view {
              id = "collections", source = "collections",
              width = ui.preferred(state.sidebarWidth, 150, 260),
              visible = state.collections, resizable = true,
            },
            ui.view {
              id = "documents", source = "documents",
              width = ui.preferred(state.documentWidth, 180, 340),
              visible = state.navigation, resizable = true,
            },
            ui.column {
              id = "main",
              children = {
                ui.view { id = "tabs", source = "tabs", height = 34, visible = state.tabs },
                ui.view { id = "note-heading", source = "note-heading", height = 38, visible = state.notes },
                ui.view { id = "content", source = "content" },
              },
            },

          },
        },
        ui.view { id = "status", source = "status", height = 22 },
      },
    }
  end,
}

return definition
