# Nested command substitution and side-effects.
set x [expr {2 + [expr {3 * 4}]}]
puts $x
set y [puts side]
puts "<$y>"
