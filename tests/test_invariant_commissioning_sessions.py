import pytest
from unittest.mock import patch, MagicMock
import sys
import os

# Add the source path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "factory_interface", "src"))


@pytest.mark.parametrize("payload", [
    "AA:BB:CC:DD:EE:FF; rm -rf /",
    "AA:BB:CC:DD:EE:FF$(whoami)",
    "AA:BB:CC:DD:EE:FF`id`",
    "AA:BB:CC:DD:EE:FF | cat /etc/passwd",
    "AA:BB:CC:DD:EE:FF",  # valid input
])
def test_subprocess_never_receives_shell_metacharacters(payload):
    """Invariant: Shell commands never include unsanitized user input containing metacharacters."""
    shell_metacharacters = [";", "|", "$", "`", "&", "(", ")", "{", "}", "<", ">"]

    with patch("subprocess.run") as mock_run:
        mock_run.return_value = MagicMock(returncode=0, stdout="", stderr="")
        
        try:
            from factory_interface.commissioning_sessions import commission_device
            try:
                commission_device(payload)
            except Exception:
                pass
        except ImportError:
            try:
                from factory_interface.commissioning_sessions import start_session
                try:
                    start_session(payload)
                except Exception:
                    pass
            except ImportError:
                pass

        for call in mock_run.call_args_list:
            args, kwargs = call
            # If shell=True is used, the command must not contain unsanitized metacharacters
            if kwargs.get("shell", False):
                cmd = args[0] if args else kwargs.get("args", "")
                if isinstance(cmd, str) and any(mc in payload for mc in shell_metacharacters):
                    # The raw payload with metacharacters must NOT appear in shell command
                    assert payload not in cmd, (
                        f"Unsanitized input '{payload}' was passed directly to shell command: {cmd}"
                    )
            # If shell=False with a list, ensure no element contains the raw malicious payload
            elif not kwargs.get("shell", False):
                cmd_list = args[0] if args else kwargs.get("args", [])
                if isinstance(cmd_list, list):
                    for element in cmd_list:
                        if any(mc in payload for mc in shell_metacharacters):
                            if payload in str(element):
                                # List-based subprocess with unsanitized input is still risky
                                # but less so; flag if it contains the full malicious string
                                assert not any(mc in str(element) for mc in [";", "|", "&", "`", "$("]), (
                                    f"Shell metacharacters from '{payload}' found in subprocess arg: {element}"
                                )