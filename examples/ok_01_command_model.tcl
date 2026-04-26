# Command model: separators and comments.
set a 2; set b 3
# This comment should be ignored.
puts [expr {$a + $b}]
puts done
