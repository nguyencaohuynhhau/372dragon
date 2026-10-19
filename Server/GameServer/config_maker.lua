local env = arg[1]
local script_root = arg[2]
local processid = arg[3]
local svrtype = arg[4]
local scene_config = arg[5]
local config_path = arg[6]

local thread = "64"
local loglevel = "2"

local config = string.format([[
    env = "%s"
    script_root = "%s"
    config_path = "%s"
    include "config.path"
    thread = %s
    harbor = 0
    start = "main" -- main script
    bootstrap = "snlua bootstrap"	-- The service for bootstrap
    enablessl = true
    enablecipher = true
    loglevel = %s
    process_id = %d
    scene_config_env = "%s"
    sensitive_words_path = "./sensitive_words.txt"
    daemon = "./skynet_%d.pid"
    logger = "%s%d"
    logpath = "../log"
    svrtype = "%s"
    __nowaiting = true

]], env, script_root, config_path, thread, loglevel,
   processid, scene_config, processid, svrtype, processid, svrtype)
print(config)
