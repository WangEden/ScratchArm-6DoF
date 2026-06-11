# Ubuntu 22 下构建 STM32H723 固件 — 环境配置笔记

> 项目原本为 Windows + Keil5 (AC6) 开发，以下步骤使其在 Ubuntu 22 下通过 EIDE + GCC 工具链编译和烧录。

---

## 一、安装 ARM GCC 交叉编译工具链

从 ARM 官网下载 `arm-gnu-toolchain-13.2.Rel1`（注意大写 **R**）：

```bash
# 解压到 /usr/local
sudo tar xf arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi.tar.xz -C /usr/local/

# 添加到 PATH（注意目录名大小写：Rel1，不是 rel1）
echo 'export PATH="/usr/local/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# 验证
arm-none-eabi-gcc --version
# 应输出：arm-none-eabi-gcc (Arm GNU Toolchain 13.2.rel1 ...) 13.2.1
```

---

## 二、安装 .NET 运行时（EIDE 构建依赖）

EIDE 的构建工具 `unify_builder` 是 .NET 程序，需要 dotnet 运行时：

```bash
# 方法一：Ubuntu 22.04 自带源可直接安装
sudo apt install -y dotnet-runtime-8.0

# 方法二：如果找不到包，先添加 Microsoft 源
wget https://packages.microsoft.com/config/ubuntu/22.04/packages-microsoft-prod.deb -O packages-microsoft-prod.deb
sudo dpkg -i packages-microsoft-prod.deb
rm packages-microsoft-prod.deb
sudo apt update
sudo apt install -y dotnet-runtime-8.0

# 验证
dotnet --version
```

---

## 三、从 STM32Cube 官方仓库获取 GCC 版文件

ST 官方仓库里 **没有单独的 `GCC/` 文件夹**，GCC 格式的文件都放在 `STM32CubeIDE/` 目录下（因为 STM32CubeIDE 就用 GCC 工具链）。

### 3.1 GCC 启动文件

**注意：不能用 `EWARM/` 下的文件（那是 IAR 格式）！**

| 目录 | 工具链 | 语法特征 | 是否可用 |
|------|--------|---------|---------|
| `EWARM/` | IAR | `MODULE`, `SECTION CSTACK`, `__iar_program_start` | ❌ |
| `MDK-ARM/` | Keil/ARMCC | Keil 汇编语法 | ❌ |
| `STM32CubeIDE/Application/Startup/` | GCC | `.syntax unified`, `.cpu cortex-m7`, `.thumb` | ✅ |

```bash
# 从 STM32CubeH7 仓库下载 GCC 启动文件
curl -sL \
  "https://raw.githubusercontent.com/STMicroelectronics/STM32CubeH7/master/Projects/NUCLEO-H723ZG/Applications/FreeRTOS/FreeRTOS_Semaphore/STM32CubeIDE/Application/Startup/startup_stm32h723zgtx.s" \
  -o GCC/startup_stm32h723xx.s
```

**注意：** 该启动文件里有一行 `bl ExitRun0Mode`，如果项目没有此函数需要删除，否则链接报 `undefined reference`：

```asm
// 删除这两行
/* Call the ExitRun0Mode function to configure the power supply */
  bl  ExitRun0Mode
```

### 3.2 GCC 链接脚本（.ld 文件）

同样从 STM32CubeIDE 模板项目获取：

```bash
# 来源：STM32CubeH7 仓库的 STM32CubeIDE 模板项目
# 文件：STM32H723ZGTX_FLASH.ld
```

放到 `GCC/STM32H723ZGTX_FLASH.ld`。

建议在 MEMORY 块中补上 AXI SRAM（默认模板可能只有 DTCMRAM）：

```ld
MEMORY
{
  ITCMRAM    (xrw)    : ORIGIN = 0x00000000,   LENGTH = 64K
  DTCMRAM    (xrw)    : ORIGIN = 0x20000000,   LENGTH = 128K
  RAM_D1     (xrw)    : ORIGIN = 0x24000000,   LENGTH = 320K
  ROM    (rx)    : ORIGIN = 0x08000000,   LENGTH = 1024K
}
```

如果改了 `RAM` 为 `DTCMRAM`，需同步更新所有 `>RAM` 引用为 `>DTCMRAM`。

### 3.3 FreeRTOS GCC port 文件

需要确保 GCC 版 FreeRTOS port 文件存在：

```
Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/
├── port.c
└── portmacro.h
```

如果缺失，从 FreeRTOS 官方仓库获取：
```bash
# FreeRTOS Kernel 仓库
https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/main/portable/GCC/ARM_CM4F
```

---

## 四、修改 EIDE 配置（`.eide/eide.yml`）

共需修改 5 处：

### 4.1 启动文件路径

```yaml
# 旧：Keil 启动文件
- path: MDK-ARM/startup_stm32h723xx_keil.s
# 新：GCC 启动文件
- path: GCC/startup_stm32h723xx.s
```

### 4.2 FreeRTOS port 源文件

