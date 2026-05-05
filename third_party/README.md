# 第三方依赖

本目录存放预下载的第三方库压缩包，用于内网/离线构建。

## 依赖列表

| 压缩包 | 版本 | 来源 |
|---|---|---|
| `fmt-11.2.0.tar.gz` | 11.2.0 | https://github.com/fmtlib/fmt |
| `googletest-1.17.0.tar.gz` | v1.17.0 | https://github.com/google/googletest |
| `spdlog-1.15.3.tar.gz` | v1.15.3 | https://github.com/gabime/spdlog |
| `nlohmann_json-3.12.0.tar.gz` | v3.12.0 | https://github.com/nlohmann/json |
| `boost_1_90_0.zip` | 1.90.0 | https://archives.boost.io |

## 外网机器（首次下载，一人执行一次）

```powershell
.\third_party\download_deps.bat   # 下载全部依赖包
.\third_party\package.bat         # 打包为 third_party.zip
# 将 third_party.zip 上传到公司内部文件服务器
```

## 内网开发机器

1. 从公司内部文件服务器下载 `third_party.zip`
2. 解压到项目根目录（`third_party/` 与 `CMakeLists.txt` 同级）
3. 正常构建：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

CMake 配置阶段会输出 `Using local archive: xxx` 表示正在使用本地依赖包。
当本地包不存在时，自动回退到从远程 URL 下载。
