# PicConvertor

JPG -> Unicode 终端绘制器

构建：

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

示例：

```bash
# 输出到终端
./picconvertor -i path/to/image.jpg -w 100

# 输出为块字符到文本文件
./picconvertor -i path/to/image.jpg -w 80 -s high -o out.txt
```

依赖：`stb_image.h`（放置在 `third_party/` 或允许 CMake 自动下载）。
