# while with continue and break.
set i 0
while {$i < 6} {
    incr i
    if {$i == 2} {continue}
    if {$i == 5} {break}
    puts $i
}
