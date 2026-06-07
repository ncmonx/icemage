;; Example WASM skill (filter-v1): masks every ASCII digit 0-9 with '#'.
;; Use-case: redact card numbers / IDs / tokens-of-digits from noisy output.
;; Register: icmg skill wasm add redact-digits.skill.json
;; Test:     echo "card 4111222233334444 ok" | icmg skill wasm run redact-digits
;;           -> card ################ ok
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
      (if (i32.and (i32.ge_u (local.get $c) (i32.const 48)) (i32.le_u (local.get $c) (i32.const 57)))
        (then (i32.store8 (i32.add (local.get $ptr) (local.get $i)) (i32.const 35))))  ;; '#'
      (local.set $i (i32.add (local.get $i) (i32.const 1)))
      (br $loop)))
    (i64.or (i64.shl (i64.extend_i32_u (local.get $ptr)) (i64.const 32))
            (i64.extend_i32_u (local.get $len)))))
