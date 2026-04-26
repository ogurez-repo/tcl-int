import subprocess
import os
import tempfile
from pathlib import Path


class Tcl:
    def __init__(self, tcl_path: str) -> None:
        self.tcl_path = tcl_path
        self._script_lines: list[str] = []

    def command(self, text: str) -> None:
        self._script_lines.append(text)

    def run(
        self,
        args: list[str] | None = None,
        stdin_data: str = "",
        script_from_stdin: bool = False,
    ) -> subprocess.CompletedProcess[str]:
        script = "\n".join(self._script_lines)
        if script:
            script += "\n"

        command = [self.tcl_path]
        if args:
            command.extend(args)

        temp_path: Path | None = None
        input_data = stdin_data

        if script_from_stdin:
            input_data = script
        else:
            fd, raw_path = tempfile.mkstemp(prefix="minitcl_", suffix=".tcl")
            temp_path = Path(raw_path)
            with os.fdopen(fd, "w", encoding="utf-8") as handle:
                handle.write(script)
            command.append(str(temp_path))

        try:
            return subprocess.run(
                command,
                input=input_data,
                text=True,
                capture_output=True,
                check=False,
                timeout=5,
            )
        finally:
            if temp_path and temp_path.exists():
                temp_path.unlink()

    def set(self, var: str, value: str) -> None:
        self.command(f"set {var} {value}")

    def put(self, text: str) -> None:
        self.command(f"puts {text}")
