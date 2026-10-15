"""Compile and exercise Frame's actual, dependency-free native layout maths.

This checks geometry invariants, not Views rendering or Windows interaction.
Set CXX to a C++17-or-newer compiler (g++ or clang++).
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DockLayoutTests(unittest.TestCase):
    def test_window_overflow_and_caption_exclusion(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('C++ compiler unavailable; native geometry was not checked')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'dock.cc'
            binary = Path(directory) / 'dock-test'
            source.write_text(r'''
#include "chrome/browser/ui/views/frame/frame_dock_layout.h"
#include <cassert>
#include <limits>
int main() {
  using namespace frame;
  // Empty browsers still offer a plus; one tab fits fully above the footer.
  assert(DockPreferredHeight(0) >= kDockFooterHeight);
  assert(DockPreferredHeight(1) - kDockFooterHeight - 4 >= kDockTabHeight);
  assert(DockPreferredHeight(std::numeric_limits<int>::max()) == 600);
  assert(DockPreferredHeight(-1) == DockPreferredHeight(0));
  for (int height : {0, 40, 88, 240, 420, 720, 900, 2160}) {
    for (int count : {0, 1, 4, 16, 1000}) {
      const int top = 60, bottom = top + height;
      const auto dock = CalculateDockBounds(top, bottom, DockPreferredHeight(count));
      assert(dock.height >= 0 && dock.height <= height && dock.height <= 600);
      assert(dock.y >= top && dock.y + dock.height <= bottom);
      // Rounding leaves at most one DIP asymmetry about the usable area.
      assert((dock.y - top) - (bottom - dock.y - dock.height) >= -1);
      assert((dock.y - top) - (bottom - dock.y - dock.height) <= 1);
      if (height >= DockPreferredHeight(count))
        assert(dock.height == DockPreferredHeight(count));
    }
  }
  assert(CalculateDockBounds(200, 100, 600).height == 0);
  // Caption, split button and gutter exclusions survive narrow windows and
  // offsets (including a secondary monitor with negative coordinates).
  for (int origin : {-1920, 0, 1920}) {
    for (int width : {0, 240, 600, 1280, 3440}) {
      const int left = origin + kDockGutter;
      const int right = std::max(left, origin + width - 180);
      const auto nav = CalculateToolbarBounds(left, right, origin + width / 2);
      assert(nav.x >= left && nav.x + nav.width <= right);
      assert(nav.width >= 0 && nav.width <= 720);
      assert(nav.width == std::min(720, right - left));
      if (origin + width / 2 - nav.width / 2 >= left &&
          origin + width / 2 + nav.width / 2 <= right)
        assert(nav.x + nav.width / 2 == origin + width / 2);
    }
  }
}
''')
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I', str(ROOT / 'native/overlay'), str(source),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
