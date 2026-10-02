#!/usr/bin/env bash
set -euo pipefail

# Simulates a client uploading a video in chunks to the object storage server.
#
# Usage:
#   ./client-upload.sh <session-key> [video-path] [chunk-size]
#
#   session-key  required, the upload_session key returned by POST /upload
#   video-path   optional, defaults to ~/Downloads/video1.mp4
#   chunk-size   optional, defaults to 5M (passed straight to `split -b`)

SESSION_KEY="${1:-}"
VIDEO_PATH="${2:-$HOME/Downloads/video1.mp4}"
CHUNK_SIZE="${3:-5M}"
HOST="${UPLOAD_HOST:-http://localhost:3001}"

if [ -z "$SESSION_KEY" ]; then
    echo "Usage: $0 <session-key> [video-path] [chunk-size]"
    exit 1
fi

if [ ! -f "$VIDEO_PATH" ]; then
    echo "Video file not found: $VIDEO_PATH"
    exit 1
fi

CHUNK_DIR="$(mktemp -d)"
trap 'rm -rf "$CHUNK_DIR"' EXIT

echo "Splitting $VIDEO_PATH into $CHUNK_SIZE chunks..."
split -b "$CHUNK_SIZE" "$VIDEO_PATH" "$CHUNK_DIR/chunk_"

CHUNK_COUNT=$(ls -1 "$CHUNK_DIR" | wc -l)
echo "Uploading $CHUNK_COUNT chunks to session $SESSION_KEY..."

i=0
for chunk in "$CHUNK_DIR"/chunk_*; do
    i=$((i + 1))
    size=$(stat -c%s "$chunk")
    echo "[$i/$CHUNK_COUNT] sending $chunk ($size bytes)"

    curl -sS -X POST \
        --data-binary "@$chunk" \
        -w "  -> HTTP %{http_code}\n" \
        -o /dev/null \
        "$HOST/upload/$SESSION_KEY/chunk"
done

echo "Done."
