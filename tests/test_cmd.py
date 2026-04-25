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


def test_if_implicit_else_is_accepted(tcl: Tcl):
    tcl.command("if {1} {put a} {put b}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_proc_recursive_with_implicit_else_is_accepted(tcl: Tcl):
    tcl.command("proc fact {n} {if {$n <= 1} {return 1} {return [expr {$n * [fact [expr {$n - 1}]]}]}}")
    tcl.command("fact 5")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""


def test_while_syntax_is_accepted_without_execution(tcl: Tcl):
    tcl.command("while {$x < 10} {put $x}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_while_with_unary_bitwise_not_expr_is_accepted(tcl: Tcl):
    tcl.command("while {~1} {put x}")

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


def test_quoted_literal_command_name_is_supported(tcl: Tcl):
    tcl.command('"puts" hello')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "hello"


def test_quoted_prefixed_interpolated_set_command(tcl: Tcl):
    tcl.set("a", "et")
    tcl.command('"s$a" bbbb b')
    tcl.put("$bbbb")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "b"


def test_quoted_backslash_escape_sequences_are_interpreted(tcl: Tcl):
    tcl.command('puts "line1\\nline2\\tend"')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == "line1\nline2\tend\n"


def test_quoted_octal_hex_unicode_escape_sequences_are_interpreted(tcl: Tcl):
    tcl.command('puts "\\101\\x42\\u0043\\U00000044"')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == "ABCD\n"


def test_quoted_incomplete_hex_unicode_escapes_fall_back_to_literal(tcl: Tcl):
    tcl.command('puts "\\x\\u\\U"')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == "xuU\n"


def test_escaped_command_substitution_in_quotes_is_literal(tcl: Tcl):
    tcl.command('puts "\\[expr {1 + 2}\\]"')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "[expr {1 + 2}]"


def test_escaped_unterminated_bracket_in_quotes_is_not_command_substitution(tcl: Tcl):
    tcl.command('puts "\\["')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "["


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


def test_set_read_variable_is_valid(tcl: Tcl):
    tcl.command("set a")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""


def test_set_argument_error_is_syntax_error(tcl: Tcl):
    tcl.command("set a 1 2")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: set expects 1 or 2 arguments\n"


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


def test_dynamic_unknown_command_is_semantic_error(tcl: Tcl):
    tcl.command("set cmd unknown")
    tcl.command("$cmd 1")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "semantic error at 2:1: unknown command 'unknown'\n"


def test_error_reports_absolute_input_line(tcl: Tcl):
    tcl.command("set a 1")
    tcl.command("set b 2")
    tcl.command("put $missing")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "semantic error at 3:5: variable '$missing' not found\n"


def test_nested_if_body_is_validated(tcl: Tcl):
    tcl.command("if {1} {unknown 1}")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown command 'unknown'" in result.stderr


def test_nested_if_body_argument_error_is_validated(tcl: Tcl):
    tcl.command("if {1} {set a 1 2}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "set expects 1 or 2 arguments" in result.stderr


def test_proc_body_is_validated(tcl: Tcl):
    tcl.command("proc f {x} {set y 1 2}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "set expects 1 or 2 arguments" in result.stderr


def test_nested_control_flow_is_valid(tcl: Tcl):
    tcl.command("if {$x > 0} {while {$x < 10} {put $x}}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_proc_call_with_correct_arity_is_valid(tcl: Tcl):
    tcl.command("proc inc {x} {return [expr {$x + 1}]}")
    tcl.command("inc 1")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""


def test_proc_call_with_wrong_arity_is_error(tcl: Tcl):
    tcl.command("proc inc {x} {return [expr {$x + 1}]}")
    tcl.command("inc")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "procedure expects more arguments" in result.stderr


def test_return_with_options_is_valid(tcl: Tcl):
    tcl.command("proc f {} {return -code error \"oops\"}")
    tcl.command("f")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""


def test_return_too_many_args_is_error(tcl: Tcl):
    tcl.command("return a b")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: return expects ?-option value ...? ?result?\n"


def test_catch_is_validation_only(tcl: Tcl):
    tcl.command("catch {set a [expr {1/0}]} err")
    tcl.command("puts done")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "done"


def test_command_substitution_is_validated(tcl: Tcl):
    tcl.command("for {set i 0} {$i < 10} {set i [expr {$i + 1}]} {put $i}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""


def test_unbalanced_command_substitution_is_error(tcl: Tcl):
    tcl.command("set a [expr {1 + 2}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "unterminated command substitution" in result.stderr


def test_expr_multiple_args_is_accepted(tcl: Tcl):
    tcl.command("expr 1 + 2")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""


def test_expr_no_args_is_error(tcl: Tcl):
    tcl.command("expr")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: expr expects at least 1 argument\n"


def test_invalid_expr_is_error(tcl: Tcl):
    tcl.command("expr {$x + }")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "invalid expression" in result.stderr


def test_invalid_expr_bareword_is_error(tcl: Tcl):
    tcl.command("expr {abc}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "invalid bareword in expression" in result.stderr


def test_if_expr_allows_namespace_variable_reference(tcl: Tcl):
    tcl.command("if {$::x > 0} {puts ok}")

    result = tcl.run(args=["--check"])

    assert result.returncode == 0
    assert result.stderr == ""


def test_if_expr_allows_empty_array_name_variable_reference(tcl: Tcl):
    tcl.command("if {$(k) > 0} {puts ok}")

    result = tcl.run(args=["--check"])

    assert result.returncode == 0
    assert result.stderr == ""


def test_expr_accepts_scientific_and_prefixed_numbers(tcl: Tcl):
    tcl.command("expr {1e3 > 0}")
    tcl.command("expr {1.2e-3 > 0}")
    tcl.command("expr {0x10 == 16}")
    tcl.command("expr {0b1010 == 10}")
    tcl.command("expr {0o17 == 15}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""


def test_invalid_scientific_literal_is_error(tcl: Tcl):
    tcl.command("expr {1e + 1}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "invalid number" in result.stderr


def test_invalid_prefixed_literal_without_digits_is_error(tcl: Tcl):
    tcl.command("expr {0x + 1}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "invalid number" in result.stderr


def test_invalid_legacy_octal_literal_is_error(tcl: Tcl):
    tcl.command("if {08 > 0} {puts ok}")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "invalid number" in result.stderr


def test_legacy_octal_literal_is_accepted(tcl: Tcl):
    tcl.command("if {017 > 0} {puts ok}")

    result = tcl.run(args=["--check"])

    assert result.returncode == 0
    assert result.stderr == ""


def test_leading_comment_before_command_is_accepted(tcl: Tcl):
    tcl.command("# comment")
    tcl.command("set a 1")
    tcl.command("puts $a")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "1"


def test_leading_blank_line_before_command_is_accepted(tcl: Tcl):
    tcl.command("")
    tcl.command("set a 1")
    tcl.command("puts $a")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "1"


def test_top_level_command_substitution_is_structurally_accepted(tcl: Tcl):
    tcl.command("set a [expr {1 + 2}]")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""


def test_embedded_top_level_command_substitution_is_structurally_accepted(tcl: Tcl):
    tcl.command("set a x[expr {1}]y")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""


def test_backslash_newline_continuation_replaces_with_space(tcl: Tcl):
    tcl.command("set a hello\\")
    tcl.command("world")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: set expects 1 or 2 arguments\n"


def test_backslash_newline_continuation_inside_quotes_keeps_single_word(tcl: Tcl):
    tcl.command('set a "hello\\')
    tcl.command('world"')
    tcl.command("puts $a")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "hello world"


def test_extended_expr_operators_are_accepted(tcl: Tcl):
    tcl.command("expr {2 ** 3}")
    tcl.command("expr {1 & 3}")
    tcl.command("expr {1 << 2}")
    tcl.command("expr {~1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""


def test_array_variable_reference_is_supported(tcl: Tcl):
    tcl.command("set a(x) 1")
    tcl.command("puts $a(x)")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "1"


def test_array_variable_reference_with_substituted_index_is_supported(tcl: Tcl):
    tcl.command("set b x")
    tcl.command("set a(x) 42")
    tcl.command("puts $a($b)")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "42"


def test_braced_variable_reference_keeps_literal_name(tcl: Tcl):
    tcl.command("set b x")
    tcl.command("set {a($b)} 9")
    tcl.command("puts ${a($b)}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "9"


def test_single_colon_is_not_treated_as_namespace_separator_in_variable_name(tcl: Tcl):
    tcl.command("set a 1")
    tcl.command('puts "$a:b"')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "1:b"


def test_namespace_separator_variable_reference_is_supported(tcl: Tcl):
    tcl.command("set ::ns::value 5")
    tcl.command("puts $::ns::value")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "5"


def test_empty_array_name_variable_reference_is_supported(tcl: Tcl):
    tcl.command("set (k) 9")
    tcl.command("puts $(k)")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "9"


def test_argument_expansion_from_variable_list_is_supported(tcl: Tcl):
    tcl.command("set pair {name value}")
    tcl.command("set {*}$pair")
    tcl.command("puts $name")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "value"


def test_argument_expansion_from_braced_list_is_supported(tcl: Tcl):
    tcl.command("set {*}{x 7}")
    tcl.command("puts $x")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "7"


def test_argument_expansion_applies_backslash_substitution_in_list_parser(tcl: Tcl):
    tcl.command("set pair {{x} \\x37}")
    tcl.command("set {*}$pair")
    tcl.command("puts $x")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout.strip() == "7"


def test_argument_expansion_with_malformed_list_is_error(tcl: Tcl):
    tcl.command('set pair "a {b"')
    tcl.command("set {*}$pair")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "malformed list for argument expansion" in result.stderr


def test_puts_nonewline(tcl: Tcl):
    tcl.command("puts -nonewline hello")
    tcl.command("puts world")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == "helloworld\n"


def test_foreach_is_validation_only(tcl: Tcl):
    tcl.command("foreach x {a b} {puts $x}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_foreach_quoted_varlist_with_multiple_names_is_valid(tcl: Tcl):
    tcl.command('foreach "x y" {a b} {puts $x}')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_foreach_empty_varlist_is_error(tcl: Tcl):
    tcl.command("foreach {} {a b} {puts x}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "foreach varlist is empty" in result.stderr


def test_foreach_malformed_literal_varlist_is_error(tcl: Tcl):
    tcl.command('foreach "x {y" {a b} {puts x}')

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "invalid foreach variable list" in result.stderr


def test_switch_braced_cases_are_validated(tcl: Tcl):
    tcl.command("switch -- $x {a {puts a} default {puts d}}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_switch_braced_cases_accept_quoted_pattern_with_spaces(tcl: Tcl):
    tcl.command('switch -- "a a" {"a a" {puts yes} default {puts no}}')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_switch_glob_option_is_validated(tcl: Tcl):
    tcl.command("switch -glob abc {a* {puts yes} default {puts no}}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_switch_regexp_matchvar_indexvar_options_are_validated(tcl: Tcl):
    tcl.command("switch -regexp -matchvar m -indexvar i -- abc {a(b)c {puts yes} default {puts no}}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_switch_unknown_option_is_error(tcl: Tcl):
    tcl.command("switch -bad abc {a {puts yes} default {puts no}}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "unknown switch option" in result.stderr


def test_switch_matchvar_requires_regexp_option(tcl: Tcl):
    tcl.command("switch -matchvar m -- abc {abc {puts yes}}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "-matchvar option requires -regexp option" in result.stderr


def test_switch_indexvar_requires_regexp_option(tcl: Tcl):
    tcl.command("switch -indexvar i -- abc {abc {puts yes}}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "-indexvar option requires -regexp option" in result.stderr


def test_switch_matchvar_before_regexp_is_valid(tcl: Tcl):
    tcl.command("switch -matchvar m -regexp -- abc {abc {puts yes}}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_switch_options_without_string_and_cases_is_error(tcl: Tcl):
    tcl.command("switch -regexp -matchvar m")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "switch expects string and cases" in result.stderr


def test_incr_is_validation_only_in_for_next(tcl: Tcl):
    tcl.command("for {set i 0} {$i < 0} {incr i} {puts $i}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_unterminated_top_level_command_substitution_is_error(tcl: Tcl):
    tcl.command("set a [expr {1 + 2}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "unterminated command substitution" in result.stderr


def test_malformed_extended_expr_operator_is_error(tcl: Tcl):
    tcl.command("expr {1 << }")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "invalid expression" in result.stderr


def test_malformed_foreach_is_error(tcl: Tcl):
    tcl.command("foreach x {a b}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "foreach expects var/list pairs and body" in result.stderr


def test_malformed_switch_case_list_is_error(tcl: Tcl):
    tcl.command("switch -- $x {a {puts a} default}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "switch expects pattern/body pairs" in result.stderr


def test_switch_trailing_dash_body_in_braced_form_is_error(tcl: Tcl):
    tcl.command("switch -- a {a -}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "switch pattern has no body" in result.stderr


def test_switch_trailing_dash_body_in_pairs_form_is_error(tcl: Tcl):
    tcl.command("switch -- a a -")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "switch pattern has no body" in result.stderr


def test_invalid_puts_arity_is_error(tcl: Tcl):
    tcl.command("puts a b")

    result = tcl.run()

    assert result.returncode != 0
    assert result.stderr == "syntax error at 1:1: puts expects string or -nonewline string\n"
