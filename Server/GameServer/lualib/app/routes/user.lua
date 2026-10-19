return function(router)

local skynet = require "skynet"
local utils = require "app.utils"
local ipairs = ipairs

local users = {
    admin = {
        username = "admin",
        password = "admin",
        roles = {"admin"},
        introduction = "I am a super administrator",
        avatar = "https://wpimg.wallstcn.com/f778738c-e4f8-4870-b634-56703b4acafe.gif",
        name = "Super Admin",
    },
    editor = {
        username = "editor",
        password = "editor",
        roles = {"editor"},
        introduction = "I am an editor",
        avatar = "https://wpimg.wallstcn.com/f778738c-e4f8-4870-b634-56703b4acafe.gif",
        name = "Normal Editor",
    },
}

router:post("/info", function(c)
    local username = c.token.get('username')
    skynet.logi("username =", username)
    local info = users[username]
    skynet.logi("info =", info)
    c:send_json({
        code = "OK",
        msg = "登录成功",
        data = info,
    })
end)

router:post("/login", function(c)
    local username = c.req.body.username
    local password = c.req.body.password
    print("login username =", username, ", password =", password)
    local info = users[username]
    if info and info.password == password then
        local accesstoken = utils.gen_accesstoken(username)
        if c.token and c.token.set then
            c.token.set(accesstoken, username)
        end
        c:send_json({
            code = "OK",
            msg = "登录成功",
            data = {
                token = accesstoken,
            }
        })
        return
    end

    c:send_json({
        code = "LOGIN_FAILED",
        msg = "Wrong username or password! Please check.",
    })
    return
end)

router:post("/logout", function(c)
    c.token.destroy()
    c:send_json({
        code = "OK",
        msg = "登出成功",
    })
end)

end

