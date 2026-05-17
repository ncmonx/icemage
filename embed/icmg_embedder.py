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

CRITICAL: only JSON goes to stdout. All warnings/logs/progress -> stderr.
sentence-transformers + transformers + huggingface_hub all emit warnings on
import/load that would otherwise pollute stdout and break the parser. We
silence + redirect them at module level BEFORE importing.
"""
# v1.5.2: belt-and-suspenders — Windows child *should* inherit parent's
# SetErrorMode, but enforce locally too in case a transformers/torch C
# extension launches a helper without inheritance. SEM_FAILCRITICALERRORS
# suppresses the "B:/ — system cannot find drive" dialog seen when MSYS-style
# bash paths like /b/x reach Win32 file APIs as `B:\x`.
import sys
if sys.platform == "win32":
    try:
        import ctypes
        SEM_FAILCRITICALERRORS = 0x0001
        SEM_NOOPENFILEERRORBOX = 0x8000
        ctypes.windll.kernel32.SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX)
    except Exception:
        pass  # graceful: popup may recur, but sidecar still runs

import io
import os
import json
import warnings
import logging
import contextlib

# Force UTF-8 stdio so multilingual text doesn't crash on Windows cp1252.
sys.stdin  = io.TextIOWrapper(sys.stdin.buffer,  encoding="utf-8", errors="replace", newline="\n")
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace", newline="\n", line_buffering=True)
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace", newline="\n")

# Silence library noise on stdout.
warnings.filterwarnings("ignore")
logging.getLogger().setLevel(logging.ERROR)
os.environ.setdefault("TRANSFORMERS_VERBOSITY", "error")
os.environ.setdefault("HF_HUB_DISABLE_PROGRESS_BARS", "1")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

MODEL_NAME = "all-MiniLM-L6-v2"
DIM = 384

def emit(obj):
    """Write a single JSON line to stdout, flushed.

    Graceful on broken pipe (parent died / closed stdout). Exits 0 instead of
    raising OSError to the C++ parent which logs it as a sidecar crash.
    """
    try:
        sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
        sys.stdout.flush()
    except (OSError, BrokenPipeError, ValueError):
        try:
            sys.stderr.write("icmg-embedder: parent pipe closed, exiting\n")
        except Exception:
            pass
        sys.exit(0)

def main():
    # Redirect anything-that-isn't-our-protocol to stderr during model load.
    try:
        with contextlib.redirect_stdout(sys.stderr):
            from sentence_transformers import SentenceTransformer
            model = SentenceTransformer(MODEL_NAME)
    except ImportError as e:
        emit({"op": "error", "error": f"sentence-transformers not installed: {e}"})
        return 0
    except Exception as e:
        emit({"op": "error", "error": f"model load failed: {e}"})
        return 0

    emit({"op": "ready", "dim": DIM, "model": MODEL_NAME})

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception as e:
            emit({"id": 0, "error": f"bad json: {e}"})
            continue
        op = req.get("op", "")
        if op == "shutdown":
            return 0
        if op != "embed":
            emit({"id": req.get("id", 0), "error": f"unknown op: {op}"})
            continue
        text = req.get("text", "")
        try:
            with contextlib.redirect_stdout(sys.stderr):
                vec = model.encode(text, normalize_embeddings=False).tolist()
            emit({"id": req.get("id", 0), "vec": vec, "dim": DIM})
        except Exception as e:
            emit({"id": req.get("id", 0), "error": str(e)})

    return 0

if __name__ == "__main__":
    sys.exit(main())
