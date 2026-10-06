# 模拟器控制协议

`simu-cli` 是无界面模拟器。带窗口的 `simu` 加上 `--control` 后使用同一套命令。标准输出在 `stdio` 模式下只放 JSON，每行一条；固件原来的 `debugPrintf()` 日志（含 Lua `print()` 和 `-E- ` 错误）改由 `log` 命令读取，内容和 Companion 调试窗口相同。

就绪时先有一行：

```json
{"event":"ready","control":"stdio"}
```

请求和响应都带 `id`。失败时 `ok` 为 false，并带 `code` 与 `error`。

## 打开脚本

黑白屏遥测和彩屏小部件是两个固件，各用自己的 `simu-cli`。

遥测脚本放在 SD 卡的 `/SCRIPTS/TELEMETRY/<名字>.lua`。`<名字>` 最多 6 个字符。`show_telemetry` 会把它设为第 0 个遥测页并打开该页，然后等到 `run` 执行过一次。

小部件放在 `/WIDGETS/<目录>/main.lua`。`show_widget` 的 `name` 是脚本返回的 `name`，不是目录名。它把小部件放到主画面第 0 区，并等到 `refresh` 被调用。

## 按步输入并读日志

模拟器进程保持运行。测试脚本每次发一条命令，再用 `log` 取上次之后的新行，据此决定下一步。

```python
sim.key("PAGE")
lines = sim.log_since()
if any("telem event" in line for line in lines):
    sim.switch("SA", "up")
```

`log` 的 `since` 是上一次返回的 `next`。按键命令返回时，对应的 `print` 可能还要等下一次 `run` 或 `refresh`，所以用 `log_since()` 或 `wait_log()` 读取，而不是认为命令一返回日志就已经更新。

日志里出现 `-E- `、`Error loading script` 或 `Error parsing script` 表示脚本失败。

## 命令

输入：`key`（`name`，`action` 为 down、up 或 click）、`switch`（`state` 为 up、mid、down）、`stick`（`ail`/`ele`/`thr`/`rud`，数值 -100 到 100）、`pot`、`analog`（原始 ADC 0 到 4096）、`trim`、`encoder`、`touch`、`voltage`。

观测：`log`、`screenshot`、`channels`、`mixes`、`logical_switches`、`trims`、`flight_mode`、`gvars`、`status`。

其它：`help`、`wait`、`reload_lua`、`show_telemetry`、`show_widget`、`stop`。

`help` 返回这一固件实际可用的按键、开关、摇杆和电位器名称。

## 一次性测试

在 `tools` 目录执行：

```text
python -m simctl test --kind telemetry --sim ./simu-cli --storage ./sd --script telem --expect "telem ready"
python -m simctl test --kind widget --sim ./simu-cli --storage ./sd --script MyWidget --expect "widget ready"
```

`--expect` 匹配模拟器日志里的现有文本，可以重复。不写 `--expect` 时只根据上面的错误行判定。进程退出码 0 为通过，1 为失败。标准输出是一行 JSON，里面的 `log` 就是参与判定的日志。

示例脚本在 `examples/telemetry.lua` 和 `examples/widget/main.lua`。按日志决定下一步输入的写法在 `examples/dynamic_session.py`。
