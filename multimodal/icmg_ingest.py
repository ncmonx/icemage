#!/usr/bin/env python3
"""
icmg ingest sidecar — Phase 38.

Protocol (line-delimited JSON over stdin/stdout — same pattern as embedder):
  -> on startup: {"op":"ready","types":["pdf","image"]}
  -> requests:
       {"op":"pdf",   "id":N, "path":"..."}      -> {"id":N, "text":"...", "pages":N}
       {"op":"image", "id":N, "path":"..."}      -> {"id":N, "text":"...", "lang":"eng"}
       {"op":"shutdown"}

Graceful: optional deps (pdfplumber, pytesseract). Missing dep -> error per request.
"""
import io
import os
import sys
import json

sys.stdin  = io.TextIOWrapper(sys.stdin.buffer,  encoding="utf-8", errors="replace", newline="\n")
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace", newline="\n", line_buffering=True)
sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding="utf-8", errors="replace", newline="\n")

def emit(obj):
    sys.stdout.write(json.dumps(obj, ensure_ascii=False) + "\n")
    sys.stdout.flush()

def extract_pdf(path):
    try:
        import pdfplumber
    except ImportError:
        return {"error": "pdfplumber not installed (pip install pdfplumber)"}
    try:
        text_parts = []
        with pdfplumber.open(path) as pdf:
            for page in pdf.pages:
                text_parts.append(page.extract_text() or "")
            n_pages = len(pdf.pages)
        return {"text": "\n\n".join(text_parts), "pages": n_pages}
    except Exception as e:
        return {"error": f"pdf read failed: {e}"}

def extract_image(path):
    try:
        from PIL import Image
        import pytesseract
    except ImportError:
        return {"error": "pytesseract / Pillow not installed (pip install pytesseract Pillow + tesseract binary)"}
    try:
        img = Image.open(path)
        text = pytesseract.image_to_string(img)
        return {"text": text, "lang": "eng"}
    except Exception as e:
        return {"error": f"ocr failed: {e}"}

def main():
    emit({"op": "ready", "types": ["pdf", "image"]})
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
        rid = req.get("id", 0)
        path = req.get("path", "")
        if op == "pdf":
            r = extract_pdf(path)
        elif op == "image":
            r = extract_image(path)
        else:
            r = {"error": f"unknown op: {op}"}
        r["id"] = rid
        emit(r)
    return 0

if __name__ == "__main__":
    sys.exit(main())
