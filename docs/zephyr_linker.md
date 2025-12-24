# Zephyr 链接器脚本分析

本文档详细分析构建 `zephyr.elf` 使用的链接器脚本 (`linker.cmd`) 的结构和内容。

## 文件位置

- **最终链接器脚本**：`build/hello_world/zephyr/linker.cmd`
- **第一次链接脚本**：`build/hello_world/zephyr/linker_zephyr_pre0.cmd`

## 链接器脚本结构

### 1. 基本配置

```1:10:linker.cmd
OUTPUT_FORMAT("elf32-littlearm")
_region_min_align = 32;
MEMORY
    {
    FLASH (rx) : ORIGIN = 0x0, LENGTH = 0x100000
    RAM (wx) : ORIGIN = 0x20000000, LENGTH = 0x40000
   
    IDT_LIST (wx) : ORIGIN = 0xFFFF7FFF, LENGTH = 32K
    }
ENTRY("__start")
```

**说明**：
- `OUTPUT_FORMAT`: 输出格式为 32 位小端 ARM ELF
- `_region_min_align`: 最小对齐为 32 字节
- `MEMORY`: 定义三个内存区域
  - **FLASH**: 0x0 - 0x100000 (1MB)，只读可执行
  - **RAM**: 0x20000000 - 0x20040000 (256KB)，可读写可执行
  - **IDT_LIST**: 0xFFFF7FFF - 0xFFFFFFFF (32KB)，中断描述符表
- `ENTRY("__start")`: 程序入口点为 `__start` 符号

---

### 2. 段定义 (SECTIONS)

链接器脚本定义了多个段，按照在 Flash 和 RAM 中的布局顺序排列。

#### 2.1 Flash 区域段

##### 2.1.1 中断向量表 (`rom_start`)

```44:61:linker.cmd
__rom_region_start = 0x0;
    rom_start :
{
HIDDEN(__rom_start_address = .);
FILL(0x00);
. += 0x0 - (. - __rom_start_address);
. = ALIGN(4);
. = ALIGN( 1 << LOG2CEIL(4 * 32) );
. = ALIGN( 1 << LOG2CEIL(4 * (16 + 48)) );
_vector_start = .;
KEEP(*(.exc_vector_table))
KEEP(*(".exc_vector_table.*"))
KEEP(*(.vectors))
_vector_end = .;
. = ALIGN(4);
KEEP(*(.gnu.linkonce.irq_vector_table*))
 _vector_end = .;
 } > FLASH
```

**作用**：
- 定义中断向量表的位置
- 从 Flash 地址 0x0 开始
- 包含异常向量表和中断向量表

##### 2.1.2 代码段 (`text`)

```62:72:linker.cmd
    text :
    {
     __text_region_start = .;
     *(.text)
     *(".text.*")
     *(".TEXT.*")
     *(.gnu.linkonce.t.*)
     *(.glue_7t) *(.glue_7) *(.vfp11_veneer) *(.v4_bx)
     . = ALIGN(4);
     } > FLASH
     __text_region_end = .;
```

**作用**：
- 包含所有可执行代码
- 包括主程序、库函数、中断处理程序等

##### 2.1.3 ARM 异常索引表 (`.ARM.exidx`)

```73:78:linker.cmd
 .ARM.exidx :
 {
  __exidx_start = .;
  *(.ARM.exidx* gnu.linkonce.armexidx.*)
  __exidx_end = .;
 } > FLASH
```

**作用**：
- ARM 异常展开信息
- 用于 C++ 异常处理和栈展开

##### 2.1.4 初始化级别段 (`initlevel`)

