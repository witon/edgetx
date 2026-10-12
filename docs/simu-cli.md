# simu-cli

`simu-cli` 是没有遥控器外壳的固件模拟器。它跑某一款遥控器的固件，用 JSON 行接收按键、开关和摇杆，也可以把 LCD 画到一个窗口里。带摇杆和开关面板的是另一个程序 `simu`，两者不是同一个可执行文件。

每种遥控器各有自己的 `simu-cli`。Zorro 是 128×64 单色屏，TX16S 是 480×272 彩屏，程序不能互换。

## 取得 macOS 上的 Zorro 程序

当前带 `--screen` 的构建产物是 Apple Silicon（arm64）。Intel Mac 不能直接运行。

1. 打开这次构建：<https://github.com/witon/edgetx/actions/runs/38200152005>
2. 在页面底部下载产物 **simu-cli-zorro-macos**。
3. 解压后得到 `simu-cli`。

也可以在终端下载：

```bash
gh run download 38200152005 -n simu-cli-zorro-macos
chmod +x simu-cli
```

产物大约保留 15 天。过期后打开拉取请求 <https://github.com/witon/edgetx/pull/3> 的 Checks，进入名为 **simu-cli** 的工作流，下载其中的 **simu-cli-zorro-macos**。

## 第一次打开

从 GitHub 下载的程序没有苹果签名。双击时系统会提示无法验证是否包含恶意软件。在放着 `simu-cli` 的目录执行：

```bash
xattr -d com.apple.quarantine simu-cli
chmod +x simu-cli
codesign --force --sign - simu-cli
```

如果提示已经弹出过，先点「完成」，再打开 **系统设置 → 隐私与安全性**，在「安全性」里点 **仍要打开**。

屏幕窗口依赖 Homebrew 的 SDL2：

```bash
brew install sdl2
```

## 运行

准备一个空目录当作 SD 卡，不要省略 `--storage`，否则模型文件会写到当前目录。

只看屏幕，控制仍走标准输入：

```bash
./simu-cli --storage ./sd --screen
```

窗口标题是 `EdgeTX simu-cli`，里面只有 LCD。Zorro 的 128×64 画面会放大到约 512×256。关掉窗口、发送 `stop`，或断开控制端，进程都会退出。

边看屏幕边在终端里看日志时，改用 TCP，标准输出才留给日志：

```bash
./simu-cli --storage ./sd --control tcp://127.0.0.1:17666 --screen
```

## 命令行参数

| 参数 | 作用 |
| --- | --- |
| `--storage 路径` | SD 卡目录 |
| `--settings 路径` | 设置目录。省略时和使用 `--storage` 时的默认位置由固件决定 |
| `--control stdio` | 默认。标准输入和标准输出都是 JSON，每行一条 |
| `--control tcp://主机:端口` | JSON 走 TCP，日志打到标准输出 |
| `--screen` | 打开 LCD 窗口。编译时没有 SDL2 会直接报错退出 |
| `-h`、`--help` | 打印帮助 |

`--width` 和 `--height` 只影响带面板的 `simu`。`simu-cli --screen` 按 LCD 尺寸自己决定窗口大小。

## 日志

默认的 `stdio` 模式里，标准输出只有 JSON。`debugPrintf()`、Lua `print()` 和 `-E- ` 错误进内部缓冲，用 `log` 命令读取。

`--control tcp://...` 时，同一份日志会同时写到启动它的终端。

`--screen` 不改变日志去向。

## 发一条命令

进程就绪后，标准输出先有一行：

```json
{"event":"ready","control":"stdio"}
```

之后每条请求带 `id` 和 `cmd`。下面把 Zorro 的 PAGE 键点一次，再读新日志，最后停止：

```bash
printf '%s\n' \
  '{"id":1,"cmd":"key","name":"PAGE","action":"click"}' \
  '{"id":2,"cmd":"log","since":0}' \
  '{"id":3,"cmd":"stop"}' \
  | ./simu-cli --storage ./sd --screen
```

把当前画面写成 PNG：

```json
{"id":1,"cmd":"screenshot","path":"lcd.png"}
```

某一固件实际有哪些按键、开关和摇杆，问它自己：

```json
{"id":1,"cmd":"help"}
```

命令的完整列表、遥测脚本和小部件的打开方式在 [tools/simctl/PROTOCOL.md](../tools/simctl/PROTOCOL.md)。Python 客户端是 `tools/simctl`。在 `tools` 目录可以这样跑一次脚本检查：

```bash
python -m simctl test --kind telemetry --sim /path/to/simu-cli --storage ./sd --script telem --expect "telem ready"
```

## 自己编译

macOS 上编译 Zorro，需要 CMake、Ninja、Python 依赖和 SDL2。`--screen` 只有在配置时找得到 SDL2 才会编进去。

```bash
brew install cmake ninja sdl2
cmake -S . -B build-zorro -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DEdgeTX_SUPERBUILD=OFF \
  -DNATIVE_BUILD=ON \
  -DDISABLE_COMPANION=ON \
  -DNATIVE_GTESTS=OFF \
  -DPCB=X7 \
  -DPCBREV=ZORRO \
  -DCMAKE_PREFIX_PATH="$(brew --prefix sdl2)"
cmake --build build-zorro --target simu-cli --parallel
```

编出来的文件是 `build-zorro/simu-cli`。换别的遥控器时改 `-DPCB` 和 `-DPCBREV`，并使用那次构建得到的程序。
