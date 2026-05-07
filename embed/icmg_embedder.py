#!/usr/bin/env python3
"""
icmg embedder sidecar — Phase 23 Task 1.

Protocol (line-delimited JSON over stdin/stdout):
  -> on startup, emits: {"op":"ready","dim":384,"model":"all-MiniLM-L6-v2"}
  -> requests:
       {"op":"embed","id":N,"text":"..."}     -> {"id":N,"vec":[...],"dim":384}
       {"op":"shutdown"}                        -> exits
  -> on error: {"id":N,"error":"<msg>"}

Graceful: if sentence-transformers missing, emits one error line then exits 0.
Stdout is line-buffered (-u in launcher).
"""
import sys
import json

MODEL_NAME = "all-MiniLM-L6-v2"
DIM = 384

def main():
    try:
        from sentence_transformers import SentenceTransformer
    except Exception as e:
        sys.stdout.write(json.dumps({"op": "error", "error": f"sentence-transformers not installed: {e}"}) + "\n")
        sys.stdout.flush()
        return 0

    try:
        model = SentenceTransformer(MODEL_NAME)
    except Exception as e:
        sys.stdout.write(json.dumps({"op": "error", "error": f"model load failed: {e}"}) + "\n")
        sys.stdout.flush()
        return 0

    sys.stdout.write(json.dumps({"op": "ready", "dim": DIM, "model": MODEL_NAME}) + "\n")
    sys.stdout.flush()

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception as e:
            sys.stdout.write(json.dumps({"id": 0, "error": f"bad json: {e}"}) + "\n")
            sys.stdout.flush()
            continue
        op = req.get("op", "")
        if op == "shutdown":
            return 0
        if op != "embed":
            sys.stdout.write(json.dumps({"id": req.get("id", 0), "error": f"unknown op: {op}"}) + "\n")
            sys.stdout.flush()
            continue
        text = req.get("text", "")
        try:
            vec = model.encode(text, normalize_embeddings=False).tolist()
            sys.stdout.write(json.dumps({"id": req.get("id", 0), "vec": vec, "dim": DIM}) + "\n")
        except Exception as e:
            sys.stdout.write(json.dumps({"id": req.get("id", 0), "error": str(e)}) + "\n")
        sys.stdout.flush()

    return 0

if __name__ == "__main__":
    sys.exit(main())
