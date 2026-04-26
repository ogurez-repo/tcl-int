# Procedure scope and explicit return.
set x global
proc compute {v} {
    set x local
    set y [expr {$v * 2}]
    return [expr {$y + 2}]
}
puts [compute 20]
proc localValue {} {
    set x 99
    return $x
}
puts [localValue]
puts $x
