# Command substitution as expression operands.
set x 2
if {[expr {[expr {$x + 1}] == 3}]} {
    puts yes
} else {
    puts no
}
puts [expr {[expr {$x * 3}] + 1}]
