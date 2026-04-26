# Operator precedence and boolean integer output.
puts [expr {1 || 0 && 0}]
puts [expr {!(1 && 0)}]
puts [expr {(3 + 2) * 4 == 20}]
puts [expr {7 % 4}]
