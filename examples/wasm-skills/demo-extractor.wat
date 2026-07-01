(module
  ;; extractor-v1 ABI demo / scaffold.
  ;;
  ;; This hand-written WAT emits a FIXED ExtractResult JSON to demonstrate the
  ;; extractor-v1 contract end-to-end. A real language extractor would tokenize
  ;; the input source (the bytes at ptr..ptr+len) and build the JSON dynamically
  ;; -- in practice you compile that from Rust / AssemblyScript / Zig to .wasm,
  ;; not by hand. The exports + the packed-i64 return convention below are the
  ;; whole ABI; keep them identical in your compiled module.
  ;;
  ;; Exports:
  ;;   (memory "memory")
  ;;   icmg_alloc(size i32) -> ptr i32            ; bump-allocate an input buffer
  ;;   icmg_extract(ptr i32, len i32) -> i64      ; returns (outPtr<<32 | outLen)
  ;; Output at [outPtr, outPtr+outLen) is UTF-8 JSON matching graph::ExtractResult:
  ;;   {"context":str,"imports":[str],"namespaces":[str],"classes":[str],
  ;;    "functions":[str],"tables":[str]}   (all fields optional)

  (memory (export "memory") 2)

  ;; Fixed output JSON placed at offset 2048 (out of the way of the input buffer).
  ;; {"context":"demo extractor","imports":["std","fmt"],"functions":["main"]}
  (data (i32.const 2048)
    "{\22context\22:\22demo extractor\22,\22imports\22:[\22std\22,\22fmt\22],\22functions\22:[\22main\22]}")

  (global $bump (mut i32) (i32.const 1024))

  (func (export "icmg_alloc") (param $n i32) (result i32)
    (local $p i32)
    (local.set $p (global.get $bump))
    (global.set $bump (i32.add (global.get $bump) (local.get $n)))
    (local.get $p))

  (func (export "icmg_extract") (param $ptr i32) (param $len i32) (result i64)
    ;; ignore input; return the fixed JSON slice: outPtr=2048, outLen=73
    (i64.or (i64.shl (i64.extend_i32_u (i32.const 2048)) (i64.const 32))
            (i64.extend_i32_u (i32.const 73))))
)
