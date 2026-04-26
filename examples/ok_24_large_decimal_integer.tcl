# Large decimal integer boundary inside signed 64-bit range.
set x 9223372036854775800
incr x 7
puts $x
