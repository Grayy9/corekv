import json
import os
import subprocess
import tempfile
import unittest


class BenchTests(unittest.TestCase):
    def test_reports_completed_operations(self):
        executable = os.environ.get("COREKV_BENCH", "./corekv_bench")
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([executable, directory, "10"], capture_output=True,
                                    text=True, timeout=10, check=True)
            report = json.loads(result.stdout)
            self.assertEqual(report["writes_completed"], 10)
            self.assertEqual(report["reads_completed"], 10)
            self.assertGreater(report["write_ops_per_second"], 0)

    def test_reports_batch_write_operations(self):
        executable = os.environ.get("COREKV_BENCH", "./corekv_bench")
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([executable, directory, "10", "4"], capture_output=True,
                                    text=True, timeout=10, check=True)
            report = json.loads(result.stdout)
            self.assertEqual(report["writes_completed"], 10)
            self.assertEqual(report["reads_completed"], 10)
            self.assertEqual(report["batch_size"], 4)
            self.assertGreater(report["write_ops_per_second"], 0)


if __name__ == "__main__":
    unittest.main()
