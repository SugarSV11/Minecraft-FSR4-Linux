# Third-party notices / Avisos de terceros

## DirectX-Headers (Microsoft) — MIT

`native/include/d3d12_compat.h` declares `ID3D12Device2` and `ID3D12Device3` (method order and IIDs) following
Microsoft's `d3d12.idl` from <https://github.com/microsoft/DirectX-Headers>. It is only used when the toolchain's own
`<d3d12.h>` lacks them (mingw-w64 11.0.1 of Ubuntu 24.04).

```
MIT License

Copyright (c) Microsoft Corporation.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

## AMD FidelityFX SDK

The helper `fsr4_bridge.exe` is compiled against the headers of the AMD FidelityFX SDK v2.3.0 (MIT, Advanced Micro Devices, Inc.),
which is downloaded at build time and not vendored here. The official AMD DLLs (`amd_fidelityfx_*_dx12.dll`) are redistributed
binary-only under AMD's license shipped with the SDK.
