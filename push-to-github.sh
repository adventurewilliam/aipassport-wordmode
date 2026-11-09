#!/bin/sh
# push-to-github.sh —— 把本仓库推送到 GitHub
#
# 用法：
#   GITHUB_TOKEN=<你的PAT> sh push-to-github.sh [owner/repo]
# 默认目标：adventurewilliam/aipassport-wordmode
#
# 令牌获取（任选其一）：
#   1) 环境变量 GITHUB_TOKEN（推荐：一个有 repo 权限的 Personal Access Token）
#   2) 在 WorkBuddy 环境里，若已授权 GitHub 连接器，脚本会自动尝试读取
#
# 前提：目标空仓库需已存在（本脚本不会创建仓库）。
#   到 https://github.com/new 创建【私有】【不勾选任何初始化文件】的空仓库，
#   并确保令牌对该仓库有写权限。
#
# 安全：令牌只通过环境变量引用，绝不写入命令行参数、日志或仓库文件。
set -e
cd "$(dirname "$0")"

REPO="${1:-adventurewilliam/aipassport-wordmode}"

# 仅当环境变量未设置、且当前环境存在 WorkBuddy 的连接器脚本时才自动读取
if [ -z "$GITHUB_TOKEN" ] && \
   [ -f /root/.codebuddy/skills/github-connector/scripts/get_token.sh ]; then
    # shellcheck disable=SC1091
    . /root/.codebuddy/skills/github-connector/scripts/get_token.sh github >/dev/null 2>&1 || true
fi

if [ -z "$GITHUB_TOKEN" ]; then
    echo "ERROR: 未获取到 GITHUB_TOKEN。请设置：GITHUB_TOKEN=<PAT> sh push-to-github.sh" >&2
    exit 1
fi

git branch -M main
git remote remove origin 2>/dev/null || true
git remote add origin "https://oauth2:${GITHUB_TOKEN}@github.com/${REPO}.git"

echo "推送到 ${REPO} ..."
git push -u origin main

echo
echo "===== 校验远端 ====="
GITHUB_TOKEN="$GITHUB_TOKEN" REPO="$REPO" python3 - <<'PY'
import os, json, urllib.request
repo = os.environ['REPO']
T = os.environ['GITHUB_TOKEN']
H = {'Authorization': 'Bearer '+T, 'Accept':'application/vnd.github+json',
     'User-Agent':'workbuddy'}
def get(u):
    r = urllib.request.Request(u, headers=H)
    with urllib.request.urlopen(r, timeout=30) as f:
        return json.loads(f.read().decode())
d = get(f'https://api.github.com/repos/{repo}')
print('仓库:', d['html_url'], '| private =', d['private'], '| branch =', d['default_branch'])
tree = get(f'https://api.github.com/repos/{repo}/git/trees/main?recursive=1')
paths = [x['path'] for x in tree.get('tree', [])]
print('远端文件数:', len(paths))
for key in ['README.md', 'integration/INTEGRATION.md',
            'firmware/components/core/services/src/word_mode.c',
            'firmware/components/ui/presentation/src/ui_wordmode.c',
            'firmware/components/app/src/wordmode_app.c',
            'host/test_word_mode.c', 'assets/words/index.json',
            'assets/audio/apple.adp1']:
    print(('  OK  ' if key in paths else '  MISS'), key)
PY
