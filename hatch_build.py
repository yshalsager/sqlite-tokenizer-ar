import os
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


class CustomBuildHook(BuildHookInterface):
    def initialize(self, version, build_data):
        binary = Path(os.environ['SQLITE_TOKENIZER_AR_BINARY']).resolve(strict=True)
        build_data['pure_python'] = False
        build_data['tag'] = f"py3-none-{os.environ['WHEEL_PLATFORM']}"
        build_data['force_include'][str(binary)] = f'sqlite_tokenizer_ar/{binary.name}'
