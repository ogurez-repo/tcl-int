# Empty return and implicit last-command result.
proc emptyReturn {} {
    return
}
proc lastResult {} {
    set x 1
    incr x
}
puts "<[emptyReturn]>"
puts [lastResult]
