#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

HOST="root@164.92.222.201"
KEY="$HOME/.ssh/droplet"
LOCAL_BIN="/tmp/co2-release"
REMOTE_TMP="/tmp/co2-release"
REMOTE_BIN="/opt/co2/co2"

if [[ ! -f "$KEY" ]]; then
  echo "SSH key not found: $KEY" >&2
  echo "One-time setup:" >&2
  echo "  mkdir -p ~/.ssh" >&2
  echo "  cp /mnt/c/Users/joeod/.ssh/droplet ~/.ssh/droplet" >&2
  echo "  chmod 600 ~/.ssh/droplet" >&2
  exit 1
fi

cleanup() {
  if [[ -n "${SSH_AGENT_PID:-}" ]]; then
    ssh-agent -k >/dev/null 2>&1 || true
  fi
}

trap cleanup EXIT

echo "Unlocking SSH key..."
eval "$(ssh-agent -s)" >/dev/null
ssh-add "$KEY"

echo "Running tests..."
go test ./...

echo "Building Linux binary..."
GOOS=linux GOARCH=amd64 go build -o "$LOCAL_BIN" ./api

echo "Uploading binary..."
scp -i "$KEY" "$LOCAL_BIN" "$HOST:$REMOTE_TMP"

echo "Restarting service..."
ssh -i "$KEY" "$HOST" "
  set -e
  sudo install -o www-data -g www-data -m 755 '$REMOTE_TMP' '$REMOTE_BIN'
  rm -f '$REMOTE_TMP'
  sudo systemctl restart co2.service
  sudo systemctl --no-pager --full status co2.service | sed -n '1,12p'
  curl -fsS http://127.0.0.1:8080/healthz
  echo
"

echo "Done."
