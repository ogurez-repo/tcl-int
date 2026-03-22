import subprocess

class Tcl:
    def __init__(self, tcl_path: str) -> None:
        self.tcl_path = tcl_path
        self._cmd = ""

    def run(self) -> str:
        result = subprocess.run(
            [self.tcl_path],
            input=self._cmd,
            text=True,
            capture_output=True,
        )

        return result.stdout.strip()

    def set(self, var: str, value: str) -> None:
        self._cmd += f"set {var} {value}\n"

    def put(self, var: str) -> None:
        self._cmd += f"put {var}\n"
