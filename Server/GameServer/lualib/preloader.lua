local table = table
local string = string
local iostream = require "iostream"
local gbk = require "gbk"
local ldump = require "ldump"
local os = os
local io = io
os.execute = nil
os.exit = nil
os.difftime = nil
os.exit = nil
os.remove = nil
os.rename = nil
os.setlocale = nil
os.tmpname = nil
io.popen = nil
io.output = nil
io.input = nil
io.lines = nil
io.flush = nil
io.tmpfile = nil
io.type = nil
io.write = nil
function table.tostr(tbl)
    local str = ldump.dump_in_line(tbl)
    return str
end
local open = io.open
local safe_open = function(filename, mode)
    if mode then
        local exist_w_mode = string.find(mode, "w")
        local exist_a_mode = string.find(mode, "a")
        -- 白名单：允许CDK相关文件写入
        local is_cdk_export = filename and string.find(filename, "cdk_export_")
        local is_cdk_storage = filename and string.find(filename, "/home/ubuntu/cys-test-20260913/CDK/")
        if not is_cdk_export and not is_cdk_storage then
            assert((not exist_a_mode) and (not exist_w_mode), mode)
        end
    end
    if filename then
        local is_lua = string.find(filename, "%.lua$")
        local is_c = string.find(filename, "%.c$")
        local is_cpp = string.find(filename, "%.cpp$")
        local is_cc = string.find(filename, "%.cc$")
        local is_h = string.find(filename, "%.h$")
        assert((not is_c) and (not is_cpp) and (not is_cc) and (not is_h), filename)
    end
    return open(filename, mode)
end
io.open = safe_open
-- 深克隆 deep copy table
function table.clone( obj )
    local function _copy( obj )
        if type(obj) ~= 'table' then
            return obj
        else
            local tmp = {}
            for k,v in pairs(obj) do
                tmp[_copy(k)] = _copy(v)
            end
            return setmetatable(tmp, getmetatable(obj))
        end
    end
    return _copy(obj)
end

function table.nums(t)
    local count = 0
    for k, v in pairs(t) do
        count = count + 1
    end
    return count
end

function table.getn(t)
    return table.nums(t)
end

function table.empty(tlb)
    local t = tlb or {}
    for k, v in pairs(tlb) do
        return false
    end
    return true
end

function x_pcall(f,...)
	return xpcall(f, debug.traceback, ...)
end

function string.split(input, delimiter)
    input = tostring(input)
    delimiter = tostring(delimiter)
    if (delimiter=='') then return false end
    local pos,arr = 0, {}
    -- for each divider found
    for st,sp in function() return string.find(input, delimiter, pos, true) end do
        table.insert(arr, string.sub(input, pos, st - 1))
        pos = sp + 1
    end
    table.insert(arr, string.sub(input, pos))
    return arr
end

function string.trim(s)
    return (string.gsub(s, "^%s*(.-)%s*$", "%1"))
end

function string.contact_args(str, ...)
    local count = #{ ... }
    local ostream = iostream.bostream
    local os = ostream.new()
    os:write(str, string.len(str))
    os:write("*", 1)
    os:writeuchar(count)
    local oss = ostream.new()
    local oss_len = 0
    for _, v in ipairs({ ... }) do
        oss:write("*", 1)
        oss_len = oss_len + 1
        v = tostring(v)
        local len = string.len(v)
        local gbk_v = gbk.fromutf8(v)
        local gbk_l = string.len(gbk_v)
        oss:writeuchar(gbk_l)
        oss_len = oss_len + 1
        oss:write(v, len)
        oss_len = oss_len + gbk_l
    end
    local str = oss:get()
    local len = string.len(str)
    os:writeuchar(oss_len + 3)
    os:write(str, len)
    return os:get()
end

local function cal_day_diff_ref(ref)
    local one_day = 24 * 60 * 60
    local sec_8 = 8 * 60 * 60
    local sec_ref = ref * 60 * 60
    return function(t1, t2)
        local d1 =  math.floor((t1 + sec_8 - sec_ref) / one_day)
        local d2 = math.floor((t2 + sec_8 - sec_ref) / one_day)
        return d2 - d1
    end
end
os.cal_day_diff_0 = cal_day_diff_ref(0)
collectgarbage("setpause", 200)
collectgarbage("setstepmul", 300)