```80:90:linker.cmd
 initlevel :
 {
  __init_start = .;
  __init_EARLY_start = .; KEEP(*(SORT(.z_init_EARLY_P_?_*))); KEEP(*(SORT(.z_init_EARLY_P_??_*))); KEEP(*(SORT(.z_init_EARLY_P_???_*)));
  __init_PRE_KERNEL_1_start = .; KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_?_*))); KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_??_*))); KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_???_*)));
  __init_PRE_KERNEL_2_start = .; KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_?_*))); KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_??_*))); KEEP(*(SORT(.z_init_PRE_KERNEL_2_P_???_*)));
  __init_POST_KERNEL_start = .; KEEP(*(SORT(.z_init_POST_KERNEL_P_?_*))); KEEP(*(SORT(.z_init_POST_KERNEL_P_??_*))); KEEP(*(SORT(.z_init_POST_KERNEL_P_???_*)));
  __init_APPLICATION_start = .; KEEP(*(SORT(.z_init_APPLICATION_P_?_*))); KEEP(*(SORT(.z_init_APPLICATION_P_??_*))); KEEP(*(SORT(.z_init_APPLICATION_P_???_*)));
  __init_SMP_start = .; KEEP(*(SORT(.z_init_SMP_P_?_*))); KEEP(*(SORT(.z_init_SMP_P_??_*))); KEEP(*(SORT(.z_init_SMP_P_???_*)));
  __init_end = .;
 } > FLASH
```

**作用**：
- 定义 Zephyr 内核的初始化级别
- 按顺序排列：EARLY → PRE_KERNEL_1 → PRE_KERNEL_2 → POST_KERNEL → APPLICATION → SMP
- 每个级别的初始化函数按优先级排序

##### 2.1.5 设备列表 (`device_area`)

```91:91:linker.cmd
 device_area : SUBALIGN(4) { _device_list_start = .; KEEP(*(SORT(._device.static.*_?_*))); KEEP(*(SORT(._device.static.*_??_*))); KEEP(*(SORT(._device.static.*_???_*))); KEEP(*(SORT(._device.static.*_????_*))); KEEP(*(SORT(._device.static.*_?????_*))); _device_list_end = .;; } > FLASH
```

**作用**：
- 存储设备树中定义的设备结构体
- 用于设备驱动初始化和查找

##### 2.1.6 软件 ISR 表 (`sw_isr_table`)

```92:96:linker.cmd
 sw_isr_table :
 {
  . = ALIGN(4);
  *(.gnu.linkonce.sw_isr_table*)
 } > FLASH
```

**作用**：
- 软件中断服务例程表
- 由 `gen_isr_tables.py` 生成

##### 2.1.7 驱动 API 区域

```116:197:linker.cmd
gpio_driver_api_area : SUBALIGN(4) { _gpio_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._gpio_driver_api.static.*))); _gpio_driver_api_list_end = .;; } > FLASH
shared_irq_driver_api_area : SUBALIGN(4) { _shared_irq_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._shared_irq_driver_api.static.*))); _shared_irq_driver_api_list_end = .;; } > FLASH
crypto_driver_api_area : SUBALIGN(4) { _crypto_driver_api_list_start = .; KEEP(*(SORT_BY_NAME(._crypto_driver_api.static.*))); _crypto_driver_api_list_end = .;; } > FLASH
...
```

**作用**：
- 存储各种驱动的 API 函数指针
- 包括 GPIO、UART、SPI、I2C 等所有驱动类型
- 用于驱动框架查找和调用驱动函数

##### 2.1.8 只读数据段 (`rodata`)

```259:265:linker.cmd
    rodata :
{
 *(.rodata)
 *(".rodata.*")
 *(.gnu.linkonce.r.*)
 . = ALIGN(4);
 } > FLASH
```

**作用**：
- 包含所有只读数据
- 如字符串常量、常量数组等

---

#### 2.2 RAM 区域段

##### 2.2.1 RAM 函数段 (`.ramfunc`)

```281:293:linker.cmd
.ramfunc : ALIGN_WITH_INPUT
{
 __ramfunc_region_start = .;
 . = ALIGN(_region_min_align); . = ALIGN( 1 << LOG2CEIL(__ramfunc_size));
 __ramfunc_start = .;
 *(.ramfunc)
 *(".ramfunc.*")
 . = ALIGN(_region_min_align); . = ALIGN( 1 << LOG2CEIL(__ramfunc_size));
 __ramfunc_end = .;
} > RAM AT > FLASH
__ramfunc_size = __ramfunc_end - __ramfunc_start;
__ramfunc_load_start = LOADADDR(.ramfunc);
```

