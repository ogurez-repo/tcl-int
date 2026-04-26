# Nested loop control flow.
for {set i 0} {$i < 3} {incr i} {
    set j 0
    while {$j < 3} {
        incr j
        if {$j == 2} {continue}
        if {$i == 2 && $j == 3} {break}
        puts "$i:$j"
    }
}
