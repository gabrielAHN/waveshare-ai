"""Wheel build hook that bundles the dashboard plugin used by the installed CLI."""
from pathlib import Path
import shutil

from setuptools import setup
from setuptools.command.build_py import build_py


class BuildPyWithDashboardPlugin(build_py):
    def run(self):
        super().run()
        bridge_root = Path(__file__).resolve().parent
        source = bridge_root.parent / 'plugins' / 'hermes' / 'dashboard-plugin'
        target = Path(self.build_lib) / 'waveshare_bridge' / 'dashboard_plugin'
        if not (source / 'plugin.yaml').is_file() or not (source / 'dashboard' / 'plugin_api.py').is_file():
            raise RuntimeError('dashboard plugin source is missing')
        shutil.copytree(source, target, dirs_exist_ok=True,
                        ignore=shutil.ignore_patterns('tests', '__pycache__', '*.md'))


setup(cmdclass={'build_py': BuildPyWithDashboardPlugin})
