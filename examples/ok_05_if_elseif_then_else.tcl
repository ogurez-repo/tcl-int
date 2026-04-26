# if/elseif/else with optional 'then'.
set x 0
if {$x > 0} then {
    puts positive
} elseif {$x < 0} {
    puts negative
} else {
    puts zero
}

set x 5
if {$x > 0} {
    puts positive
} elseif {$x < 0} {
    puts negative
} else {
    puts zero
}
