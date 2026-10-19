local skynet = require "skynet"
local cluster = require "skynet.cluster"
require "skynet.manager"
local CMD = {}
local process_list = {}
local node_list = {}
local cluster_conf = { __nowaiting = true }
local self_process_id = ...
local check_interval = 60
self_process_id = tonumber(self_process_id)

local function make_cluster_conf(self, other)
    skynet.logd("self =", self, ";other =", other)
    local process_conf = process_list[other]
    local nodename = process_conf.name
    local ip = process_conf.host.localip
    skynet.logd("ip =", ip)
    if process_list[self].host.id == process_list[other].host.id then
        ip = "127.0.0.1"
    end
    local port = 0
    local servers = process_conf.servers
    for _, s in ipairs(servers) do
        if s.name == "clusterd" then
            port = s.port
            break
        end
    end
    assert(port > 0,  string.format("invaild port = %s processid = %d", tostring(port), other))
    return nodename, string.format("%s:%d", ip, port)
end

local function make_self_cluster_conf()
    local process_conf = process_list[self_process_id]
    local ip = "0.0.0.0"
    local nodename = process_conf.name
    local port = 0
    local servers = process_conf.servers
    for _, s in ipairs(servers) do
        skynet.logd("make_self_cluster_conf s =", table.tostr(s))
        if s.name == "clusterd" then
            port = s.port
            break
        end
    end
    assert(port > 0,  string.format("invaild port = %s processid = %d", tostring(port), self_process_id))
    return nodename, string.format("%s:%d", ip, port)
end

local function proxy_remote_service(nodename, S)
    local proxy
    if S.alias then
        proxy = cluster.proxy(nodename, S.alias)
        skynet.name(S.alias, proxy)
    end
    return proxy
end

local function group_intersect(gs1, gs2)
    for _, g1 in ipairs(gs1) do
        for _, g2 in ipairs(gs2) do
            if g1 == g2 then
                return true
            end
        end
    end
end

local function remake_constr(self, other)
    local nodename, constr = make_cluster_conf(self, other)
    skynet.logd("remake_constr  self =", self, "; other =", other, ";nodename =", nodename, ";constr =", constr)
    return constr
end

local function broad_event(event, to)
    if to then
        local clone_node = table.clone(event.node)
        skynet.logd("to =", table.tostr(to))
        skynet.logd("clone_node =", table.tostr(clone_node))
        clone_node.constr = remake_constr(to.processid, clone_node.processid)
        event.node = clone_node
        skynet.send(to.proxy, "lua", "event_happen", event)
        skynet.logd("broad_event to =", to.proxy, ";event =", table.tostr(event))
    else
        for _, node in pairs(node_list) do
            if group_intersect(event.node.group, node.group) then
                broad_event(event, node)
            end
        end
    end
end

local function make_node(process_id, nodename, constr, conf)
    local proxy = proxy_remote_service(nodename, {name = "cluster_agent", alias = ".cluster_agent", id = 0})
    skynet.fork(function()
        local r, err = pcall(skynet.call, proxy, "lua", "keep_linking")
        skynet.logd("r =", r, "; err =", err)
        if not r then
            skynet.logw("node =", nodename, "disconnect.")
            local node = node_list[process_id]
            node_list[process_id] = nil
            local event = {
                name = "offline",
                node = node
            }
            broad_event(event)
        end
    end)
    local node = {}
    node.processid = process_id
    node.proxy = proxy
    node.name = nodename
    node.constr = constr
    node.group = conf.group
    node.status = conf.status
    node.domain = conf.host.domain
    return node
end

local function filter_node_list(group, Nlist)
    local list = {}
    for _, N in pairs(Nlist) do
        if group_intersect(group, N.group) then
            list[N.processid] = N
        end
    end
    return list
end

function CMD.register(process_id)
    if node_list[process_id] then
        skynet.logw("node", process_id, "already registered, unregistering old node")
        local old_node = node_list[process_id]
        node_list[process_id] = nil
        broad_event({ name = "offline", node = old_node })
    end
    local process = process_list[process_id]
    if not process then
        return false, "process conf not found"
    end
    --node_list[process_id] = true
    local list = filter_node_list(process.group, node_list)
    local clone_list = table.clone(list)
    for _, l in pairs(clone_list) do
        l.constr = remake_constr(process_id, l.processid)
    end
    return process_list[process_id], clone_list
