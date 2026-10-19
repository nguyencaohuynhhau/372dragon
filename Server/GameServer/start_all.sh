#!/bin/bash
set -e

# ============================================================
# 游戏服务器统一管理脚本
# 用法: ./start_all.sh [start|stop|start-web|start-manager|start-game|status|log]
# ============================================================

# ---------- 颜色定义 ----------
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # 无颜色

info()    { echo -e "${YELLOW}[INFO]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC}   $*"; }
error()   { echo -e "${RED}[ERR]${NC}  $*"; }

# ---------- 路径与环境配置 ----------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GAME_DIR="$SCRIPT_DIR"
script_root="$SERVER_DIR/Game_2/Script"
scene_config="$SERVER_DIR/Game_2/Scene"
config_path="$SERVER_DIR/configs/%s"
env=publish_yu

# ---------- 进入工作目录 ----------
cd "$GAME_DIR"

# ============================================================
# 内部函数：编译框架与主体
# ============================================================
do_build() {
    info "开始编译 framework ..."
    cd framework
    if [ -f "3rd/jemalloc/lib/libjemalloc_pic.a" ]; then
        make linux
    else
        make linux_nojemalloc
    fi
    cd ..
    info "开始编译主体 ..."
    make
    success "编译完成"
}

# ============================================================
# 内部函数：启动单个服务
# 参数: $1=svrtype  $2=processid  $3=config_file_name
# 示例: start_service Manager 8 config
# ============================================================
start_service() {
    local svrtype=$1
    local processid=$2
    local config_file=$3
    local exec_target="${svrtype}_tlbb_${processid}"
    local pidfile="${GAME_DIR}/skynet_${processid}.pid"

    info "准备启动 ${svrtype}(processid=${processid}) ..."

    # 检查是否已有同进程在运行，如果有则先停掉
    if [ -f "$pidfile" ]; then
        local old_pid
        old_pid=$(cat "$pidfile")
        if kill -0 "$old_pid" 2>/dev/null; then
            info "检测到 ${svrtype}(pid=${old_pid}) 正在运行，先停止旧进程 ..."
            kill "$old_pid"
            # 等待进程完全退出
            local wait_count=0
            while kill -0 "$old_pid" 2>/dev/null && [ $wait_count -lt 15 ]; do
                sleep 1
                wait_count=$((wait_count + 1))
            done
            if kill -0 "$old_pid" 2>/dev/null; then
                info "进程未响应，强制终止 ..."
                kill -9 "$old_pid" 2>/dev/null || true
                sleep 1
            fi
            success "旧 ${svrtype} 进程已停止"
        fi
        rm -f "$pidfile"
    fi

    # 拷贝可执行文件
    cp ./framework/skynet "./framework/${exec_target}"

    # 生成配置文件
    info "生成配置文件 -> ${config_file}"
    lua config_maker.lua "$env" "$script_root" "$processid" "$svrtype" "$scene_config" "$config_path" > "$config_file"

    # 后台启动
    info "启动进程: nohup ./framework/${exec_target} ${config_file} &"
    nohup "./framework/${exec_target}" "$config_file" &

    success "${svrtype} 已在后台启动"
}

# ============================================================
# 内部函数：通过 pid 文件停止单个服务
# 参数: $1=svrtype  $2=processid
# ============================================================
stop_service() {
    local svrtype=$1
    local processid=$2
    local pidfile="${GAME_DIR}/skynet_${processid}.pid"

    if [ -f "$pidfile" ]; then
        local pid
        pid=$(cat "$pidfile")
        if kill -0 "$pid" 2>/dev/null; then
            info "正在停止 ${svrtype}(pid=${pid}) ..."
            kill "$pid" && success "${svrtype} 已停止"
        else
            info "${svrtype} pid=${pid} 进程不存在，跳过"
        fi
        rm -f "$pidfile"
    else
        info "${svrtype} 未找到 pid 文件(${pidfile})，可能未运行"
    fi
}

# ============================================================
# 子命令: start — 按顺序启动所有服务
# ============================================================
cmd_start() {
    info "===== 开始启动所有服务 ====="

    do_build

    # 1. 启动 Manager
    start_service Manager 8 config
    info "等待 Manager 稳定（5 秒）..."
    sleep 5

    # 2. 启动 Game
    start_service Game 2 game_config
    info "等待 Game 稳定（5 秒）..."
    sleep 5

    # 3. 启动 Tool（含 Web 服务）
    start_service Tool 6 tool_config

    success "===== 所有服务启动完毕 ====="
}

# ============================================================
# 子命令: stop — 停止所有服务
# ============================================================
cmd_stop() {
    info "===== 开始停止所有服务 ====="

    stop_service Tool       6
    stop_service Game       2
    stop_service Manager    8
    stop_service DataCenter 9

    success "===== 所有服务已停止 ====="
}

