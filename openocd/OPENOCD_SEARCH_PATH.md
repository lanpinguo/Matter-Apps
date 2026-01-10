# OpenOCD 路径搜索逻辑详解

## 概述

OpenOCD 使用 `[find filename]` 命令在配置文件中查找其他配置文件。这个命令会在多个搜索路径中查找指定的文件。

## `find` 命令的工作原理

在配置文件中使用 `[find filename]` 时，OpenOCD 会在以下路径中按顺序搜索：

### 1. 默认搜索路径

OpenOCD 的默认搜索路径通常是：
- **安装目录下的 `scripts/` 目录**
  - 例如：`/usr/share/openocd/scripts/`
  - 或：`/opt/nordic/ncs/v3.1.1/toolchain/opt/zephyr-sdk/0.16.5/sysroots/x86_64-pokysdk-linux/usr/share/openocd/scripts/`

### 2. 通过 `-s` 参数指定的搜索路径

使用 `-s` 命令行参数可以添加额外的搜索路径：

```bash
openocd -s /path/to/custom/scripts -f openocd.cfg
```

可以指定多个搜索路径：
```bash
openocd -s /path/to/scripts1 -s /path/to/scripts2 -f openocd.cfg
```

### 3. 配置文件所在目录

OpenOCD 也会在配置文件所在的目录中搜索。

## 实际使用示例

### 当前项目结构

```
apps/openocd/
├── openocd-54.cfg          # 主配置文件
├── openocd-52.cfg
├── board/
│   └── nrf54l15-dk.cfg
└── target/
    └── nordic/
        ├── nrf51.cfg
        ├── nrf52.cfg
        ├── nrf53.cfg
        ├── nrf54l.cfg       # nRF54L 系列配置
        ├── nrf91.cfg
        └── common.cfg
```

### 配置文件内容

```tcl
# openocd-54.cfg
source [find interface/jlink.cfg]
source [find target/nordic/nrf54l.cfg]
```

### 运行方式

#### 方式 1：指定搜索路径（推荐）

```bash
# 从 apps/openocd 目录运行
cd /Users/lanpinguo/work/matter/nrf/apps/openocd
openocd -s . -f openocd-54.cfg
```

这样 OpenOCD 会在当前目录（`.`）中搜索：
- `interface/jlink.cfg` → 在默认路径或 `-s` 指定的路径中查找
- `target/nordic/nrf54l.cfg` → 在当前目录的 `target/nordic/` 子目录中查找

#### 方式 2：使用绝对路径

```bash
openocd -s /Users/lanpinguo/work/matter/nrf/apps/openocd -f openocd-54.cfg
```

#### 方式 3：从其他目录运行

```bash
# 从 apps 目录运行
cd /Users/lanpinguo/work/matter/nrf/apps
openocd -s openocd -f openocd/openocd-54.cfg
```

## 搜索顺序

OpenOCD 按以下顺序搜索文件：

1. **通过 `-s` 参数指定的路径**（按指定顺序）
2. **默认安装路径**（`scripts/` 目录）
3. **配置文件所在目录**

**重要**：一旦找到文件，搜索就会停止。

## 常见问题

### 问题 1：找不到配置文件

**错误信息**：
```
Error: unable to find target/nordic/nrf54l.cfg
```

**原因**：搜索路径中没有包含配置文件所在的目录。

**解决方案**：
```bash
# 使用 -s 参数指定搜索路径
openocd -s /path/to/openocd/directory -f openocd-54.cfg
```

### 问题 2：找到错误的配置文件

**原因**：多个搜索路径中存在同名文件，OpenOCD 使用了第一个找到的文件。

**解决方案**：
- 检查搜索路径顺序
- 使用更具体的路径
- 或者使用绝对路径

### 问题 3：Zephyr/West 自动处理

当使用 Zephyr 的 `west flash` 或 `west debug` 命令时，Zephyr 会自动添加搜索路径：

```python
# zephyr/scripts/west_commands/runners/openocd.py
search_args = []
if path.exists(support):
    search_args.append('-s')
    search_args.append(support)  # boards/xxx/support 目录

if self.openocd_config is not None:
    for i in self.openocd_config:
        if path.exists(i):
            search_args.append('-s')
            search_args.append(path.dirname(i))  # 配置文件所在目录
```

## 最佳实践

### 1. 项目结构组织

```
project/
├── openocd/
│   ├── openocd.cfg          # 主配置
│   ├── board/               # 板级配置
│   └── target/              # 目标芯片配置
│       └── vendor/
│           └── chip.cfg
```

### 2. 运行命令

```bash
# 从项目根目录运行
openocd -s openocd -f openocd/openocd.cfg

# 或从 openocd 目录运行
cd openocd
openocd -s . -f openocd.cfg
```

### 3. 使用相对路径

在配置文件中，使用相对于搜索路径的路径：

```tcl
# ✅ 正确：相对于搜索路径
source [find target/nordic/nrf54l.cfg]

# ❌ 错误：绝对路径（不推荐）
source /path/to/target/nordic/nrf54l.cfg
```

## 验证搜索路径

可以使用以下方法验证 OpenOCD 的搜索路径：

```bash
# 查看 OpenOCD 版本和默认路径
openocd --version

# 使用调试模式查看详细信息
openocd -d3 -s /path/to/scripts -f config.cfg
```

## 总结

- `[find filename]` 命令在多个搜索路径中查找文件
- 使用 `-s` 参数可以添加自定义搜索路径
- 搜索顺序：`-s` 路径 → 默认路径 → 配置文件目录
- 项目中的自定义配置文件应该通过 `-s` 参数指定搜索路径
- Zephyr/West 会自动处理搜索路径，手动运行时需要手动指定

---

## 参考

- [OpenOCD 用户指南](http://openocd.org/doc/html/index.html)
- [OpenOCD 配置脚本语法](http://openocd.org/doc/html/Config-File-Syntax.html)
- Zephyr OpenOCD Runner: `zephyr/scripts/west_commands/runners/openocd.py`
