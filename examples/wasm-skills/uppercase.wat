;; Example WASM skill (filter-v1 ABI): uppercases ASCII a-z in place.
;;
;; A filter-v1 module MUST export exactly:
;;   (memory (export "memory") ...)            ;; linear memory the host reads/writes
;;   (func (export "icmg_alloc") (param i32) (result i32))   ;; host calls to place input
;;   (func (export "icmg_filter")(param i32 i32)(result i64)) ;; returns (out_ptr<<32 | out_len)
;;
;; The host: calls icmg_alloc(len) -> ptr, copies your input bytes to memory[ptr..],
;; calls icmg_filter(ptr,len), unpacks the i64 result, and reads memory[out_ptr..out_ptr+out_len]
;; as the filtered text. Pure computation only -- zero host imports (strict sandbox).
;;
;; Register:  icmg skill wasm add uppercase.skill.json
;; Test:      echo "hello" | icmg skill wasm run uppercase     ->  HELLO
(module
  (memory (export "memory") 2)
  (global $bump (mut i32) (i32.const 1024))
  (func (export "icmg_alloc") (param $n i32) (result i32)
    (local $p i32)
    (local.set $p (global.get $bump))
    (global.set $bump (i32.add (global.get $bump) (local.get $n)))
    (local.get $p))
  (func (export "icmg_filter") (param $ptr i32) (param $len i32) (result i64)
    (local $i i32) (local $c i32)
    (block $done (loop $loop
      (br_if $done (i32.ge_u (local.get $i) (local.get $len)))
      (local.set $c (i32.load8_u (i32.add (local.get $ptr) (local.get $i))))
      (if (i32.and (i32.ge_u (local.get $c) (i32.const 97)) (i32.le_u (local.get $c) (i32.const 122)))
        (then (i32.store8 (i32.add (local.get $ptr) (local.get $i)) (i32.sub (local.get $c) (i32.const 32)))))
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $loop)))
    (i64.or (i64.shl (i64.extend_i32_u (local.get $ptr)) (i64.const 32))
            (i64.extend_i32_u (local.get $len)))))
