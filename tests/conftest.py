import pytest
from .tcl import Tcl


@pytest.fixture(scope="session")
def tcl():
    return Tcl("./bin/tclsh.exe")