**作用**：
- 需要在 RAM 中执行的函数（如 Flash 操作函数）
- 使用 `AT > FLASH` 表示代码存储在 Flash，运行时复制到 RAM

##### 2.2.2 数据段 (`datas`)

```294:305:linker.cmd
    datas : ALIGN_WITH_INPUT
 {
 __data_region_start = .;
 __data_start = .;
 *(.data)
 *(".data.*")
 *(".kernel.*")
 __data_end = .;
 } > RAM AT > FLASH
    __data_size = __data_end - __data_start;
    __data_load_start = LOADADDR(datas);
    __data_region_load_start = LOADADDR(datas);
```

**作用**：
- 初始化的全局变量和静态变量
- 存储在 Flash，启动时复制到 RAM

##### 2.2.3 设备状态 (`device_states`)

```306:314:linker.cmd
        device_states : ALIGN_WITH_INPUT
        {
  . = ALIGN(4);
                __device_states_start = .;
  KEEP(*(".z_devstate"));
  KEEP(*(".z_devstate.*"));
                __device_states_end = .;
  . = ALIGN(4);
        } > RAM AT > FLASH
```

**作用**：
- 设备驱动状态结构体
- 每个设备有一个状态结构体

##### 2.2.4 内核对象区域

```315:333:linker.cmd
 log_mpsc_pbuf_area : ALIGN_WITH_INPUT { _log_mpsc_pbuf_list_start = .; *(SORT_BY_NAME(._log_mpsc_pbuf.static.*)); _log_mpsc_pbuf_list_end = .;; } > RAM AT > FLASH
 log_msg_ptr_area : ALIGN_WITH_INPUT { _log_msg_ptr_list_start = .; KEEP(*(SORT_BY_NAME(._log_msg_ptr.static.*))); _log_msg_ptr_list_end = .;; } > RAM AT > FLASH
 log_dynamic_area : ALIGN_WITH_INPUT { _log_dynamic_list_start = .; KEEP(*(SORT_BY_NAME(._log_dynamic.static.*))); _log_dynamic_list_end = .;; } > RAM AT > FLASH
 k_timer_area : ALIGN_WITH_INPUT { _k_timer_list_start = .; *(SORT_BY_NAME(._k_timer.static.*)); _k_timer_list_end = .;; } > RAM AT > FLASH
 k_mem_slab_area : ALIGN_WITH_INPUT { _k_mem_slab_list_start = .; *(SORT_BY_NAME(._k_mem_slab.static.*)); _k_mem_slab_list_end = .;; } > RAM AT > FLASH
 k_heap_area : ALIGN_WITH_INPUT { _k_heap_list_start = .; *(SORT_BY_NAME(._k_heap.static.*)); _k_heap_list_end = .;; } > RAM AT > FLASH
 k_mutex_area : ALIGN_WITH_INPUT { _k_mutex_list_start = .; *(SORT_BY_NAME(._k_mutex.static.*)); _k_mutex_list_end = .;; } > RAM AT > FLASH
 k_stack_area : ALIGN_WITH_INPUT { _k_stack_list_start = .; *(SORT_BY_NAME(._k_stack.static.*)); _k_stack_list_end = .;; } > RAM AT > FLASH
 k_msgq_area : ALIGN_WITH_INPUT { _k_msgq_list_start = .; *(SORT_BY_NAME(._k_msgq.static.*)); _k_msgq_list_end = .;; } > RAM AT > FLASH
 k_mbox_area : ALIGN_WITH_INPUT { _k_mbox_list_start = .; *(SORT_BY_NAME(._k_mbox.static.*)); _k_mbox_list_end = .;; } > RAM AT > FLASH
 k_pipe_area : ALIGN_WITH_INPUT { _k_pipe_list_start = .; *(SORT_BY_NAME(._k_pipe.static.*)); _k_pipe_list_end = .;; } > RAM AT > FLASH
 k_sem_area : ALIGN_WITH_INPUT { _k_sem_list_start = .; *(SORT_BY_NAME(._k_sem.static.*)); _k_sem_list_end = .;; } > RAM AT > FLASH
 k_event_area : ALIGN_WITH_INPUT { _k_event_list_start = .; *(SORT_BY_NAME(._k_event.static.*)); _k_event_list_end = .;; } > RAM AT > FLASH
 k_queue_area : ALIGN_WITH_INPUT { _k_queue_list_start = .; *(SORT_BY_NAME(._k_queue.static.*)); _k_queue_list_end = .;; } > RAM AT > FLASH
 k_fifo_area : ALIGN_WITH_INPUT { _k_fifo_list_start = .; *(SORT_BY_NAME(._k_fifo.static.*)); _k_fifo_list_end = .;; } > RAM AT > FLASH
 k_lifo_area : ALIGN_WITH_INPUT { _k_lifo_list_start = .; *(SORT_BY_NAME(._k_lifo.static.*)); _k_lifo_list_end = .;; } > RAM AT > FLASH
 k_condvar_area : ALIGN_WITH_INPUT { _k_condvar_list_start = .; *(SORT_BY_NAME(._k_condvar.static.*)); _k_condvar_list_end = .;; } > RAM AT > FLASH
 sys_mem_blocks_ptr_area : ALIGN_WITH_INPUT { _sys_mem_blocks_ptr_list_start = .; *(SORT_BY_NAME(._sys_mem_blocks_ptr.static.*)); _sys_mem_blocks_ptr_list_end = .;; } > RAM AT > FLASH
 net_buf_pool_area : ALIGN_WITH_INPUT { _net_buf_pool_list_start = .; KEEP(*(SORT_BY_NAME(._net_buf_pool.static.*))); _net_buf_pool_list_end = .;; } > RAM AT > FLASH
```

