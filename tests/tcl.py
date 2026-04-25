import subprocess


class Tcl:
    def __init__(self, tcl_path: str) -> None:
        self.tcl_path = tcl_path
        self._script_lines: list[str] = []

    def command(self, text: str) -> None:
        self._script_lines.append(text)

    def run(self, args: list[str] | None = None) -> subprocess.CompletedProcess[str]:
        script = "\n".join(self._script_lines)
        if script:
            script += "\n"

        command = [self.tcl_path]
        if args:
            command.extend(args)

        return subprocess.run(
            command,
            input=script,
            text=True,
            capture_output=True,
            check=False,
            timeout=5,
        )

    def set(self, var: str, value: str) -> None:
        self.command(f"set {var} {value}")

    def put(self, text: str) -> None:
        self.command(f"put {text}")
