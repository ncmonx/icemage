;; filter-v1 fixture: infinite loop in icmg_filter -> must be killed by fuel/epoch.
(module
  (memory (export "memory") 1)
  (global $bump (mut i32) (i32.const 1024))
  (func (export "icmg_alloc") (param $n i32) (result i32)
    (local $p i32)
    (local.set $p (global.get $bump))
    (global.set $bump (i32.add (global.get $bump) (local.get $n)))
    (local.get $p))
  (func (export "icmg_filter") (param $ptr i32) (param $len i32) (result i64)
    (loop $l (br $l))
    (i64.const 0)))
