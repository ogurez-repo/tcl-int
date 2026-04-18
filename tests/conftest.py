from pathlib import Path

import pytest

from .tcl import Tcl


@pytest.fixture()
def tcl():
    candidates = [Path("./bin/tclsh"), Path("./bin/tclsh.exe")]
    for candidate in candidates:
        if candidate.exists() and candidate.is_file():
            return Tcl(str(candidate))

    pytest.fail("tcl binary not found in ./bin; run `make` first")
