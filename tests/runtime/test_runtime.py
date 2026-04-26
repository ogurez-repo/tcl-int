from tests.tcl import Tcl


def test_set_read_write_and_puts(tcl: Tcl):
    tcl.command("set x 10")
    tcl.command("puts [set x]")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "10\n"
    assert result.stderr == ""


def test_semicolon_command_separator(tcl: Tcl):
    tcl.command("set x 1; set y 2; puts [expr {$x + $y}]")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "3\n"


def test_quoted_and_braced_words(tcl: Tcl):
    tcl.command("set name Alice")
    tcl.command('puts "hello, $name"')
    tcl.command("puts {$name}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "hello, Alice\n$name\n"


def test_command_substitution_returns_last_command_result(tcl: Tcl):
    tcl.command("puts [expr {2 + 3}]")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "5\n"


def test_command_substitution_result_is_isolated(tcl: Tcl):
    tcl.command("set a [expr {1 + 1}]")
    tcl.command("set b [puts hi]")
    tcl.command('puts "$a,$b"')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "hi\n2,\n"


def test_unset_existing_variable(tcl: Tcl):
    tcl.command("set x 10")
    tcl.command("unset x")
    tcl.command("set x")

    result = tcl.run()

    assert result.returncode != 0
    assert "Undefined variable" in result.stderr


def test_unset_missing_variable_is_error(tcl: Tcl):
    tcl.command("unset x")

    result = tcl.run()

    assert result.returncode != 0
    assert "Undefined variable" in result.stderr


def test_incr_default_and_custom_step(tcl: Tcl):
    tcl.command("set x 10")
    tcl.command("incr x")
    tcl.command("incr x 5")
    tcl.command("puts $x")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "16\n"


def test_incr_requires_existing_integer_variable(tcl: Tcl):
    tcl.command("incr x")

    result = tcl.run()

    assert result.returncode != 0
    assert "Undefined variable" in result.stderr


def test_incr_rejects_non_integer_value(tcl: Tcl):
    tcl.command("set x abc")
    tcl.command("incr x")

    result = tcl.run()

    assert result.returncode != 0
    assert "Expected integer" in result.stderr


def test_puts_nonewline_and_channels(tcl: Tcl):
    tcl.command('puts -nonewline "a"')
    tcl.command('puts stdout "b"')
    tcl.command('puts stderr "err"')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "ab\n"
    assert result.stderr == "err\n"


def test_gets_stdin_returns_line(tcl: Tcl):
    tcl.command("puts [gets stdin]")

    result = tcl.run(stdin_data="hello\n")

    assert result.returncode == 0
    assert result.stdout == "hello\n"


def test_gets_stdin_with_variable_returns_count(tcl: Tcl):
    tcl.command("set n [gets stdin line]")
    tcl.command("puts $n")
    tcl.command("puts $line")

    result = tcl.run(stdin_data="hello\n")

    assert result.returncode == 0
    assert result.stdout == "5\nhello\n"


def test_if_elseif_else_then_noise_word(tcl: Tcl):
    tcl.command("set x 0")
    tcl.command('if {$x > 0} then {puts pos} elseif {$x < 0} {puts neg} else {puts zero}')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "zero\n"


def test_while_with_break_and_continue(tcl: Tcl):
    tcl.command("set i 0")
    tcl.command(
        "while {$i < 10} {"
        "incr i;"
        "if {$i == 3} {continue};"
        "if {$i == 7} {break};"
        "puts $i"
        "}"
    )

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "1\n2\n4\n5\n6\n"


def test_for_loop_runs_start_test_next_body(tcl: Tcl):
    tcl.command("for {set i 0} {$i < 5} {incr i} {puts $i}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "0\n1\n2\n3\n4\n"


def test_break_outside_loop_is_error(tcl: Tcl):
    tcl.command("break")

    result = tcl.run()

    assert result.returncode != 0
    assert "break outside loop" in result.stderr


def test_continue_outside_loop_is_error(tcl: Tcl):
    tcl.command("continue")

    result = tcl.run()

    assert result.returncode != 0
    assert "continue outside loop" in result.stderr


def test_proc_fixed_arity_and_return(tcl: Tcl):
    tcl.command("proc add {a b} {return [expr {$a + $b}]}")
    tcl.command("puts [add 2 3]")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "5\n"


def test_proc_wrong_arity_is_error(tcl: Tcl):
    tcl.command("proc add {a b} {return [expr {$a + $b}]}")
    tcl.command("add 1")

    result = tcl.run()

    assert result.returncode != 0
    assert "expects 2 arguments" in result.stderr


def test_return_unwinds_and_skips_following_commands(tcl: Tcl):
    tcl.command("proc f {} {return 1; puts never}")
    tcl.command("puts [f]")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "1\n"


def test_return_outside_proc_is_error(tcl: Tcl):
    tcl.command("return 1")

    result = tcl.run()

    assert result.returncode != 0
    assert "return outside procedure" in result.stderr


def test_proc_has_local_scope(tcl: Tcl):
    tcl.command("set x 10")
    tcl.command("proc f {} {set x 99; return $x}")
    tcl.command("puts [f]")
    tcl.command("puts $x")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "99\n10\n"


def test_break_inside_proc_does_not_break_caller_loop(tcl: Tcl):
    tcl.command("proc g {} {break}")
    tcl.command("while {1} {g}")

    result = tcl.run()

    assert result.returncode != 0
    assert "break outside loop" in result.stderr


def test_continue_inside_proc_does_not_continue_caller_loop(tcl: Tcl):
    tcl.command("proc g {} {continue}")
    tcl.command("while {1} {g}")

    result = tcl.run()

    assert result.returncode != 0
    assert "continue outside loop" in result.stderr


def test_recursion_factorial(tcl: Tcl):
    tcl.command("proc factorial {n} {if {$n <= 1} {return 1} else {return [expr {$n * [factorial [expr {$n - 1}]]}]}}")
    tcl.command("puts [factorial 5]")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "120\n"


def test_recursion_fibonacci_no_segfault(tcl: Tcl):
    tcl.command("proc fib {n} {if {$n < 2} {return $n} else {return [expr {[fib [expr {$n - 1}]] + [fib [expr {$n - 2}]]}]}}")
    tcl.command("puts [fib 7]")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "13\n"


def test_unsupported_commands_are_rejected(tcl: Tcl):
    tcl.command("switch 1 {1 {puts one}}")

    result = tcl.run()

    assert result.returncode != 0
    assert "Unknown command" in result.stderr or "unknown command" in result.stderr


def test_expr_supports_allowed_integer_operators(tcl: Tcl):
    tcl.command("puts [expr {(1 + 2) * 3}]")
    tcl.command("puts [expr {7 / 2}]")
    tcl.command("puts [expr {7 % 3}]")
    tcl.command("puts [expr {5 > 2 && 1}]")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stdout == "9\n3\n1\n1\n"


def test_expr_rejects_float_and_prefixed_numeric_literals(tcl: Tcl):
    tcl.command("expr {3.14}")

    result = tcl.run()

    assert result.returncode != 0

    tcl2 = Tcl(tcl.tcl_path)
    tcl2.command("expr {0x10}")
    result2 = tcl2.run()

    assert result2.returncode != 0


def test_expr_rejects_unsupported_operators(tcl: Tcl):
    tcl.command("expr {2 ** 3}")

    result = tcl.run()

    assert result.returncode != 0


def test_expr_division_by_zero_is_runtime_error(tcl: Tcl):
    tcl.command("expr {1 / 0}")

    result = tcl.run()

    assert result.returncode != 0
    assert "Division by zero" in result.stderr


def test_variable_namespace_and_array_forms_are_not_supported(tcl: Tcl):
    tcl.command("set a(x) 1")

    result = tcl.run()

    assert result.returncode != 0
    assert "array/namespace variables are not supported" in result.stderr


def test_nested_command_substitution_preserves_absolute_error_position(tcl: Tcl):
    tcl.command("puts before")
    tcl.command("puts [expr {[missing_cmd]}]")

    result = tcl.run()

    assert result.returncode != 0
    assert "2:" in result.stderr


def test_script_from_stdin_mode_is_kept_for_compatibility(tcl: Tcl):
    tcl.command("puts ok")

    result = tcl.run(script_from_stdin=True)

    assert result.returncode == 0
    assert result.stdout == "ok\n"