end

function CMD.online(process_id, servers)
    --[[if not node_list[process_id] then
        return false, "need register first."
    end
    if not (node_list[process_id] == true) then
        return false, "invaild state."
    end]]
    local process = process_list[process_id]
    if not process then
        return false, "process conf not found"
    end
    local nodename, constr = make_cluster_conf(self_process_id, process_id)
    cluster_conf[nodename] = constr
    cluster.reload(cluster_conf)
    local node = make_node(process_id, nodename, constr, process)
    node.servers = servers
    node_list[process_id] = node
    for _, S in pairs(node.servers) do
        proxy_remote_service(nodename, S)
    end
    local event = {
        name = "online",
        node = node
    }
    skynet.fork(function()
        -- CYS: a peer can become online while this node initializes scenes.
        -- Replay current peers now that the new node can receive events.
        for _, peer in pairs(node_list) do
            if peer.processid ~= process_id and group_intersect(peer.group, node.group) then
                broad_event({name = "online", node = peer}, node)
            end
        end
        broad_event(event)
    end)
end

local function load_cluster_conf()
    local host_conf = skynet.call(".cluster_db_mgr", "lua", "get_conf", "host_conf")
    local process_conf = skynet.call(".cluster_db_mgr", "lua", "get_conf", "process_conf")
    local server_conf = skynet.call(".cluster_db_mgr", "lua", "get_conf", "server_conf")
    for _, p in pairs(process_conf) do
        for _, h in pairs(host_conf) do
            if p.hostid == h.id then
                p.host = h
            end
        end
        p.servers = {}
        for _, s in pairs(server_conf) do
            if s.processid == p.id then
                table.insert(p.servers, s)
            end
        end
        table.sort(p.servers, function(a, b) return a.id < b.id end)
        process_list[p.id] = p
    end
end

local function start()
    local nodename, constr = make_self_cluster_conf()
    cluster_conf[nodename] = constr
    cluster.reload(cluster_conf)
    cluster.open(nodename)
end

local function find_alive_node()
    for process_id, conf in pairs(process_list) do
        if process_id ~= self_process_id then
            local r, nodename, constr = pcall(make_cluster_conf, self_process_id, process_id)
            if r and cluster_conf[nodename] == nil then
                skynet.logd("start find node =", nodename)
                local tmp_conf = {}
                tmp_conf.__nowaiting = true
                tmp_conf[nodename] = constr
                cluster.reload(tmp_conf)
                local ret,sender = pcall(cluster.sender,nodename)
                if ret and sender then
                    cluster_conf[nodename] = constr
                    skynet.logi("find alive process =", process_id)
                    local node = make_node(process_id, nodename, constr, conf)
                    node.servers = skynet.call(node.proxy, "lua", "get_public_servers")
                    node_list[process_id] = node
                    for _, S in pairs(node.servers) do
                        proxy_remote_service(nodename, S)
                    end
                    skynet.logd("node =", table.tostr(node))
                else
                    skynet.logd("find_alive_node process =", process_id, "not alive")
                end
                skynet.logd("end find node =", nodename)
            end
        end
    end
end

CMD.check_process_status = function ()
    load_cluster_conf()
    local process_conf = skynet.call(".cluster_db_mgr", "lua", "get_conf", "process_conf")
    for _, conf in pairs(process_conf) do
        local processid = conf.id
        local node = node_list[processid]
        if node and node.status ~= conf.status then
            node.status = conf.status
            local event = {
                name = "status_change",
                node = node
            }
            broad_event(event)
        end
    end
    skynet.timeout(check_interval * 100, CMD.check_process_status)
end

local function init()
    load_cluster_conf()
    start()
    find_alive_node()
    skynet.timeout(check_interval * 100, CMD.check_process_status)
end

skynet.start(function()
    skynet.dispatch("lua", function(_, _, command, ...)
        local f = assert(CMD[command], command)
        skynet.ret(skynet.pack(f(...)))
    end)
    init()
end)
