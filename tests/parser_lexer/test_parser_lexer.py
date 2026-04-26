from tests.tcl import Tcl


def test_check_mode_accepts_valid_subset_program(tcl: Tcl):
    tcl.command("set x 10")
    tcl.command("if {$x > 0} {puts ok} else {puts no}")
    tcl.command("proc inc {n} {return [expr {$n + 1}]}")

    result = tcl.run(args=["--check"])

    assert result.returncode == 0
    assert result.stdout == ""
    assert result.stderr == ""


def test_check_mode_accepts_semicolon_and_newline_separators(tcl: Tcl):
    tcl.command("set x 1; set y 2")
    tcl.command("puts [expr {$x + $y}]")

    result = tcl.run(args=["--check"])

    assert result.returncode == 0


def test_check_mode_ignores_command_start_comments(tcl: Tcl):
    tcl.command("# comment")
    tcl.command("set x 1")

    result = tcl.run(args=["--check"])

    assert result.returncode == 0


def test_check_mode_reports_unterminated_quoted_string(tcl: Tcl):
    tcl.command('puts "abc')

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "lexical error" in result.stderr
    assert "unterminated quoted string" in result.stderr


def test_check_mode_reports_unterminated_braced_string(tcl: Tcl):
    tcl.command("puts {abc")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "lexical error" in result.stderr
    assert "unterminated braced string" in result.stderr


def test_check_mode_reports_unterminated_command_substitution(tcl: Tcl):
    tcl.command("puts [expr {1 + 2}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "unterminated command substitution" in result.stderr


def test_check_mode_rejects_argument_expansion(tcl: Tcl):
    tcl.command("set {*}$pair")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0


def test_check_mode_reports_unknown_command(tcl: Tcl):
    tcl.command("unknown 1")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "unknown command" in result.stderr


def test_check_mode_does_not_execute_script(tcl: Tcl):
    tcl.command("puts hello")

    result = tcl.run(args=["--check"])

    assert result.returncode == 0
    assert result.stdout == ""


def test_check_mode_if_missing_body_is_error(tcl: Tcl):
    tcl.command("if {$x > 0}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "if expects condition and body" in result.stderr or "if missing body" in result.stderr


def test_check_mode_if_rejects_implicit_else_form(tcl: Tcl):
    tcl.command("if {1} {puts a} {puts b}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "expected elseif or else" in result.stderr


def test_check_mode_while_argument_count(tcl: Tcl):
    tcl.command("while {$x < 10}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "while expects exactly 2 arguments" in result.stderr


def test_check_mode_for_argument_count(tcl: Tcl):
    tcl.command("for {set i 0} {$i < 10} {incr i}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "for expects exactly 4 arguments" in result.stderr


def test_check_mode_proc_rejects_default_arguments(tcl: Tcl):
    tcl.command("proc f {a {b 1}} {return $a}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "invalid proc argument list" in result.stderr


def test_check_mode_proc_rejects_args_parameter(tcl: Tcl):
    tcl.command("proc f {args} {return 1}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "invalid proc argument list" in result.stderr


def test_check_mode_return_argument_count(tcl: Tcl):
    tcl.command("return a b")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "return expects" in result.stderr


def test_check_mode_expr_rejects_float_literal(tcl: Tcl):
    tcl.command("expr {3.14}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "expression" in result.stderr or "token" in result.stderr


def test_check_mode_expr_rejects_unsupported_operator(tcl: Tcl):
    tcl.command("expr {2 ** 3}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0


def test_check_mode_reports_absolute_position_inside_nested_substitution(tcl: Tcl):
    tcl.command("puts before")
    tcl.command("puts [expr {[badcmd]}]")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "2:" in result.stderr


def test_check_mode_rejects_unsupported_commands_from_spec(tcl: Tcl):
    tcl.command("switch 1 {1 {puts one}}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "unknown command" in result.stderr
