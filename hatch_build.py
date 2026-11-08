import os
from pathlib import Path
import re
import subprocess

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


def check_macos_tag(binary, platform_tag):
    if not platform_tag.startswith('macosx_'):
        return
    output = subprocess.check_output(['otool', '-l', str(binary)], text=True)
    match = re.search(r'\bcmd LC_(?:BUILD_VERSION|VERSION_MIN_MACOSX)\b.*?\n\s+(?:minos|version) ([\d.]+)', output, re.S)
    if match is None:
        raise ValueError(f'Cannot determine the minimum macOS version of {binary}')
    minimum = tuple(map(int, match[1].split('.')))
    target = tuple(map(int, platform_tag.split('_')[1:3]))
    if minimum[:2] > target:
        raise ValueError(f'{binary} requires macOS {match[1]}, but {platform_tag} claims {target[0]}.{target[1]}')


class CustomBuildHook(BuildHookInterface):
    def initialize(self, version, build_data):
        binary = Path(os.environ['SQLITE_TOKENIZER_AR_BINARY']).resolve(strict=True)
        platform_tag = os.environ['WHEEL_PLATFORM']
        check_macos_tag(binary, platform_tag)
        build_data['pure_python'] = False
        build_data['tag'] = f'py3-none-{platform_tag}'
        build_data['force_include'][str(binary)] = f'sqlite_tokenizer_ar/{binary.name}'
