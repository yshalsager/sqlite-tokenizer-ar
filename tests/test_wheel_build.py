import unittest
from unittest.mock import patch

from hatch_build import check_macos_tag


class MacosTagTests(unittest.TestCase):
    @patch('hatch_build.subprocess.check_output')
    def test_macos_versions(self, otool):
        for command, version_line, tag in [
            ('LC_BUILD_VERSION', 'platform 1\n    minos 11.0', 'macosx_11_0_arm64'),
            ('LC_VERSION_MIN_MACOSX', 'version 10.13', 'macosx_10_13_x86_64'),
        ]:
            with self.subTest(command=command):
                otool.return_value = f'Load command 0\n cmd {command}\n cmdsize 32\n {version_line}\n sdk 15.0\n'
                check_macos_tag('extension.so', tag)
                check_macos_tag('extension.so', 'macosx_15_0_arm64')
                with self.assertRaisesRegex(ValueError, 'requires macOS'):
                    check_macos_tag('extension.so', 'macosx_10_12_x86_64')


if __name__ == '__main__':
    unittest.main()
