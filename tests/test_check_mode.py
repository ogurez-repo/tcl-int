import subprocess

from .tcl import Tcl


def test_check_mode_accepts_valid_script_without_execution(tcl: Tcl):
    tcl.command("set a 1")
    tcl.command("puts $a")

    result = tcl.run(args=["--check"])

    assert result.returncode == 0
    assert result.stdout == ""
    assert result.stderr == ""


def test_check_mode_reports_lexical_errors(tcl: Tcl):
    tcl.command('puts "abc')

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert "lexical error" in result.stderr
    assert "unterminated quoted string" in result.stderr


def test_check_mode_reports_semantic_unknown_command(tcl: Tcl):
    tcl.command("unknown 1")

    result = tcl.run(args=["--check"])

    assert result.returncode != 0
    assert result.stderr == "semantic error at 1:1: unknown command 'unknown'\n"


def test_check_mode_does_not_execute_runtime_variable_lookup(tcl: Tcl):
    tcl.command("puts $missing")

    result = tcl.run(args=["--check"])

    assert result.returncode == 0
    assert result.stdout == ""
    assert result.stderr == ""


def test_check_mode_accepts_argument_expansion_syntax(tcl: Tcl):
    tcl.command("set {*}$pair")

    result = tcl.run(args=["--check"])

    assert result.returncode == 0
    assert result.stdout == ""
    assert result.stderr == ""


def test_invalid_cli_mode_prints_usage(tcl: Tcl):
    result = subprocess.run(
        [tcl.tcl_path, "--invalid-mode"],
        input="",
        text=True,
        capture_output=True,
        check=False,
        timeout=5,
    )

    assert result.returncode != 0
    assert "usage:" in result.stderr
