# Tcl docs: && and || are lazy operators.
puts [expr {0 && [unknown_cmd]}]
puts [expr {1 || [unknown_cmd]}]
