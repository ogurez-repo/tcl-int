from .tcl import Tcl


def test_set_put(tcl: Tcl):
    tcl.set("foo", "bar")
    tcl.put("$foo")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "bar"


def test_put(tcl: Tcl):
    tcl.put("Hello")
    result = tcl.run()
    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "Hello"


def test_put_quoted_with_spaces(tcl: Tcl):
    tcl.put('"hello world"')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "hello world"


def test_semicolon_separates_commands(tcl: Tcl):
    tcl.command("set a 1; put $a")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "1"


def test_comment_at_command_start_is_ignored(tcl: Tcl):
    tcl.command("set a 1")
    tcl.command("# comment")
    tcl.command("put $a")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "1"


def test_hash_in_middle_of_word_is_not_comment(tcl: Tcl):
    tcl.put("a#b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "a#b"


def test_braced_word_keeps_spaces(tcl: Tcl):
    tcl.put("{hello world}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "hello world"


def test_braced_word_does_not_interpolate_variable(tcl: Tcl):
    tcl.set("a", "1")
    tcl.put("{$a}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "$a"


def test_nested_braced_word_is_literal(tcl: Tcl):
    tcl.put("{{a b}}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "{a b}"


def test_multiline_braced_word_is_literal(tcl: Tcl):
    tcl.command("put {hello\nworld}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "hello\nworld"


def test_if_syntax_is_accepted_without_execution(tcl: Tcl):
    tcl.command("if {$x > 0} {put pos} elseif {$x < 0} then {put neg} else {put zero}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_if_missing_body_is_syntax_error(tcl: Tcl):
    tcl.command("if {$x > 0}")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: if expects condition and body\n"


def test_if_else_without_body_is_syntax_error(tcl: Tcl):
    tcl.command("if {$x > 0} {put pos} else")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "if missing else body" in result.stderr


def test_while_syntax_is_accepted_without_execution(tcl: Tcl):
    tcl.command("while {$x < 10} {put $x}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_while_argument_error_is_syntax_error(tcl: Tcl):
    tcl.command("while {$x < 10}")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: while expects exactly 2 arguments\n"


def test_for_syntax_is_accepted_without_execution(tcl: Tcl):
    tcl.command("for {set i 0} {$i < 10} {set i [expr {$i + 1}]} {put $i}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_for_argument_error_is_syntax_error(tcl: Tcl):
    tcl.command("for {set i 0} {$i < 10} {set i 1}")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: for expects exactly 4 arguments\n"


def test_proc_syntax_is_accepted_without_execution(tcl: Tcl):
    tcl.command("proc inc {x} {set y 1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_proc_argument_error_is_syntax_error(tcl: Tcl):
    tcl.command("proc inc {x}")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: proc expects exactly 3 arguments\n"


def test_quoted_interpolated_set_command(tcl: Tcl):
    tcl.set("ab", "set")
    tcl.command('"$ab" a a')
    tcl.put("$a")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "a"


def test_quoted_prefixed_interpolated_set_command(tcl: Tcl):
    tcl.set("a", "et")
    tcl.command('"s$a" bbbb b')
    tcl.put("$bbbb")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "b"


def test_set_overwrites_existing_variable(tcl: Tcl):
    tcl.set("a", "1")
    tcl.set("a", "2")
    tcl.put("$a")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "2"


def test_missing_variable_is_semantic_error(tcl: Tcl):
    tcl.put("$missing")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "variable '$missing' not found" in result.stderr


def test_set_argument_error_is_syntax_error(tcl: Tcl):
    tcl.command("set a")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: set expects exactly 2 arguments\n"


def test_put_argument_error_is_syntax_error(tcl: Tcl):
    tcl.command("put a b")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: put expects exactly 1 argument\n"


def test_invalid_quoted_token_is_lexical_error(tcl: Tcl):
    tcl.command('put "abc')

    result = tcl.run()

    assert result.returncode != 0
    assert "lexical error" in result.stderr
    assert "unterminated quoted string" in result.stderr


def test_invalid_braced_token_is_lexical_error(tcl: Tcl):
    tcl.command("put {abc")

    result = tcl.run()

    assert result.returncode != 0
    assert "lexical error" in result.stderr
    assert "unterminated braced string" in result.stderr


def test_unknown_command_is_semantic_error(tcl: Tcl):
    tcl.command("unknown 1")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "semantic error at 1:1: unknown command 'unknown'\n"


def test_error_reports_absolute_input_line(tcl: Tcl):
    tcl.command("set a 1")
    tcl.command("set b 2")
    tcl.command("put $missing")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "semantic error at 3:5: variable '$missing' not found\n"
