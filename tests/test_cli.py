import os
import subprocess
import tempfile
import unittest


class CliTests(unittest.TestCase):
    def test_round_trip_across_processes(self):
        executable = os.environ.get("COREKV_CLI", "./corekv_cli")
        with tempfile.TemporaryDirectory() as directory:
            def run(*arguments):
                return subprocess.run([executable, directory, *arguments],
                                      capture_output=True, text=True, timeout=10)

            self.assertEqual(run("put", "name", "alice").returncode, 0)
            self.assertEqual(run("get", "name").stdout, "alice\n")
            self.assertEqual(run("delete", "name").returncode, 0)
            missing = run("get", "name")
            self.assertEqual(missing.returncode, 3)
            self.assertEqual(run("put", "name", "bob").returncode, 0)
            self.assertEqual(run("flush").returncode, 0)
            self.assertEqual(run("compact").returncode, 0)
            self.assertEqual(run("get", "name").stdout, "bob\n")

    def test_scan_prints_ordered_rows(self):
        executable = os.environ.get("COREKV_CLI", "./corekv_cli")
        with tempfile.TemporaryDirectory() as directory:
            def run(*arguments):
                return subprocess.run([executable, directory, *arguments],
                                      capture_output=True, text=True, timeout=10, check=True)

            run("put", "b", "two")
            run("put", "a", "one")
            run("put", "c", "three")
            self.assertEqual(run("scan", "a", "z", "2").stdout, "a\tone\nb\ttwo\n")


if __name__ == "__main__":
    unittest.main()
