local skynet = require "skynet"

local M = {}

function M.check_or_abort()
    skynet.error("[INFO] License check bypassed for local test environment")
end

return M
