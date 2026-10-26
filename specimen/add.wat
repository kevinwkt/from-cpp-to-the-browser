(module
  (type (;0;) (func (param i32 i32) (result i32)))
  (func (;0;) (type 0) (param i32 i32) (result i32)
    local.get 0
    i32.const 1
    i32.shl
    local.get 1
    i32.add)
  (memory (;0;) 1)
  (export "memory" (memory 0))
  (export "add" (func 0))
  (@custom "producers" "\01\0cprocessed-by\01\0eHomebrew clang\0623.1.4")
  (@custom "target_features" "\08+\0fmutable-globals+\13nontrapping-fptoint+\0bbulk-memory+\08sign-ext+\0freference-types+\0amultivalue+\0fbulk-memory-opt+\16call-indirect-overlong"))