# ============================================================
# 子命令: start-web — 仅启动 Tool（含 Web 服务）
# ============================================================
cmd_start_web() {
    info "===== 仅启动 Tool / Web 服务 ====="

    # 如果其他进程正在运行，跳过编译避免影响
    if ss -tlnp 2>/dev/null | grep -q ":6008 " || \
       netstat -tlnp 2>/dev/null | grep -q ":6008 "; then
        info "检测到 Manager 正在运行，跳过编译以避免影响"
    else
        do_build
    fi
    start_service Tool 6 tool_config

    success "===== Tool / Web 服务启动完毕 ====="
}

# ============================================================
# 子命令: start-manager — 仅启动 Manager
# ============================================================
cmd_start_manager() {
    info "===== 仅启动 Manager ====="

    do_build
    start_service Manager 8 config

    success "===== Manager 启动完毕 ====="
}

# ============================================================
# 子命令: start-game — 仅启动 Game
# ============================================================
cmd_start_game() {
    info "===== 仅启动 Game ====="

    do_build
    start_service Game 2 game_config

    success "===== Game 启动完毕 ====="
}

# ============================================================
# 子命令: status — 查看各服务运行状态（检查端口占用）
# ============================================================
cmd_status() {
    info "===== 服务运行状态检查 ====="

    check_port() {
        local name=$1
        local port=$2
        if ss -tlnp 2>/dev/null | grep -q ":${port} " || \
           netstat -tlnp 2>/dev/null | grep -q ":${port} "; then
            success "${name} 端口 ${port} — 监听中"
        else
            error  "${name} 端口 ${port} — 未监听"
        fi
    }

    check_port "Manager (集群)"    18888
    check_port "DataCenter(集群)"  12531
    check_port "Tool    (集群)"    12532
    check_port "Tool    (Web)"     18000
    check_port "Game    (Login)"   15733
    check_port "Game    (Gateway)" 15734
    check_port "MongoDB (Database)" 27018
    check_port "Mosquitto (MQTT)"  1884

    info "===== 状态检查完毕 ====="
}

# ============================================================
# 子命令: log — 查看 Tool 进程最新日志
# ============================================================
cmd_log() {
    # 在多个可能的日志目录中搜索
    local log_dirs=(
        "${GAME_DIR}/../log"
        "/home/ubuntu/cys-test-20260913/log"
        "${GAME_DIR}/log"
        "${GAME_DIR}"
    )

    local latest=""
    for log_dir in "${log_dirs[@]}"; do
        local found
        found=$(ls -t "${log_dir}"/Tool6_*.log 2>/dev/null | head -1)
        if [ -n "$found" ]; then
            latest="$found"
            break
        fi
    done

    # 如果还没找到，全局搜索
    if [ -z "$latest" ]; then
        info "在常规路径未找到，正在搜索 /home 目录 ..."
        latest=$(find /home -name "Tool6_*.log" -type f 2>/dev/null | head -1)
    fi

    if [ -z "$latest" ]; then
        error "未找到 Tool6 日志文件"
        info "提示: 可查看 nohup 输出获取启动信息:"
        # 显示 nohup.out 最后20行（如果存在）
        if [ -f "${GAME_DIR}/nohup.out" ]; then
            tail -20 "${GAME_DIR}/nohup.out"
        elif [ -f ~/nohup.out ]; then
            tail -20 ~/nohup.out
        else
            info "也未找到 nohup.out 文件"
        fi
        exit 1
    fi

    info "正在追踪日志文件: ${latest}"
    tail -f "$latest"
}

# ============================================================
# 帮助信息
# ============================================================
cmd_help() {
    echo ""
    echo -e "${YELLOW}游戏服务器管理脚本${NC}"
    echo ""
    echo "用法:"
    echo "  $0 <子命令>"
    echo ""
    echo "可用子命令:"
    echo -e "  ${GREEN}start${NC}          按顺序启动所有服务 (Manager → Game → Tool/Web)"
    echo -e "  ${GREEN}stop${NC}           停止所有服务"
    echo -e "  ${GREEN}start-web${NC}      仅启动 Tool 进程（含 Web 服务，端口 8000）"
    echo -e "  ${GREEN}start-manager${NC}  仅启动 Manager"
    echo -e "  ${GREEN}start-game${NC}     仅启动 Game"
    echo -e "  ${GREEN}status${NC}         查看各服务端口监听状态"
    echo -e "  ${GREEN}log${NC}            追踪 Tool 进程最新日志"
    echo ""
    echo "环境配置:"
    echo "  GAME_DIR     = $GAME_DIR"
    echo "  env          = $env"
    echo "  script_root  = $script_root"
    echo "  scene_config = $scene_config"
    echo "  config_path  = $config_path"
    echo ""
}

# ============================================================
# 入口：根据参数分发子命令
# ============================================================
case "${1:-}" in
    start)          cmd_start         ;;
    stop)           cmd_stop          ;;
    start-web)      cmd_start_web     ;;
    start-manager)  cmd_start_manager ;;
    start-game)     cmd_start_game    ;;
    status)         cmd_status        ;;
    log)            cmd_log           ;;
    *)              cmd_help          ;;
esac
