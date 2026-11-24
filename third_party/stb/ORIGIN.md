# stb_image

源码：https://github.com/nothings/stb

作者维护仓库：Sean Barrett（nothings）。固定提交：`6b5799bae8a1fe3792691ef4b72ae26434c3e799`。

`stb_image.h` 按上游原样保存；许可证保留于 `LICENSE` 和上游头文件。Chimera 仅启用内存输入的 PNG/JPEG 解码，禁用 HDR、线性浮点和 stdio 接口。

测试数据 `tests/graphics.png` 与 `tests/graphics.jpg` 使用 Pillow 独立编码。两者均为 2×2 的红、绿、蓝、白四像素，JPEG 使用 quality=100、subsampling=0。
