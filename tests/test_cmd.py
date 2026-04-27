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


def test_array_set_is_validation_only(tcl: Tcl):
    tcl.command("array set arr {a 1 b 2}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_array_get_is_validation_only(tcl: Tcl):
    tcl.command("array get arr")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_array_size_is_validation_only(tcl: Tcl):
    tcl.command("array size arr")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_array_exists_is_validation_only(tcl: Tcl):
    tcl.command("array exists arr")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_array_names_is_validation_only(tcl: Tcl):
    tcl.command("array names arr")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_unset_is_validation_only(tcl: Tcl):
    tcl.command("unset a")
    tcl.command("unset arr(x) arr(y)")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_array_no_args_is_error(tcl: Tcl):
    tcl.command("array")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "array expects a subcommand" in result.stderr


def test_array_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("array bad arr")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown array subcommand 'bad'" in result.stderr


def test_array_size_wrong_arity_is_error(tcl: Tcl):
    tcl.command("array size")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "array size expects arrayName" in result.stderr


def test_array_set_wrong_arity_is_error(tcl: Tcl):
    tcl.command("array set arr")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "array set expects arrayName list" in result.stderr


def test_unset_no_args_is_error(tcl: Tcl):
    tcl.command("unset")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "unset expects at least 1 argument" in result.stderr


def test_append_is_validation_only(tcl: Tcl):
    tcl.command("append x hello")
    tcl.command("append x hello world")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_append_no_args_is_error(tcl: Tcl):
    tcl.command("append")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "append expects varName ?value ...?" in result.stderr


def test_lappend_is_validation_only(tcl: Tcl):
    tcl.command("lappend lst a")
    tcl.command("lappend lst b c")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_lindex_is_validation_only(tcl: Tcl):
    tcl.command("lindex {a b c} 1")
    tcl.command("lindex {a b c} 1 2")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_lindex_too_few_args_is_error(tcl: Tcl):
    tcl.command("lindex {a b}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "lindex expects list ?index ...?" in result.stderr


def test_llength_is_validation_only(tcl: Tcl):
    tcl.command("llength {a b c}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_llength_wrong_arity_is_error(tcl: Tcl):
    tcl.command("llength")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "llength expects list" in result.stderr


def test_concat_is_validation_only(tcl: Tcl):
    tcl.command("concat {a b} {c d}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_lrange_is_validation_only(tcl: Tcl):
    tcl.command("lrange {a b c d} 1 2")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_lrange_wrong_arity_is_error(tcl: Tcl):
    tcl.command("lrange {a b} 1")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "lrange expects list first last" in result.stderr


def test_linsert_is_validation_only(tcl: Tcl):
    tcl.command("linsert {a b} 1 x y")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_linsert_too_few_args_is_error(tcl: Tcl):
    tcl.command("linsert {a b} 1")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "linsert expects list index element ?element ...?" in result.stderr


def test_lreplace_is_validation_only(tcl: Tcl):
    tcl.command("lreplace {a b c} 1 2 x")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_lreplace_too_few_args_is_error(tcl: Tcl):
    tcl.command("lreplace {a b} 1")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "lreplace expects list first last ?element ...?" in result.stderr


def test_split_is_validation_only(tcl: Tcl):
    tcl.command('split {a,b,c} ","')

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_split_wrong_arity_is_error(tcl: Tcl):
    tcl.command("split")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "split expects string/list ?chars/joinString?" in result.stderr


def test_join_is_validation_only(tcl: Tcl):
    tcl.command("join {a b c} -")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_format_is_validation_only(tcl: Tcl):
    tcl.command("format %s hello")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_format_no_args_is_error(tcl: Tcl):
    tcl.command("format")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "format expects formatString ?arg ...?" in result.stderr


def test_scan_is_validation_only(tcl: Tcl):
    tcl.command("scan 123 %d num")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_scan_too_few_args_is_error(tcl: Tcl):
    tcl.command("scan 123")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "scan expects string format ?varName ...?" in result.stderr


def test_error_is_validation_only(tcl: Tcl):
    tcl.command("error oops")
    tcl.command("error oops info code")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_error_wrong_arity_is_error(tcl: Tcl):
    tcl.command("error")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "error expects message ?info? ?code?" in result.stderr


def test_eval_is_validation_only(tcl: Tcl):
    tcl.command("eval {set a 1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_eval_no_args_is_error(tcl: Tcl):
    tcl.command("eval")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "eval expects arg ?arg ...?" in result.stderr


def test_global_is_validation_only(tcl: Tcl):
    tcl.command("global x y")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_global_no_args_is_error(tcl: Tcl):
    tcl.command("global")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "global expects varName ?varName ...?" in result.stderr


def test_upvar_is_validation_only(tcl: Tcl):
    tcl.command("upvar 1 x y")
    tcl.command("upvar x y")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_upvar_odd_pairs_is_error(tcl: Tcl):
    tcl.command("upvar x")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "upvar expects ?level? otherVar myVar ?...?" in result.stderr


def test_regexp_is_validation_only(tcl: Tcl):
    tcl.command("regexp -nocase {a.*b} abc")
    tcl.command("regexp -start 2 {a.*b} abc m1 m2")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_regexp_too_few_args_is_error(tcl: Tcl):
    tcl.command("regexp")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "regexp expects expression and string" in result.stderr


def test_regsub_is_validation_only(tcl: Tcl):
    tcl.command("regsub -all a b c")
    tcl.command("regsub -start 1 a b c var")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_regsub_too_few_args_is_error(tcl: Tcl):
    tcl.command("regsub a b")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "regsub expects expression string subSpec ?varName?" in result.stderr


def test_string_length_is_validation_only(tcl: Tcl):
    tcl.command("string length hello")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_string_index_is_validation_only(tcl: Tcl):
    tcl.command("string index hello 1")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_string_range_is_validation_only(tcl: Tcl):
    tcl.command("string range hello 1 3")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_string_match_is_validation_only(tcl: Tcl):
    tcl.command("string match a* abc")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_string_compare_is_validation_only(tcl: Tcl):
    tcl.command("string compare a b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_string_equal_is_validation_only(tcl: Tcl):
    tcl.command("string equal a b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_string_first_is_validation_only(tcl: Tcl):
    tcl.command("string first a abc")
    tcl.command("string first a abc 1")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_string_trim_is_validation_only(tcl: Tcl):
    tcl.command("string trim hello")
    tcl.command("string trimleft hello x")
    tcl.command("string trimright hello x")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_string_replace_is_validation_only(tcl: Tcl):
    tcl.command("string replace hello 1 3")
    tcl.command("string replace hello 1 3 xx")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_string_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("string reverse hello")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown string subcommand 'reverse'" in result.stderr


def test_string_no_args_is_error(tcl: Tcl):
    tcl.command("string")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "string expects a subcommand" in result.stderr


def test_string_index_wrong_arity_is_error(tcl: Tcl):
    tcl.command("string index hello")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "string index expects string charIndex" in result.stderr


def test_info_exists_is_validation_only(tcl: Tcl):
    tcl.command("info exists x")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_info_args_is_validation_only(tcl: Tcl):
    tcl.command("info args foo")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_info_body_is_validation_only(tcl: Tcl):
    tcl.command("info body foo")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_info_commands_is_validation_only(tcl: Tcl):
    tcl.command("info commands")
    tcl.command("info commands f*")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_info_procs_is_validation_only(tcl: Tcl):
    tcl.command("info procs")
    tcl.command("info procs f*")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_info_vars_is_validation_only(tcl: Tcl):
    tcl.command("info vars")
    tcl.command("info vars x*")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_info_level_is_validation_only(tcl: Tcl):
    tcl.command("info level")
    tcl.command("info level 1")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_info_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("info bad")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown info subcommand 'bad'" in result.stderr


def test_info_no_args_is_error(tcl: Tcl):
    tcl.command("info")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "info expects a subcommand" in result.stderr


def test_uplevel_is_validation_only(tcl: Tcl):
    tcl.command("uplevel {set a 1}")
    tcl.command("uplevel 1 {set a 1}")
    tcl.command("uplevel #0 {set a 1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_uplevel_no_args_is_error(tcl: Tcl):
    tcl.command("uplevel")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "uplevel expects ?level? arg ?arg ...?" in result.stderr


def test_dict_create_is_validation_only(tcl: Tcl):
    tcl.command("dict create")
    tcl.command("dict create a 1 b 2")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_create_odd_pairs_is_error(tcl: Tcl):
    tcl.command("dict create a 1 b")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "dict create expects ?key value ...?" in result.stderr


def test_dict_get_is_validation_only(tcl: Tcl):
    tcl.command("dict get {a 1 b 2} a")
    tcl.command("dict get {a 1 b 2} a b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_get_too_few_args_is_error(tcl: Tcl):
    tcl.command("dict get")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "dict get expects dict ?key ...?" in result.stderr


def test_dict_set_is_validation_only(tcl: Tcl):
    tcl.command("dict set d k v")
    tcl.command("dict set d k1 k2 v")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_set_too_few_args_is_error(tcl: Tcl):
    tcl.command("dict set d k")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "dict set expects dictName key ?key ...? value" in result.stderr


def test_dict_unset_is_validation_only(tcl: Tcl):
    tcl.command("dict unset d k")
    tcl.command("dict unset d k1 k2")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_unset_too_few_args_is_error(tcl: Tcl):
    tcl.command("dict unset d")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "dict unset expects dictName key ?key ...?" in result.stderr


def test_dict_exists_is_validation_only(tcl: Tcl):
    tcl.command("dict exists {a 1} a")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_size_is_validation_only(tcl: Tcl):
    tcl.command("dict size {a 1 b 2}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_size_wrong_arity_is_error(tcl: Tcl):
    tcl.command("dict size")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "dict size expects dict" in result.stderr


def test_dict_keys_is_validation_only(tcl: Tcl):
    tcl.command("dict keys {a 1 b 2}")
    tcl.command("dict keys {a 1 b 2} a*")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_values_is_validation_only(tcl: Tcl):
    tcl.command("dict values {a 1 b 2}")
    tcl.command("dict values {a 1 b 2} a*")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_append_is_validation_only(tcl: Tcl):
    tcl.command("dict append d k v")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_lappend_is_validation_only(tcl: Tcl):
    tcl.command("dict lappend d k v")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_incr_is_validation_only(tcl: Tcl):
    tcl.command("dict incr d k")
    tcl.command("dict incr d k 1")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_incr_wrong_arity_is_error(tcl: Tcl):
    tcl.command("dict incr d")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "dict incr expects dictName key ?increment?" in result.stderr


def test_dict_remove_is_validation_only(tcl: Tcl):
    tcl.command("dict remove {a 1 b 2} a")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_replace_is_validation_only(tcl: Tcl):
    tcl.command("dict replace {a 1}")
    tcl.command("dict replace {a 1} b 2")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_replace_odd_pairs_is_error(tcl: Tcl):
    tcl.command("dict replace {a 1} b")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "dict replace expects dict ?key value ...?" in result.stderr


def test_dict_merge_is_validation_only(tcl: Tcl):
    tcl.command("dict merge")
    tcl.command("dict merge {a 1} {b 2}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_for_is_validation_only(tcl: Tcl):
    tcl.command("dict for {k v} {a 1} {puts $k}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_for_wrong_arity_is_error(tcl: Tcl):
    tcl.command("dict for {k v} {a 1}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "dict for expects {keyVar valueVar} dict body" in result.stderr


def test_dict_info_is_validation_only(tcl: Tcl):
    tcl.command("dict info {a 1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_dict_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("dict bad {a 1}")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown dict subcommand 'bad'" in result.stderr


def test_dict_no_args_is_error(tcl: Tcl):
    tcl.command("dict")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "dict expects a subcommand" in result.stderr


def test_namespace_eval_is_validation_only(tcl: Tcl):
    tcl.command("namespace eval ns {set a 1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_namespace_current_is_validation_only(tcl: Tcl):
    tcl.command("namespace current")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_namespace_code_is_validation_only(tcl: Tcl):
    tcl.command("namespace code {set a 1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_namespace_delete_is_validation_only(tcl: Tcl):
    tcl.command("namespace delete ns1 ns2")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_namespace_exists_is_validation_only(tcl: Tcl):
    tcl.command("namespace exists ns")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_namespace_export_is_validation_only(tcl: Tcl):
    tcl.command("namespace export -clear f*")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_namespace_import_is_validation_only(tcl: Tcl):
    tcl.command("namespace import -force ns::*")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_namespace_qualifiers_is_validation_only(tcl: Tcl):
    tcl.command("namespace qualifiers ::a::b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_namespace_tail_is_validation_only(tcl: Tcl):
    tcl.command("namespace tail ::a::b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_namespace_which_is_validation_only(tcl: Tcl):
    tcl.command("namespace which -command foo")
    tcl.command("namespace which -variable foo")
    tcl.command("namespace which foo")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_namespace_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("namespace bad")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown namespace subcommand 'bad'" in result.stderr


def test_namespace_no_args_is_error(tcl: Tcl):
    tcl.command("namespace")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "namespace expects a subcommand" in result.stderr


def test_namespace_eval_too_few_args_is_error(tcl: Tcl):
    tcl.command("namespace eval ns")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "namespace eval expects name arg ?arg...?" in result.stderr


def test_variable_is_validation_only(tcl: Tcl):
    tcl.command("variable x")
    tcl.command("variable x 1")
    tcl.command("variable x 1 y 2")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_variable_no_args_is_error(tcl: Tcl):
    tcl.command("variable")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "variable expects ?name value...? || ?name ...?" in result.stderr


def test_source_is_validation_only(tcl: Tcl):
    tcl.command("source foo.tcl")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_source_wrong_arity_is_error(tcl: Tcl):
    tcl.command("source")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "source expects fileName" in result.stderr


def test_package_require_is_validation_only(tcl: Tcl):
    tcl.command("package require Tcl")
    tcl.command("package require -exact Tcl 8.6")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_package_provide_is_validation_only(tcl: Tcl):
    tcl.command("package provide mypkg")
    tcl.command("package provide mypkg 1.0")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_package_vcompare_is_validation_only(tcl: Tcl):
    tcl.command("package vcompare 1.0 2.0")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_package_versions_is_validation_only(tcl: Tcl):
    tcl.command("package versions mypkg")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_package_forget_is_validation_only(tcl: Tcl):
    tcl.command("package forget mypkg")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_package_ifneeded_is_validation_only(tcl: Tcl):
    tcl.command("package ifneeded mypkg 1.0 {source mypkg.tcl}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_package_names_is_validation_only(tcl: Tcl):
    tcl.command("package names")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_package_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("package bad")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown package subcommand 'bad'" in result.stderr


def test_package_require_too_few_args_is_error(tcl: Tcl):
    tcl.command("package require")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "package require expects name ?version?" in result.stderr


def test_rename_is_validation_only(tcl: Tcl):
    tcl.command("rename old new")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_rename_wrong_arity_is_error(tcl: Tcl):
    tcl.command("rename old")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "rename expects oldName newName" in result.stderr


def test_exit_is_validation_only(tcl: Tcl):
    tcl.command("exit")
    tcl.command("exit 0")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_exit_wrong_arity_is_error(tcl: Tcl):
    tcl.command("exit 0 1")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "exit expects ?returnCode?" in result.stderr


def test_cd_is_validation_only(tcl: Tcl):
    tcl.command("cd")
    tcl.command("cd /tmp")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_cd_wrong_arity_is_error(tcl: Tcl):
    tcl.command("cd /tmp /home")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "cd expects ?dirName?" in result.stderr


def test_pwd_is_validation_only(tcl: Tcl):
    tcl.command("pwd")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_pwd_wrong_arity_is_error(tcl: Tcl):
    tcl.command("pwd /tmp")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "pwd expects no arguments" in result.stderr


def test_after_ms_is_validation_only(tcl: Tcl):
    tcl.command("after 1000")
    tcl.command("after 1000 {set a 1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_after_cancel_is_validation_only(tcl: Tcl):
    tcl.command("after cancel id1")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_after_idle_is_validation_only(tcl: Tcl):
    tcl.command("after idle {set a 1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_after_idle_no_script_is_error(tcl: Tcl):
    tcl.command("after idle")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "after idle expects script ?script...?" in result.stderr


def test_trace_add_is_validation_only(tcl: Tcl):
    tcl.command("trace add variable x w {set a 1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_trace_remove_is_validation_only(tcl: Tcl):
    tcl.command("trace remove variable x w cmd")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_trace_info_is_validation_only(tcl: Tcl):
    tcl.command("trace info variable x")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_trace_variable_is_validation_only(tcl: Tcl):
    tcl.command("trace variable x w {set a 1}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_trace_vdelete_is_validation_only(tcl: Tcl):
    tcl.command("trace vdelete x w cmd")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_trace_vinfo_is_validation_only(tcl: Tcl):
    tcl.command("trace vinfo x")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_trace_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("trace bad")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown trace subcommand 'bad'" in result.stderr


def test_clock_seconds_is_validation_only(tcl: Tcl):
    tcl.command("clock seconds")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_clock_clicks_is_validation_only(tcl: Tcl):
    tcl.command("clock clicks")
    tcl.command("clock clicks -milliseconds")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_clock_format_is_validation_only(tcl: Tcl):
    tcl.command("clock format 1234567890")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_clock_scan_is_validation_only(tcl: Tcl):
    tcl.command("clock scan {2024-01-01}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_clock_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("clock bad")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown clock subcommand 'bad'" in result.stderr


def test_vwait_is_validation_only(tcl: Tcl):
    tcl.command("vwait x")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_vwait_wrong_arity_is_error(tcl: Tcl):
    tcl.command("vwait")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "vwait expects name" in result.stderr


def test_update_is_validation_only(tcl: Tcl):
    tcl.command("update")
    tcl.command("update idletasks")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_update_wrong_arity_is_error(tcl: Tcl):
    tcl.command("update idletasks extra")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "update expects ?idletasks?" in result.stderr


def test_try_is_validation_only(tcl: Tcl):
    tcl.command("try {set a 1}")
    tcl.command("try {set a 1} finally {puts done}")
    tcl.command("try {set a 1} trap {ERR} {msg opts} {puts $msg} finally {puts done}")
    tcl.command("try {set a 1} on error {msg opts} {puts $msg}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_try_no_body_is_error(tcl: Tcl):
    tcl.command("try")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "try expects body ?handlers...? ?finally body?" in result.stderr


def test_try_finally_not_last_is_error(tcl: Tcl):
    tcl.command("try {set a 1} finally {puts done} trap {ERR} {msg opts} {puts $msg}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "try finally must be the last clause" in result.stderr


def test_try_trap_too_few_args_is_error(tcl: Tcl):
    tcl.command("try {set a 1} trap {ERR}")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "try trap/on expects pattern/code variableList body" in result.stderr


def test_throw_is_validation_only(tcl: Tcl):
    tcl.command("throw MYERR oops")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_throw_wrong_arity_is_error(tcl: Tcl):
    tcl.command("throw MYERR")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "throw expects type message" in result.stderr


def test_file_dirname_is_validation_only(tcl: Tcl):
    tcl.command("file dirname /a/b/c")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_tail_is_validation_only(tcl: Tcl):
    tcl.command("file tail /a/b/c")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_rootname_is_validation_only(tcl: Tcl):
    tcl.command("file rootname /a/b/c.txt")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_extension_is_validation_only(tcl: Tcl):
    tcl.command("file extension /a/b/c.txt")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_normalize_is_validation_only(tcl: Tcl):
    tcl.command("file normalize /a/b/../c")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_exists_is_validation_only(tcl: Tcl):
    tcl.command("file exists /a/b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_isdirectory_is_validation_only(tcl: Tcl):
    tcl.command("file isdirectory /a/b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_isfile_is_validation_only(tcl: Tcl):
    tcl.command("file isfile /a/b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_size_is_validation_only(tcl: Tcl):
    tcl.command("file size /a/b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_mtime_is_validation_only(tcl: Tcl):
    tcl.command("file mtime /a/b")
    tcl.command("file mtime /a/b 1234567890")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_join_is_validation_only(tcl: Tcl):
    tcl.command("file join a b c")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_mkdir_is_validation_only(tcl: Tcl):
    tcl.command("file mkdir /a/b /c/d")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_delete_is_validation_only(tcl: Tcl):
    tcl.command("file delete -force /a/b")
    tcl.command("file delete -- /a/b")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_copy_is_validation_only(tcl: Tcl):
    tcl.command("file copy -force /a/b /c/d")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_rename_is_validation_only(tcl: Tcl):
    tcl.command("file rename -force /a/b /c/d")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_file_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("file bad /a/b")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown file subcommand 'bad'" in result.stderr


def test_file_dirname_wrong_arity_is_error(tcl: Tcl):
    tcl.command("file dirname")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "file dirname expects name" in result.stderr


def test_file_copy_too_few_args_is_error(tcl: Tcl):
    tcl.command("file copy /a")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "file copy expects ?-force? source target" in result.stderr


def test_open_is_validation_only(tcl: Tcl):
    tcl.command("open file.txt")
    tcl.command("open file.txt r")
    tcl.command("open file.txt w 0644")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_open_wrong_arity_is_error(tcl: Tcl):
    tcl.command("open file.txt r w extra")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "open expects fileName ?access? ?permissions?" in result.stderr


def test_close_is_validation_only(tcl: Tcl):
    tcl.command("close $fd")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_close_wrong_arity_is_error(tcl: Tcl):
    tcl.command("close")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "close expects channelId" in result.stderr


def test_gets_is_validation_only(tcl: Tcl):
    tcl.command("gets $fd")
    tcl.command("gets $fd line")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_gets_wrong_arity_is_error(tcl: Tcl):
    tcl.command("gets")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "gets expects channelId ?numChars/varName?" in result.stderr


def test_read_is_validation_only(tcl: Tcl):
    tcl.command("read $fd")
    tcl.command("read $fd 100")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_seek_is_validation_only(tcl: Tcl):
    tcl.command("seek $fd 0")
    tcl.command("seek $fd 0 start")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_seek_wrong_arity_is_error(tcl: Tcl):
    tcl.command("seek $fd")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "seek expects channelId offset ?origin?" in result.stderr


def test_tell_is_validation_only(tcl: Tcl):
    tcl.command("tell $fd")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_eof_is_validation_only(tcl: Tcl):
    tcl.command("eof $fd")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_flush_is_validation_only(tcl: Tcl):
    tcl.command("flush $fd")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_fcopy_is_validation_only(tcl: Tcl):
    tcl.command("fcopy $in $out")
    tcl.command("fcopy $in $out -size 1024 callback")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_fcopy_too_few_args_is_error(tcl: Tcl):
    tcl.command("fcopy $in")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "fcopy expects input output ?-size size? ?callback?" in result.stderr


def test_fconfigure_is_validation_only(tcl: Tcl):
    tcl.command("fconfigure $fd")
    tcl.command("fconfigure $fd -buffering line")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_fconfigure_wrong_arity_is_error(tcl: Tcl):
    tcl.command("fconfigure $fd -buffering")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "fconfigure expects channelId ?name value ...?" in result.stderr


def test_fileevent_is_validation_only(tcl: Tcl):
    tcl.command("fileevent $fd readable")
    tcl.command("fileevent $fd readable callback")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_fileevent_wrong_arity_is_error(tcl: Tcl):
    tcl.command("fileevent $fd")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "fileevent expects channelId readable/writable ?script?" in result.stderr


def test_encoding_names_is_validation_only(tcl: Tcl):
    tcl.command("encoding names")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_encoding_system_is_validation_only(tcl: Tcl):
    tcl.command("encoding system")
    tcl.command("encoding system utf-8")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_encoding_convertfrom_is_validation_only(tcl: Tcl):
    tcl.command("encoding convertfrom utf-8 data")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_encoding_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("encoding bad")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown encoding subcommand 'bad'" in result.stderr


def test_binary_format_is_validation_only(tcl: Tcl):
    tcl.command("binary format H* deadbeef")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_binary_scan_is_validation_only(tcl: Tcl):
    tcl.command("binary scan data H* hex")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_binary_unknown_subcommand_is_error(tcl: Tcl):
    tcl.command("binary bad")

    result = tcl.run()

    assert result.returncode != 0
    assert "semantic error" in result.stderr
    assert "unknown binary subcommand 'bad'" in result.stderr


def test_subst_is_validation_only(tcl: Tcl):
    tcl.command("subst {hello $world}")
    tcl.command("subst -nobackslashes -nocommands {hello $world}")

    result = tcl.run()

    assert result.returncode == 0
    assert result.stderr == ""
    assert result.stdout == ""


def test_subst_no_string_is_error(tcl: Tcl):
    tcl.command("subst")

    result = tcl.run()

    assert result.returncode != 0
    assert "syntax error" in result.stderr
    assert "subst expects ?-nobackslashes? ?-nocommands? ?-novariables? string" in result.stderr
