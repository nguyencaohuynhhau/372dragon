#!/bin/bash
set -e

# ============================================================
# 多区管理脚本
# 用法:
#   ./start_zone.sh base              启动基础服务 (Manager → DataCenter → Tool)
#   ./start_zone.sh zone <区号>       启动指定区 (1区=processid 2, 2区=processid 3, ...)
#   ./start_zone.sh all               启动全部 (base + 所有已配置区)
#   ./start_zone.sh stop              停止所有服务
#   ./start_zone.sh stop-base         仅停止基础服务（需先停止所有分区）
#   ./start_zone.sh stop-zone <区号>  仅停止指定区
#   ./start_zone.sh status            查看所有服务状态
# ============================================================

# ---------- 颜色定义 ----------
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()    { echo -e "${YELLOW}[INFO]${NC} $*"; }
success() { echo -e "${GREEN}[OK]${NC}   $*"; }
error()   { echo -e "${RED}[ERR]${NC}  $*"; }
title()   { echo -e "${CYAN}===== $* =====${NC}"; }

# ---------- 路径与环境配置 ----------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
GAME_DIR="$SCRIPT_DIR"
script_root="$SERVER_DIR/Game_2/Script"
scene_config="$SERVER_DIR/Game_2/Scene"
config_path="$SERVER_DIR/configs/%s"
env=publish_yu

# ---------- 已配置的区列表（区号, 对应 processid = 区号+1）----------
# 如需新增区，在此追加区号即可
ZONE_LIST=(1)

# ---------- 进入工作目录 ----------
cd "$GAME_DIR"

# ============================================================
# 工具函数：区号 → processid
# ============================================================
zone_to_pid() {
    echo $(( $1 + 1 ))
}

# ============================================================
# 工具函数：检查服务是否已在运行
# 返回 0 = 正在运行，1 = 未运行
# ============================================================
is_running() {
    local processid=$1
    local pidfile="${GAME_DIR}/skynet_${processid}.pid"
    if [ -f "$pidfile" ]; then
        local pid
        pid=$(cat "$pidfile")
        if kill -0 "$pid" 2>/dev/null; then
            return 0
        fi
    fi
    return 1
}

# ============================================================
# 内部函数：启动单个服务（带前置检查 + 启动后验证）
# 参数: $1=svrtype  $2=processid  $3=config_file_name
# ============================================================
start_service() {
    local svrtype=$1
    local processid=$2
    local config_file=$3
    local exec_target="${svrtype}_tlbb_${processid}"
    local pidfile="${GAME_DIR}/skynet_${processid}.pid"

    # 检查是否已在运行，避免重复启动
    if is_running "$processid"; then
        local running_pid
        running_pid=$(cat "$pidfile")
        info "${svrtype}(processid=${processid}) 已在运行 (pid=${running_pid})，跳过"
        return 0
    fi

    info "准备启动 ${svrtype}(processid=${processid}) ..."

    # 清理残留 pid 文件
    rm -f "$pidfile"

    # 拷贝可执行文件
    cp ./framework/skynet "./framework/${exec_target}"

    # 生成独立的配置文件（每个进程用独立文件，避免多进程写冲突）
    info "生成配置文件 -> ${config_file}"
    lua config_maker.lua "$env" "$script_root" "$processid" "$svrtype" "$scene_config" "$config_path" > "$config_file"

    # 后台启动
    info "后台启动: nohup ./framework/${exec_target} ${config_file}"
    nohup "./framework/${exec_target}" "$config_file" >> "${GAME_DIR}/nohup_${processid}.out" 2>&1 &

    # 等待进程稳定，最多等 10 秒
    local wait_sec=0
    while [ $wait_sec -lt 10 ]; do
        sleep 1
        wait_sec=$(( wait_sec + 1 ))
        if is_running "$processid"; then
            success "${svrtype}(processid=${processid}) 启动成功 (pid=$(cat "$pidfile"))"
            return 0
        fi
    done

    # 10 秒内未产生 pid 文件则报警（不用 exit，允许继续执行后续服务）
    error "${svrtype}(processid=${processid}) 10 秒内未检测到 pid 文件，请检查日志: nohup_${processid}.out"
    return 1
}

