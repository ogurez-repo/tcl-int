import subprocess

class Tcl:
    def __init__(self, tcl_path: str) -> None:
        self.tcl_path = tcl_path

    def cmd(self, command: str) -> str:
        result = subprocess.run(
            [self.tcl_path],
            input=command,
            text=True,
            capture_output=True,
        )

        return result.stdout.strip()
