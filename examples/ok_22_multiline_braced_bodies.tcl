# Multiline braced script bodies.
set total 0
for {set i 1} {$i <= 3} {incr i} {
    set total [expr {$total + $i}]
}
puts $total
