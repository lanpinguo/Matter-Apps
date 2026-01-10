#!/bin/bash

# debug-mcuboot.sh - MCUboot + Application 调试脚本

MCUBOOT_ELF="build/mcuboot/zephyr/zephyr.elf"
APP_ELF="build/light_bulb_mote/zephyr/zephyr.elf"
GDB="/opt/nordic/ncs/toolchains/561dce9adf/opt/zephyr-sdk/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb"
DEBUGGER="${1:-openocd}"

# 检查文件是否存在
if [ ! -f "$MCUBOOT_ELF" ]; then
    echo "错误: MCUboot ELF 文件不存在: $MCUBOOT_ELF"
    echo "请先构建项目: west build -b nrf52840dk/nrf52840"
    exit 1
fi

if [ ! -f "$APP_ELF" ]; then
    echo "错误: 应用程序 ELF 文件不存在: $APP_ELF"
    echo "请先构建项目: west build -b nrf52840dk/nrf52840"
    exit 1
fi

echo "=========================================="
echo "MCUboot + Application 调试会话"
echo "=========================================="
echo "MCUboot ELF: $MCUBOOT_ELF (地址: 0x0)"
echo "Application ELF: $APP_ELF (地址: 0xC200)"
echo "调试器: $DEBUGGER"
echo ""

case "$DEBUGGER" in
    openocd)
        echo "请确保 OpenOCD 正在运行:"
        echo "  openocd -f openocd.cfg"
        echo ""
        echo "按 Enter 继续启动 GDB..."
        read
        
        $GDB -ex "target remote localhost:3333" \
             -ex "add-symbol-file $MCUBOOT_ELF 0x0" \
             -ex "add-symbol-file $APP_ELF 0xC200" \
             -ex "break main" \
             -ex "break boot_go" \
             -ex "break AppTask::StartApp" \
             -ex "break gpio_pin_configure" \
             -ex "monitor reset halt" \
             -ex "set listsize 20" \
             -ex "set print pretty on" \
             $APP_ELF
        ;;
    
    jlink)
        echo "启动 J-Link GDB Server..."
        echo "按 Ctrl+C 停止"
        echo ""
        
        if command -v JLinkGDBServer &> /dev/null; then
            JLinkGDBServer -device nRF52840_xxAA -if SWD -speed 4000 -port 2331 &
            JLINK_PID=$!
            sleep 2
            
            echo "J-Link GDB Server 已启动 (PID: $JLINK_PID)"
            echo "按 Enter 启动 GDB..."
            read
            
            $GDB -ex "target remote localhost:2331" \
                 -ex "add-symbol-file $MCUBOOT_ELF 0x0" \
                 -ex "add-symbol-file $APP_ELF 0xC200" \
                 -ex "break main" \
                 -ex "break boot_go" \
                 -ex "break AppTask::StartApp" \
                 -ex "break gpio_pin_configure" \
                 -ex "monitor reset halt" \
                 -ex "set listsize 20" \
                 -ex "set print pretty on" \
                 $APP_ELF
            
            kill $JLINK_PID 2>/dev/null || true
        else
            echo "错误: JLinkGDBServer 未找到"
            echo "请安装 SEGGER J-Link 软件包"
            exit 1
        fi
        ;;
    
    *)
        echo "用法: $0 [openocd|jlink]"
        echo ""
        echo "示例:"
        echo "  $0 openocd    # 使用 OpenOCD (需要先启动 OpenOCD)"
        echo "  $0 jlink     # 使用 J-Link (自动启动 GDB Server)"
        exit 1
        ;;
esac

