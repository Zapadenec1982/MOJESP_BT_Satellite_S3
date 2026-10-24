"""Replay production JPEG/PPA geometry without the hardware engines."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest
from .test_cpp_host import find_compiler


def test_jpeg_mcu_stride(tmp_path):
    compiler = find_compiler()
    if not compiler:
        pytest.skip("C++ compiler unavailable")
    root = Path(__file__).resolve().parents[2]
    source = (root / "components/cover/src/cover.cpp").read_text(encoding="utf-8")
    start = source.index("bool jpeg_layout(")
    end = source.index("\n}\n", start) + 2
    function = source[start:end]
    test = r'''
#include <cstdint>
#include <cassert>
enum { JPEG_DOWN_SAMPLING_YUV444, JPEG_DOWN_SAMPLING_YUV422,
       JPEG_DOWN_SAMPLING_YUV420, JPEG_DOWN_SAMPLING_GRAY };
struct jpeg_decode_picture_info_t { uint32_t width, height; int sample_method; };
''' + function + r'''
int main() {
    struct Case { uint32_t w,h; int sampling; uint32_t stride,rows; };
    // 200px Android thumbnails expose the old unconditional 208px row stride.
    const Case cases[] = {
        {200,200,JPEG_DOWN_SAMPLING_YUV444,200,200},
        {200,200,JPEG_DOWN_SAMPLING_YUV422,208,200},
        {200,200,JPEG_DOWN_SAMPLING_YUV420,208,208},
        {201,199,JPEG_DOWN_SAMPLING_YUV444,208,200},
        {17,17,JPEG_DOWN_SAMPLING_YUV420,32,32},
        {512,512,JPEG_DOWN_SAMPLING_YUV444,512,512},
    };
    for (const auto& c : cases) {
        uint32_t w=0,h=0;
        assert(jpeg_layout({c.w,c.h,c.sampling},w,h));
        assert(w==c.stride && h==c.rows);
        // Every PPA row must address the same decoded source pixel.
        for (uint32_t y=0;y<c.h;++y)
            assert(y*w+(c.w-1) == y*c.stride+(c.w-1));
    }
    uint32_t w=0,h=0;
    assert(!jpeg_layout({200,200,JPEG_DOWN_SAMPLING_GRAY},w,h));
    assert(!jpeg_layout({200,200,999},w,h));
}
'''
    cpp = tmp_path / "cover_layout.cpp"
    cpp.write_text(test)
    exe = tmp_path / ("cover_layout.exe" if os.name == "nt" else "cover_layout")
    env = dict(os.environ)
    if compiler.get("extra_path"):
        env["PATH"] = compiler["extra_path"] + os.pathsep + env.get("PATH", "")
    # The PATH fallback (Linux) leaves "gxx" empty so cmake picks its own compiler; this
    # test calls the compiler itself, so it needs the name.
    gxx = compiler["gxx"] or shutil.which("g++")
    subprocess.run([gxx, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    str(cpp), "-o", str(exe)], env=env, check=True)
    subprocess.run([str(exe)], env=env, check=True)
