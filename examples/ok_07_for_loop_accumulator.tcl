# for loop with accumulator.
set sum 0
for {set i 1} {$i <= 5} {incr i} {
    set sum [expr {$sum + $i}]
}
puts $sum

for {set j 0} {$j < 5} {incr j} {
    puts $j
}
