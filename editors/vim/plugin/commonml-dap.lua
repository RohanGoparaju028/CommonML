-- Optional Neovim nvim-dap configuration for CommonML (`cml --dap`)
-- Drop into ~/.config/nvim/lua/ or require after installing nvim-dap.
local ok, dap = pcall(require, "dap")
if ok then
  dap.adapters.commonml = {
    type = "executable",
    command = "cml",
    args = { "--dap" },
  }
  dap.configurations.commonml = {
    {
      type = "commonml",
      request = "launch",
      name = "Debug CommonML file",
      program = "${file}",
    },
  }
end