# ============================================================
# 内部函数：停止单个服务
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
            info "正在停止 ${svrtype}(processid=${processid}, pid=${pid}) ..."
            kill "$pid"
            # 等待进程退出，最多 15 秒
            local wait_count=0
            while kill -0 "$pid" 2>/dev/null && [ $wait_count -lt 15 ]; do
                sleep 1
                wait_count=$(( wait_count + 1 ))
            done
            if kill -0 "$pid" 2>/dev/null; then
                info "进程未响应，强制终止 ..."
                kill -9 "$pid" 2>/dev/null || true
                sleep 1
            fi
            success "${svrtype}(processid=${processid}) 已停止"
        else
            info "${svrtype}(processid=${processid}) pid=${pid} 进程不存在，清理 pid 文件"
        fi
        rm -f "$pidfile"
    else
        info "${svrtype}(processid=${processid}) 未找到 pid 文件，可能未运行"
    fi
}

# ============================================================
# 子命令: base — 按依赖顺序启动基础服务
# 启动顺序: Manager(8) → DataCenter(9) → Tool(6)
# ============================================================
cmd_base() {
    title "启动基础服务"

    # 1. Manager（svrtype 必须是 Manager，main.lua 走 init_manager_server 分支）
    start_service Manager 8 config_8
    info "等待 Manager 稳定（5 秒）..."
    sleep 5

    # 2. DataCenter（必须在 Manager 之后启动）
    start_service DataCenter 9 config_9
    info "等待 DataCenter 稳定（5 秒）..."
    sleep 5

    # 3. Tool（含 Web 服务，端口 8000）
    start_service Tool 6 config_6
    # [audit 2026-09-03] Game phai khoi dong SAU khi Tool da dang ky .ranking, neu khong cluster_agent
    # cua Game khong bao gio proxy .ranking (limit_shop/activity/huabang loi im lang + traceback moi lan vao scene)
    info "Cho Tool dang ky service ranking (toi da 60 giay)..."
    for _i in $(seq 1 60); do
        _tlog=$(ls -t ${GAME_DIR}/../log/*/Tool6_*.log 2>/dev/null | head -1)
        if [ -n "$_tlog" ] && grep -q "LAUNCH snlua ranking" "$_tlog"; then break; fi
        sleep 1
    done
    sleep 5

    success "基础服务启动完毕"
}

# ============================================================
# 子命令: zone <区号> — 启动指定区的 Game 进程
# ============================================================
cmd_zone() {
    local zone_num=$1
    if [ -z "$zone_num" ]; then
        error "请指定区号，例如: $0 zone 1"
        exit 1
    fi

    local processid
    processid=$(zone_to_pid "$zone_num")
    local config_file="config_${processid}"

    title "启动 ${zone_num}区 (processid=${processid})"
    start_service Game "$processid" "$config_file"
    success "${zone_num}区 启动完毕"
}

# ============================================================
# 子命令: all — 启动全部服务（base + 所有已配置区）
# ============================================================
cmd_all() {
    title "启动全部服务"

    # 先启动基础服务
    cmd_base

    info "等待基础服务就绪（3 秒）..."
    sleep 3

    # 启动所有已配置的区
    for zone_num in "${ZONE_LIST[@]}"; do
        local processid
        processid=$(zone_to_pid "$zone_num")
        local config_file="config_${processid}"
        start_service Game "$processid" "$config_file"
        sleep 2
    done

    success "全部服务启动完毕"
}

# ============================================================
# 子命令: stop — 停止所有服务（逆序停止）
# ============================================================
cmd_stop() {
    title "停止所有服务"

    # 逆序：先停各区 Game
    for zone_num in "${ZONE_LIST[@]}"; do
        local processid
        processid=$(zone_to_pid "$zone_num")
        stop_service Game "$processid"
    done

    # 再停基础服务（逆序）
    stop_service Tool       6
    stop_service DataCenter 9
    stop_service Manager    8

    success "所有服务已停止"
}

# ============================================================
# 子命令: stop-base — 仅停止基础服务
# 为避免在线 Game 进程失去 Manager/DataCenter/Tool，必须先停止所有分区。
# ============================================================
cmd_stop_base() {
    local running_zones=()
    for zone_num in "${ZONE_LIST[@]}"; do
        local processid
        processid=$(zone_to_pid "$zone_num")
        if is_running "$processid"; then
            running_zones+=("$zone_num")
        fi
    done

    if [ ${#running_zones[@]} -gt 0 ]; then
        error "仍有分区正在运行: ${running_zones[*]}区"
        error "请先执行 stop-zone 停止对应分区，再执行: $0 stop-base"
        return 1
    fi

    title "停止基础服务"
    stop_service Tool       6
    stop_service DataCenter 9
    stop_service Manager    8
    success "基础服务已停止"
}

# ============================================================
# 子命令: stop-zone <区号> — 仅停止指定区
# ============================================================
cmd_stop_zone() {
    local zone_num=$1
    if [ -z "$zone_num" ]; then
        error "请指定区号，例如: $0 stop-zone 1"
        exit 1
    fi

    local processid
    processid=$(zone_to_pid "$zone_num")

    title "停止 ${zone_num}区 (processid=${processid})"
    stop_service Game "$processid"
    success "${zone_num}区 已停止"
}

# ============================================================
# 子命令: status — 查看所有服务运行状态
# ============================================================
cmd_status() {
    title "服务运行状态"

    check_service() {
        local name=$1
        local processid=$2
        local pidfile="${GAME_DIR}/skynet_${processid}.pid"
        if is_running "$processid"; then
            local pid
            pid=$(cat "$pidfile")
            success "${name}(processid=${processid}) — 运行中 (pid=${pid})"
        else
            error  "${name}(processid=${processid}) — 未运行"
        fi
    }

    # 基础服务
    check_service Manager    8
    check_service DataCenter 9
    check_service Tool       6

    # 各区 Game
    for zone_num in "${ZONE_LIST[@]}"; do
        local processid
        processid=$(zone_to_pid "$zone_num")
        check_service "Game ${zone_num}区" "$processid"
    done

    info "状态检查完毕"
}

# ============================================================
# 帮助信息
# ============================================================
cmd_help() {
    echo ""
    echo -e "${CYAN}多区管理脚本${NC}"
    echo ""
    echo "用法:"
    echo "  $0 <子命令> [参数]"
    echo ""
    echo "可用子命令:"
    echo -e "  ${GREEN}base${NC}              启动基础服务 (Manager → DataCenter → Tool)"
    echo -e "  ${GREEN}zone <区号>${NC}       启动指定区 Game 进程 (区号+1=processid)"
    echo -e "  ${GREEN}all${NC}               启动全部服务 (base + 所有已配置区)"
    echo -e "  ${GREEN}stop${NC}              停止所有服务"
    echo -e "  ${GREEN}stop-base${NC}         仅停止基础服务（需先停止所有分区）"
    echo -e "  ${GREEN}stop-zone <区号>${NC}  仅停止指定区"
    echo -e "  ${GREEN}status${NC}            查看所有服务状态"
    echo ""
    echo "区号映射:"
    echo "  1区 → processid=2 (Game_tlbb_2, 端口6002)"
    echo "  2区 → processid=3 (Game_tlbb_3, 端口6003)"
    echo ""
    echo "环境配置:"
    echo "  GAME_DIR     = $GAME_DIR"
    echo "  env          = $env"
    echo "  script_root  = $script_root"
    echo "  scene_config = $scene_config"
    echo "  config_path  = $config_path"
    echo ""
    echo "注意: 每个进程使用独立 config 文件 (config_8, config_9, config_6, config_2, config_3)"
    echo ""
}

# ============================================================
# 入口：根据参数分发子命令
# ============================================================
case "${1:-}" in
    base)       cmd_base                ;;
    zone)       cmd_zone      "${2:-}"  ;;
    all)        cmd_all                 ;;
    stop)       cmd_stop                ;;
    stop-base)  cmd_stop_base           ;;
    stop-zone)  cmd_stop_zone "${2:-}"  ;;
    status)     cmd_status              ;;
    *)          cmd_help                ;;
esac
