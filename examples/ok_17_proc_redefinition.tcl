# Redefinition should replace previous body.
proc f {x} {
    return [expr {$x + 1}]
}
puts [f 1]
proc f {x} {
    return [expr {$x + 2}]
}
puts [f 1]
