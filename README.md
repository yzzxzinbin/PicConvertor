## JPG -> Unicode 终端绘制器

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
./picconvertor.exe -i sample.jpg -w 170 -s high

# 输出为转义字符到文本文件
./picconvertor -i path/to/image.jpg -w 80 -s high -o out.txt
```

依赖：`stb_image.h`（放置在 `third_party/` 或允许 CMake 自动下载）。

效果图:
 - 170宽 high映射模式
<img width="2160" height="1368" alt="image" src="https://github.com/user-attachments/assets/3f99de00-275c-418d-b4e4-ea9720747174" />

 - 300宽 high映射模式
<img width="2160" height="1368" alt="image" src="https://github.com/user-attachments/assets/ec87446b-0976-415f-b460-66dc6c50a2e7" />

