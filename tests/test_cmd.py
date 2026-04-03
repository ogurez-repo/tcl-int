from .tcl import Tcl


def test_set_put(tcl: Tcl):
    tcl.set("foo", "bar")
    tcl.put("$foo")

    result = tcl.run()

    assert result == "bar"


def test_put(tcl: Tcl):
    tcl.put("Hello")
    result = tcl.run()
    assert result == "Hello"


def test_quoted_interpolated_set_command(tcl: Tcl):
    tcl.set("ab", "set")
    tcl._cmd += '"$ab" a a\n'
    tcl.put("$a")

    result = tcl.run()

    assert result == "a"


def test_quoted_prefixed_interpolated_set_command(tcl: Tcl):
    tcl.set("a", "et")
    tcl._cmd += '"s$a" bbbb b\n'
    tcl.put("$bbbb")

    result = tcl.run()

    assert result == "b"