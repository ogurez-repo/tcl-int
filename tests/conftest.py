import pytest
from .tcl import Tcl


@pytest.fixture()
def tcl():
    return Tcl("./bin/tclsh.exe")
