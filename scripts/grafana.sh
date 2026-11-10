#!/usr/bin/env bash
# 安装并配置 Prometheus + Grafana，把 ddsmon 的指标接到 Grafana 看板。
# 只在本机（WSL Ubuntu）使用；需要 sudo 权限安装系统包。
#
# 可选环境变量：
#   METRICS_PORT       ddsmon 指标端口，默认 9200
#   PROM_PORT          Prometheus 端口，默认 9090
#   GRAFANA_PORT       Grafana 端口，默认 3000
#   GRAFANA_APT_BASE   Grafana APT 源，默认 https://apt.grafana.com
#                      （国内慢可换 https://mirrors.aliyun.com/grafana/apt）
#   GRAFANA_DEB_URL    直接给一个 .deb 下载地址，跳过 APT 源，用 dpkg 安装
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
METRICS_PORT="${METRICS_PORT:-9200}"
PROM_PORT="${PROM_PORT:-9090}"
GRAFANA_PORT="${GRAFANA_PORT:-3000}"
GRAFANA_APT_BASE="${GRAFANA_APT_BASE:-https://apt.grafana.com}"
GRAFANA_DEB_URL="${GRAFANA_DEB_URL:-}"

log() { printf '\033[1;36m[grafana]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[grafana]\033[0m %s\n' "$*" >&2; }

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
  if sudo -n true 2>/dev/null; then
    SUDO="sudo"
  else
    warn "需要 sudo 权限安装/配置系统服务，下面会提示输入密码。"
    SUDO="sudo"
  fi
fi

# ---------------------------------------------------------------- Prometheus
if ! command -v prometheus >/dev/null 2>&1 && [ ! -x /usr/bin/prometheus ]; then
  log "安装 Prometheus ..."
  $SUDO apt-get update -qq
  $SUDO DEBIAN_FRONTEND=noninteractive apt-get install -y prometheus
fi

# ---------------------------------------------------------------- Grafana
if ! command -v grafana-server >/dev/null 2>&1 && [ ! -x /usr/sbin/grafana-server ]; then
  if [ -n "$GRAFANA_DEB_URL" ]; then
    # 大文件不要放 /tmp：WSL 的 /tmp 是 tmpfs，实例重启后会被清空
    log "直接下载 deb 安装（走 $GRAFANA_DEB_URL）"
    curl -fL --progress-bar -o "$HOME/grafana.deb" "$GRAFANA_DEB_URL"
    $SUDO dpkg -i "$HOME/grafana.deb" || $SUDO DEBIAN_FRONTEND=noninteractive apt-get -f install -y
    rm -f "$HOME/grafana.deb"
  else
    log "添加 Grafana APT 源并安装（源: $GRAFANA_APT_BASE）..."
    $SUDO install -d -m 0755 /etc/apt/keyrings
    curl -fsSL -o /tmp/grafana.key https://apt.grafana.com/gpg.key
    $SUDO gpg --batch --yes --dearmor -o /etc/apt/keyrings/grafana.gpg /tmp/grafana.key
    printf 'deb [signed-by=/etc/apt/keyrings/grafana.gpg] %s stable main\n' "$GRAFANA_APT_BASE" \
      > /tmp/grafana.list
    $SUDO cp /tmp/grafana.list /etc/apt/sources.list.d/grafana.list
    $SUDO apt-get update -qq
    $SUDO DEBIAN_FRONTEND=noninteractive apt-get install -y grafana
  fi
fi

# ---------------------------------------------------------------- Prometheus 配置
log "写入 Prometheus 抓取配置（ddsmon -> 127.0.0.1:${METRICS_PORT}，1s 间隔）"
if [ ! -f /etc/prometheus/prometheus.yml.orig ]; then
  $SUDO cp /etc/prometheus/prometheus.yml /etc/prometheus/prometheus.yml.orig 2>/dev/null || true
fi
sed "s|127.0.0.1:9200|127.0.0.1:${METRICS_PORT}|" "$ROOT/config/prometheus.yml" \
  > /tmp/ddsmon-prometheus.yml
$SUDO cp /tmp/ddsmon-prometheus.yml /etc/prometheus/prometheus.yml
$SUDO systemctl enable prometheus >/dev/null 2>&1 || true
$SUDO systemctl restart prometheus

# ---------------------------------------------------------------- Grafana 配置
log "安装 Grafana 数据源与看板（provisioning）"
$SUDO install -d -m 0755 /etc/grafana/provisioning/datasources /etc/grafana/provisioning/dashboards
$SUDO install -d -m 0755 /var/lib/grafana/dashboards
$SUDO cp "$ROOT/grafana/provisioning/datasources/ddsmon.yaml" \
  /etc/grafana/provisioning/datasources/ddsmon.yaml
$SUDO cp "$ROOT/grafana/provisioning/dashboards/ddsmon.yaml" \
  /etc/grafana/provisioning/dashboards/ddsmon.yaml
$SUDO cp "$ROOT/grafana/dashboards/ddsmon.json" /var/lib/grafana/dashboards/ddsmon.json

$SUDO systemctl enable grafana-server >/dev/null 2>&1 || true
$SUDO systemctl restart grafana-server

# ---------------------------------------------------------------- 自检
sleep 3
PROM_HEALTH="$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:${PROM_PORT}/-/healthy" || true)"
GRAF_HEALTH="$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:${GRAFANA_PORT}/api/health" || true)"
log "Prometheus /-/healthy: HTTP ${PROM_HEALTH}（端口 ${PROM_PORT}）"
log "Grafana /api/health:   HTTP ${GRAF_HEALTH}（端口 ${GRAFANA_PORT}）"
log "看板地址: http://localhost:${GRAFANA_PORT}/d/ddsmon-main   （默认账号 admin / admin）"
log "抓取目标: http://127.0.0.1:${PROM_PORT}/targets"
cat <<EOF

启动被监控侧（另开终端）：
  cd ${ROOT}
  ./build/ddsmon-sim   -c config/ddsmon.json --node all -d 1800 &
  ./build/ddsmon metrics -c config/ddsmon.json --metrics-port ${METRICS_PORT} -d 1800 &

只想用 daemon（同时回写诊断域并出指标）：
  ./build/ddsmon daemon -c config/ddsmon.json --metrics-port ${METRICS_PORT} --report-interval 2
EOF