**作用**：
- 存储所有内核对象（信号量、互斥锁、队列、定时器等）的列表
- 用于内核对象管理和查找

##### 2.2.5 BSS 段 (`bss`)

```388:398:linker.cmd
    bss (NOLOAD) : ALIGN_WITH_INPUT
 {
        . = ALIGN(4);
 __bss_start = .;
 __kernel_ram_start = .;
 *(.bss)
 *(".bss.*")
 *(COMMON)
 *(".kernel_bss.*")
 __bss_end = ALIGN(4);
 } > RAM AT > RAM
```

**作用**：
- 未初始化的全局变量和静态变量
- `NOLOAD` 表示不加载到 Flash，启动时清零即可

##### 2.2.6 NOINIT 段 (`noinit`)

```399:403:linker.cmd
noinit (NOLOAD) :
{
        *(.noinit)
        *(".noinit.*")
} > RAM AT > RAM
```

**作用**：
- 不需要初始化的变量
- 保持 RAM 中的随机值（用于保持状态）

---

### 3. 内存布局总结

```
Flash (0x00000000 - 0x00100000, 1MB)
├── 0x00000000: 中断向量表 (rom_start)
├── 0x00000040: 代码段 (text)
├── 0x0000xxxx: ARM 异常索引表 (.ARM.exidx)
├── 0x0000xxxx: 初始化级别段 (initlevel)
├── 0x0000xxxx: 设备列表 (device_area)
├── 0x0000xxxx: 软件 ISR 表 (sw_isr_table)
├── 0x0000xxxx: 驱动 API 区域 (各种 driver_api_area)
├── 0x0000xxxx: 只读数据 (rodata)
└── 0x0000xxxx: 数据段镜像 (datas, device_states, 内核对象等)

RAM (0x20000000 - 0x20040000, 256KB)
├── 0x20000000: RAM 函数 (ramfunc)
├── 0x2000xxxx: 数据段 (datas) - 从 Flash 复制
├── 0x2000xxxx: 设备状态 (device_states) - 从 Flash 复制
├── 0x2000xxxx: 内核对象 - 从 Flash 复制
├── 0x2000xxxx: BSS 段 (bss) - 清零
└── 0x2000xxxx: NOI