```yaml
# 旧：RVDS (ARM Compiler)
- path: Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM4F/port.c
# 新：GCC
- path: Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
```

### 4.3 FreeRTOS include 路径

```yaml
# 旧
- Middlewares/Third_Party/FreeRTOS/Source/portable/RVDS/ARM_CM4F
# 新
- Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
```

### 4.4 切换 toolchain

```yaml
# 旧
toolchain: AC6
# 新
toolchain: GCC
```

### 4.5 GCC linker script 路径

```yaml
# 旧（Keil scatter file）
scatterFilePath: <YOUR_LINKER_SCRIPT>.lds
# 新（GCC linker script）
scatterFilePath: GCC/STM32H723ZGTX_FLASH.ld
```

---

## 五、修复代码兼容性问题

### 5.1 `__packed` 关键字（ARM Compiler 专用）

GCC 不支持 `__packed`，需替换为 `__attribute__((packed))`：

```c
// 旧（AC6）
typedef __packed struct { ... } my_struct_t;

// 新（GCC）
typedef struct __attribute__((packed)) { ... } my_struct_t;
```

涉及文件：
- `Algorithm/inc/user_lib.h` — 2 处
- `Bsp/inc/BMI088driver.h` — 1 处

### 5.2 `#include` 大小写问题

Windows 文件系统不区分大小写，Linux 区分。需要确保 `#include` 的文件名与实际文件名完全一致：

```c
// 旧（Windows 下能跑，Linux 找不到）
#include "bmi088driver.h"

// 新（实际文件名是 BMI088driver.h）
#include "BMI088driver.h"
```

涉及文件：
- `App/imu_task.c` — `bmi088driver.h` → `BMI088driver.h`

> **建议：** 全项目搜索所有 `#include "..."`，对比实际头文件名，确认大小写一致。

---

## 六、STLink 烧录配置

### 6.1 安装 STLink udev 规则

```bash
sudo wget -O /etc/udev/rules.d/49-stlinkv2-1.rules \
  https://raw.githubusercontent.com/stlink-org/stlink/master/config/udev/rules.d/49-stlinkv2-1.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

然后**拔插 STLink USB**，让 udev 规则生效。

### 6.2 安装烧录工具（二选一）

#### 方式 A：stlink-tools（开源，快速）

```bash
sudo apt install stlink-tools

# 验证连接
st-info --probe

# 烧录（需先生成 .bin 文件）
arm-none-eabi-objcopy -O binary build/CtrBoard-H7_ALL/CtrBoard-H7_ALL.elf build/CtrBoard-H7_ALL/CtrBoard-H7_ALL.bin
st-flash write build/CtrBoard-H7_ALL/CtrBoard-H7_ALL.bin 0x08000000
```

#### 方式 B：STM32CubeProgrammer（官方，支持 EIDE 集成）

```bash
# 从 ST 官网下载（需注册免费账号）
# https://www.st.com/en/development-tools/stm32cubeprog.html

# 安装后加入 PATH
echo 'export PATH="$HOME/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# 验证
STM32_Programmer_CLI --version
```

### 6.3 EIDE 烧录配置

在 `.eide/eide.yml` 中确认以下配置：

```yaml
uploadConfigMap:
  STLink:
    address: "0x08000000"
    elFile: build/CtrBoard-H7_ALL/CtrBoard-H7_ALL.elf   # 指向构建输出的 .elf
    proType: SWD
    speed: 4000
    runAfterProgram: true
uploader: STLink
```

在 VSCode 中点击底部状态栏 **⚡ Flash Device** 即可烧录。

---

## 七、构建与验证

```bash
# 1. 确认工具链在 PATH 中
arm-none-eabi-gcc --version

# 2. 在 VSCode 中点击 EIDE 的 Build 按钮
#    或终端运行：
dotnet ~/.vscode/extensions/cl.eide-<版本>/res/tools/linux/unify_builder/unify_builder.dll \
  -p build/CtrBoard-H7_ALL/builder.params --rebuild

# 3. 检查构建输出
ls -la build/CtrBoard-H7_ALL/CtrBoard-H7_ALL.{elf,hex,bin}
```

---

## 踩坑记录

| 问题 | 原因 | 解决 |
|------|------|------|
| `arm-none-eabi-gcc not found` | PATH 里目录名大小写错误（rel1 vs Rel1） | 修正为 `Rel1` |
| `dotnet: 未找到命令` | EIDE 构建需要 .NET 运行时 | `apt install dotnet-runtime-8.0` |
| `__packed` 编译错误 | ARM Compiler 专有关键字 | 改为 `__attribute__((packed))` |
| `bmi088driver.h: No such file` | Linux 区分大小写 | 改为 `BMI088driver.h` |
| `undefined reference to ExitRun0Mode` | CubeIDE 启动模板新增的函数 | 删除 startup 中的 `bl ExitRun0Mode` |
| `Not found ST-LINK_CLI` | 缺少烧录工具 | 安装 stlink-tools 或 STM32CubeProgrammer |
