#!/usr/bin/env bash
# 把 Grafana 左上角 logo 换成 DDS 三域诊断图标（可重复执行；升级 Grafana 后再跑一次）。
#
# 为什么不能只覆盖同名文件：Grafana 的 header 图标在 build/static/img/grafana_icon.<hash>.svg，
# URL 带 immutable 缓存，覆盖后浏览器仍用旧图。
# 所以这里做三件事，保证 SRI 校验不被破坏：
#   1) 覆盖同名 SVG（老缓存自然过期后也正确）；
#   2) 生成带内容指纹的新 SVG 文件名，并改写入口 bundle 里对它的引用；
#   3) 入口 bundle 改名 + 重算 sha384 SRI + 更新 assets-manifest.json（否则浏览器会拒载脚本）。
# 备份目录：/var/backups/grafana-branding
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/grafana/branding/ddsmon_icon.svg"
PUBLIC="/usr/share/grafana/public"
BUILD="$PUBLIC/build"
IMGDIR="$BUILD/static/img"
BACKUP="/var/backups/grafana-branding"
GRAFANA_PORT="${GRAFANA_PORT:-3000}"

[ -f "$SRC" ] || { echo "缺少图标文件: $SRC" >&2; exit 1; }

SUDO=""
[ "$(id -u)" -ne 0 ] && SUDO="sudo"
$SUDO install -d -m 0755 "$BACKUP"

TARGETS=$(ls "$IMGDIR"/grafana_icon.*.svg 2>/dev/null || true)
[ -n "$TARGETS" ] || { echo "没找到 grafana_icon.*.svg，Grafana 版本结构可能变了" >&2; exit 1; }

OLD_SVG="$(basename "$(echo "$TARGETS" | head -1)")"
FP="$(sha256sum "$SRC" | cut -c1-8)"
NEW_SVG="grafana_icon.dds${FP}.svg"

# 1) 覆盖同名 SVG + 产出新名字资源
for f in $TARGETS; do
  base="$(basename "$f")"
  [ -f "$BACKUP/$base" ] || $SUDO cp "$f" "$BACKUP/$base"
  $SUDO cp "$SRC" "$f"
  $SUDO chown root:root "$f"
done
$SUDO cp "$SRC" "$IMGDIR/$NEW_SVG"
$SUDO chown root:root "$IMGDIR/$NEW_SVG"
echo "图标已覆盖；新资源: $NEW_SVG"

# 2)+3) 改入口 bundle 引用、改名、重算 SRI、更新 manifest
$SUDO python3 - "$BUILD" "$BACKUP" "$OLD_SVG" "$NEW_SVG" <<'PY'
import hashlib, base64, json, os, sys, glob, shutil

build, backup, old_svg, new_svg = sys.argv[1:5]
manifest_path = os.path.join(build, "assets-manifest.json")
apps = [p for p in glob.glob(os.path.join(build, "app.*.js")) if not p.endswith(".map")]
if not apps:
    sys.exit("entry bundle app.*.js not found")
app_path = apps[0]
app_name = os.path.basename(app_path)

# 备份一次原始入口 bundle，后续基于它重建，保证可重复执行
orig_backup = os.path.join(backup, "orig_" + app_name)
if not os.path.exists(orig_backup):
    shutil.copy2(app_path, orig_backup)

content = open(orig_backup, encoding="utf-8").read().replace(old_svg, new_svg)
digest = hashlib.sha256(content.encode()).hexdigest()[:16]
new_app = f"app.dds{digest}.js"
new_path = os.path.join(build, new_app)
with open(new_path, "w", encoding="utf-8") as fh:
    fh.write(content)
os.chmod(new_path, 0o664)

integrity = "sha384-" + base64.b64encode(hashlib.sha384(content.encode()).digest()).decode()
old_rel = "public/build/" + app_name
new_rel = "public/build/" + new_app

data = json.load(open(manifest_path, encoding="utf-8"))
if isinstance(data.get("app.js"), dict):
    data["app.js"]["src"] = new_rel
    data["app.js"]["integrity"] = integrity
for ep in (data.get("entrypoints") or {}).values():
    if not isinstance(ep, dict):
        continue
    js = (ep.get("assets") or {}).get("js") or []
    for i, v in enumerate(js):
        if v == old_rel:
            js[i] = new_rel
json.dump(data, open(manifest_path, "w", encoding="utf-8"), ensure_ascii=False)
print(f"入口 bundle: {app_name} -> {new_app}")
print(f"新 SRI: {integrity[:29]}...")
PY

$SUDO chown root:root "$BUILD"/assets-manifest.json "$BUILD"/app.dds*.js "$IMGDIR/$NEW_SVG"

# 4) 登录页大标题：LoginTitle="Welcome to Grafana" -> "Welcome to DdsMon"
$SUDO python3 - "$BUILD" <<'PY'
import hashlib, base64, json, glob, os, sys

build = sys.argv[1]
manifest_path = os.path.join(build, "assets-manifest.json")
old = b'LoginTitle="Welcome to Grafana"'
new = b'LoginTitle="Welcome to DdsMon"'

targets = []
for p in glob.glob(os.path.join(build, "*.js")):
    if p.endswith(".map"):
        continue
    data = open(p, "rb").read()
    if old in data:
        targets.append((p, data))

if not targets:
    print("登录标题：已改写或字符串不存在，跳过")
else:
    manifest = json.load(open(manifest_path, encoding="utf-8"))
    for path, data in targets:
        data2 = data.replace(old, new)
        digest = hashlib.sha256(data2).hexdigest()[:16]
        stem = os.path.basename(path).split(".")[0]
        new_name = f"{stem}.dds{digest}.js"
        new_path = os.path.join(build, new_name)
        open(new_path, "wb").write(data2)
        os.chmod(new_path, 0o664)
        integrity = "sha384-" + base64.b64encode(hashlib.sha384(data2).digest()).decode()
        old_rel = "public/build/" + os.path.basename(path)
        new_rel = "public/build/" + new_name
        key = stem + ".js"
        if isinstance(manifest.get(key), dict):
            manifest[key] = {"src": new_rel, "integrity": integrity}
        for ep in (manifest.get("entrypoints") or {}).values():
            if not isinstance(ep, dict):
                continue
            js = (ep.get("assets") or {}).get("js") or []
            for i, v in enumerate(js):
                if v == old_rel:
                    js[i] = new_rel
        print(f"登录标题：{os.path.basename(path)} -> {new_name}")
    json.dump(manifest, open(manifest_path, "w", encoding="utf-8"), ensure_ascii=False)
PY

$SUDO chown root:root "$BUILD"/assets-manifest.json "$BUILD"/*.dds*.js

if [ -f "$PUBLIC/img/grafana_icon.svg" ]; then
  $SUDO cp "$SRC" "$PUBLIC/img/grafana_icon.svg"
  $SUDO chown root:root "$PUBLIC/img/grafana_icon.svg"
fi

$SUDO systemctl restart grafana-server
sleep 7
CODE="$(curl -s -o /dev/null -w '%{http_code}' "http://127.0.0.1:${GRAFANA_PORT}/api/health" || true)"
echo "Grafana /api/health: HTTP ${CODE}"
echo "看板/登录页刷新即可看到新品牌；还原见 ${BACKUP}"
