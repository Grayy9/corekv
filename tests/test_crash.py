import os
import subprocess
import tempfile
import unittest


class CrashTests(unittest.TestCase):
    def test_acknowledged_write_survives_process_exit_without_destructor(self):
        writer = os.environ.get("COREKV_CRASH_WRITER", "./corekv_crash_writer")
        cli = os.environ.get("COREKV_CLI", "./corekv_cli")
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([writer, directory], check=True, timeout=10)
            result = subprocess.run([cli, directory, "get", "survives"],
                                    capture_output=True, text=True, timeout=10, check=True)
            self.assertEqual(result.stdout, "yes\n")
            batch = subprocess.run([cli, directory, "get", "batch-a"],
                                   capture_output=True, text=True, timeout=10, check=True)
            self.assertEqual(batch.stdout, "one\n")


if __name__ == "__main__":
    unittest.main()
