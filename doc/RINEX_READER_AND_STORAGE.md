# RINEX 文件读取与数据存储说明

本文说明本仓库当前 `master` 分支中 RINEX 文件的读取流程、主要数据结构以及 RINEX 4 NAV 数据的存储方式。

> 代码以 `src/rinex.c` 和 `src/rtklib.h` 为准。本文重点描述“读取后数据存到哪里”，不等同于声明所有已存储字段都已经被 RTKLIB 原有定位模型使用。

## 1. 适用范围

当前代码保留 RTKLIB 原有的 RINEX 2/3 读取路径，并增加了独立的 RINEX 4 NAV 读取路径。

- RINEX OBS：继续使用原有 `readrnxobs()` / `readrnxobsb()` 路径。
- RINEX NAV，版本 `< 4.0`：使用 `readrnxnavb()`。
- RINEX NAV，版本 `>= 4.0`：使用 `readrnx4navb()`。
- RINEX CLK：使用 `readrnxclk()`。

RINEX 4 的专用扩展目前主要集中在 NAV 文件。OBS 文件没有独立的 `readrnx4obs*()` 实现，因此本文不把“完整 RINEX 4 OBS 支持”作为本仓库已验证能力。

## 2. 对外读取接口

最常用入口为：

```c
extern int readrnx(const char *file, int rcv, const char *opt,
                   obs_t *obs, nav_t *nav, sta_t *sta);
```

它最终调用：

```text
readrnx()
  -> readrnxt()
     -> readrnxfile()
        -> readrnxfp()
           -> readrnxh()       # 读取 header
           -> readrnxobs()     # O: Observation
           -> readrnxnav()     # N/G/H/J/L: Navigation
           -> readrnxclk()     # C: Clock
```

`readrnxt()` 还支持：

- 通配符文件名展开；
- 起止时间筛选；
- 采样间隔筛选；
- `-SYS=` 系统筛选；
- 各系统观测信号选择与 phase shift 选项。

例如 `-SYS=G,E,C` 表示只读取 GPS、Galileo、BeiDou。

## 3. RINEX 文件类型分流

`readrnxfp()` 读取 header 后，根据 RINEX file type 分流：

| type | 含义 | 读取函数 |
|---|---|---|
| `O` | Observation | `readrnxobs()` |
| `N` | Navigation | `readrnxnav()` |
| `G` | GLONASS NAV | `readrnxnav(..., SYS_GLO, ...)` |
| `H` | SBAS NAV | `readrnxnav(..., SYS_SBS, ...)` |
| `J` | QZSS NAV | `readrnxnav(..., SYS_QZS, ...)` |
| `L` | Galileo NAV | `readrnxnav(..., SYS_GAL, ...)` |
| `C` | Clock | `readrnxclk()` |

## 4. Observation 数据存储

### 4.1 `obs_t`

所有观测记录最终存入：

```c
typedef struct {
    int n,nmax;
    obsd_t *data;
} obs_t;
```

每个历元/卫星记录使用 `obsd_t`：

```c
typedef struct {
    gtime_t time;
    unsigned char sat,rcv;
    unsigned char SNR [NFREQ+NEXOBS];
    unsigned char LLI [NFREQ+NEXOBS];
    unsigned char code[NFREQ+NEXOBS];
    double L[NFREQ+NEXOBS];
    double P[NFREQ+NEXOBS];
    float  D[NFREQ+NEXOBS];
} obsd_t;
```

字段含义：

- `time`：观测时间，内部使用 GPST；
- `sat`：RTKLIB 内部卫星编号；
- `rcv`：接收机编号；
- `P[]`：伪距，单位 m；
- `L[]`：载波相位，单位 cycle；
- `D[]`：多普勒，单位 Hz；
- `SNR[]`：信号强度，单位为 0.25 dB-Hz；
- `LLI[]`：loss-of-lock / half-cycle 等标志；
- `code[]`：RTKLIB `CODE_???` 观测码编号。

默认：

```c
#define NFREQ  3
#define NEXOBS 0
```

因此默认每颗卫星存储 3 个主频槽位。编译时可以覆盖 `NFREQ` / `NEXOBS`。

### 4.2 OBS 解析流程

```text
readrnxobs()
  -> readrnxobsb()
     -> decode_obsepoch()
     -> decode_obsdata()
     -> obsd_t
  -> addobsdata()
  -> obs_t.data[]
```

信号到数组槽位的映射由 `set_index()` 根据 RINEX 观测类型、系统、优先级以及 `NFREQ/NEXOBS` 决定。

## 5. Navigation 数据总容器 `nav_t`

广播导航数据统一挂在 `nav_t` 下，但不同系统使用不同子结构：

```c
typedef struct {
    int n,nmax;
    int ng,ngmax;
    int ns,nsmax;
    ...
    int nion,nionmax;
    int neop,neopmax;
    int nsto,nstomax;

    eph_t  *eph;
    geph_t *geph;
    seph_t *seph;
    ...
    ion_t *ion;
    eop_t *eop;
    sto_t *sto;
} nav_t;
```

主要存储关系：

| 数据 | 结构 | `nav_t` 成员 |
|---|---|---|
| GPS/QZSS/Galileo/BeiDou/NavIC 广播星历 | `eph_t` | `nav.eph[]` |
| GLONASS 广播星历 | `geph_t` | `nav.geph[]` |
| SBAS 广播星历 | `seph_t` | `nav.seph[]` |
| RINEX 4 ION record | `ion_t` | `nav.ion[]` |
| RINEX 4 EOP record | `eop_t` | `nav.eop[]` |
| RINEX 4 STO record | `sto_t` | `nav.sto[]` |
| precise ephemeris | `peph_t` | `nav.peph[]` |
| precise clock | `pclk_t` | `nav.pclk[]` |

`rtklib.h` 中 `eph_t *eph` 的旧注释仍写为 GPS/QZS/GAL，但当前 `decode_rnx4_eph()` 实际也将 BeiDou 和 NavIC 放入 `eph_t`。

## 6. RINEX 2/3 NAV 读取

版本 `< 4.0` 时：

```text
readrnxnav()
  -> readrnxnavb()
     -> decode_eph()   # GPS/GAL/QZS/BDS
     -> decode_geph()  # GLONASS
     -> decode_seph()  # SBAS
  -> add_eph()/add_geph()/add_seph()
  -> nav_t
```

传统星历使用 RTKLIB 原有的固定行格式解析。

## 7. RINEX 4 NAV 读取

版本 `>= 4.0` 时：

```text
readrnxnav()
  -> readrnx4navb()
     -> decode_record_hdr()
     -> 根据 record type 分流
        -> EPH -> readrnx4ephbody() -> decode_rnx4_eph()/decode_geph()/decode_seph()
        -> ION -> readionbody()
        -> EOP -> readeopbody()
        -> STO -> readstobody()
  -> add_eph()/add_geph()/add_seph()/add_ion()/add_eop()/add_sto()
  -> nav_t
```

### 7.1 RINEX 4 record header

RINEX 4 每个 NAV record 的 header 被保存为：

```c
typedef struct {
    int data_type;
    int sys;
    int prn;
    int msg_type;
    char subtype[5];
} nav_data_hdr_t;
```

该 header 会附加到 `eph_t`、`geph_t`、`seph_t`、`ion_t`、`sto_t`、`eop_t` 中。

这非常重要，因为 RINEX 4 同一颗卫星可以有不同的 navigation message type，同一数组槽位在不同 message type 下可能具有不同物理意义。

对于适用的传统 EPH 消息，`eph_t.sva` 遵循编译时 `URA2URAI` 约定：
`URA2URAI=0` 时保留 RINEX 中以米表示的精度值，`URA2URAI=1` 时转换为
RTKLIB 的 URA index。GPS/QZSS 现代 `CNAV`/`CNV2` 是例外：其 accuracy
字段是 raw URAI 分量，不是 metric SVA；decoder 保留 `urai_ned[]` 和
`urai_ed`，并将传统 `eph_t.sva` 槽设为负的 unknown sentinel。该例外不套用
到 BDS、传统 GPS/QZSS、GLONASS 或 SBAS。当前 shared build 固定使用
`URA2URAI=0`；`URA2URAI=1` 不是当前 shared contract 的 supported/tested
配置。

### 7.2 支持识别的 record type

当前 `read_nav_data_type()` 识别：

```text
EPH
STO
ION
EOP
```

### 7.3 支持识别的 message type

当前 `read_nav_msg_type()` 识别：

```text
LNAV
FDMA
FNAV
INAV
D1
D2
D1D2
SBAS
CNAV
CNV1
CNV2
CNV3
IFNV
CNVX
L1NV
L1OC
L3OC
LXOC
```

是否能够“识别 message type”与“该 message 的所有参数均被完整解释”是两件事；使用新消息时仍应检查对应 decoder。

## 8. `eph_t` 中的广播星历存储

传统轨道/钟差字段继续使用 RTKLIB 原有成员，例如：

```text
toe/toc/ttr
A, e, i0, OMG0, omg, M0
crc, crs, cuc, cus, cic, cis
f0, f1, f2
sva, svh, week, iode, iodc
```

RINEX 4 扩展增加：

```c
nav_data_hdr_t hdr;
double delta_n0;
double top;
double delta_n0_dot;
double urai_ned[3];
double urai_ed;
double isc[6];
double wn_op;
double sisai[4];
double int_flag;
```

因此读取 RINEX 4 时，不能只根据 `eph_t.sat` 判断星历含义；应同时读取：

```c
eph.hdr.sys
eph.hdr.msg_type
```

## 9. TGD / BGD / ISC 的存储规则

本仓库没有给每一种 group delay / inter-signal correction 建立独立命名字段，而是复用：

```c
double tgd[4];
double isc[6];
```

因此解释这些值时必须结合：

```text
hdr.sys + hdr.msg_type
```

### 9.1 GPS

传统/LNAV：

| 参数 | 存储位置 |
|---|---|
| TGD | `eph.tgd[0]` |

GPS CNAV/CNV2 使用 `isc[]`。`eph_t` 中的代码注释定义：

| `isc[]` | GPS 含义 |
|---|---|
| `isc[0]` | ISC L1 C/A |
| `isc[1]` | ISC L2C |
| `isc[2]` | ISC L5 I5 |
| `isc[3]` | ISC L5 Q5 |
| `isc[4]` | ISC L1Cd |
| `isc[5]` | ISC L1Cp |

当前 decoder：

- `NAV_CNAV`：从 RINEX record 连续存入 `isc[0..3]`；
- `NAV_CNV2`：连续存入 `isc[0..5]`；
- TGD 仍使用 `tgd[0]`。

QZSS 的 CNAV/CNV2 进入与 GPS 相同的 decode 分支，但 `isc[]` 在 `rtklib.h` 中的注释是 GPS 语义；处理 QZSS 时应结合具体 RINEX message type/ICD 解释。

### 9.2 Galileo

| 参数 | 存储位置 |
|---|---|
| BGD E5a/E1 | `eph.tgd[0]` |
| BGD E5b/E1 | `eph.tgd[1]` |

因此 `tgd[]` 这个名字对 Galileo 实际保存的是 BGD。

### 9.3 BeiDou D1/D2

传统 BeiDou D1/D2：

| 参数 | 存储位置 |
|---|---|
| TGD1 B1/B3 | `eph.tgd[0]` |
| TGD2 B2/B3 | `eph.tgd[1]` |

### 9.4 BeiDou CNV1/CNV2/CNV3

当前 RINEX 4 decoder 的实际槽位使用如下：

| message type | `isc[0]` | `tgd[0]` | `tgd[1]` |
|---|---|---|---|
| `NAV_CNV1` | CNV1 对应 ISC 字段 | CNV1 第一个 TGD 字段 | CNV1 第二个 TGD 字段 |
| `NAV_CNV2` | CNV2 对应 ISC 字段 | CNV2 第一个 TGD 字段 | CNV2 第二个 TGD 字段 |
| `NAV_CNV3` | 未赋值 | CNV3 TGD 字段 | 未赋值 |

按 RINEX 4.01 对应字段语义，可理解为：

| message type | 参数 | 存储位置 |
|---|---|---|
| CNV1 | ISC B1Cd | `isc[0]` |
| CNV1 | TGD B1Cp | `tgd[0]` |
| CNV1 | TGD B2ap | `tgd[1]` |
| CNV2 | ISC B2ad | `isc[0]` |
| CNV2 | TGD B1Cp | `tgd[0]` |
| CNV2 | TGD B2ap | `tgd[1]` |
| CNV3 | TGD B2bI | `tgd[0]` |

注意：BDS 的 `isc[0]` 是一个复用槽位。它在 CNV1 和 CNV2 下的物理含义不同。

### 9.5 GLONASS

GLONASS 不使用 `eph_t.tgd[]`。

`geph_t` 中单独保存：

```c
double dtaun; /* delay between L1 and L2 (s) */
```

RINEX 4 GLONASS decoder 还增加：

```text
flag
sva
svhflag
dtaun
```

### 9.6 NavIC / IRN

当前 RINEX 4 common ephemeris 分支将 NavIC/IRN 的 group delay 放入：

```text
eph.tgd[0]
```

### 9.7 未使用槽位

虽然结构定义为：

```c
double tgd[4];
```

当前 `src/rinex.c` 的 RINEX NAV 解析/输出路径实际只使用 `tgd[0]` 和 `tgd[1]`；`tgd[2]`、`tgd[3]` 当前没有实际赋值用途。

## 10. RINEX 4 ION / EOP / STO 存储

### 10.1 ION

```c
typedef struct {
    nav_data_hdr_t hdr;
    gtime_t trans_time;
    double alpha[9];
    double region;
    double data[32];
    unsigned char present[32];
    int ndata;
} ion_t;
```

`alpha[9]` 用于容纳不同系统的电离层模型参数；代码注释中包括：

- GPS/QZSS/NavIC 等 Klobuchar 参数；
- Galileo 3 个参数 + region；
- BeiDou BDGIM 9 参数。

记录最终追加到：

```text
nav.ion[]
nav.nion
```

### 10.2 EOP

```c
typedef struct {
    nav_data_hdr_t hdr;
    gtime_t ref_time;
    gtime_t ttr;
    double x, dx, dx2;
    double y, dy, dy2;
    double ut, dut, dut2;
    unsigned char present[10];
} eop_t;
```

记录最终追加到：

```text
nav.eop[]
nav.neop
```

### 10.3 STO

```c
typedef struct {
    nav_data_hdr_t hdr;
    gtime_t ref_time;
    char corr_type[5];
    char corr_id[19];
    double trans_time, a0, a1, a2;
    unsigned char present[4];
} sto_t;
```

STO body 的 epoch、correction type/identifier、transmission time 及三阶
polynomial coefficients 现在均保存到 `nav.sto[]`，并由 `present[]` 保留
固定宽度字段的空白状态。

## 11. Header 中的传统导航参数

除 RINEX 4 body record 外，`nav_t` 仍保留 RTKLIB 传统 header 参数，例如：

```text
utc_gps[]
utc_glo[]
utc_gal[]
utc_qzs[]
utc_cmp[]
utc_sbs[]

ion_gps[]
ion_gal[]
ion_qzs[]
ion_cmp[]

leaps
```

因此使用方需要区分：

1. 传统 RINEX header 中保存的 UTC/ION 参数；
2. RINEX 4 NAV body 中独立 `ION/STO/EOP` record 保存的数据。

## 12. 数据追加与排序/去重

星历读取后通过：

```text
add_eph()
add_geph()
add_seph()
add_ion()
add_eop()
add_sto()
```

追加到 `nav_t` 的动态数组。当前实现通常按 1024 条为一个增量扩容。

`readrnx()` / `readrnxt()` 本身不会保证所有数据已经排序或去重。原始 RTKLIB 使用方式通常在读取完成后调用：

```c
uniqnav(nav);
```

但 RINEX 4 使用时请先阅读下一节的去重注意事项。

## 13. 已知实现注意事项

以下内容描述的是当前代码事实，后续修改代码时应同步更新本文档。

### 13.1 `tgd[]` / `isc[]` 不能脱离 message type 解释

例如：

```text
BDS CNV1: isc[0] != BDS CNV2: isc[0]
BDS D1/D2 tgd[0] != BDS CNV3 tgd[0]
```

业务层不应把裸的 `tgd[0]` / `isc[0]` 当作跨系统、跨 message type 的统一物理参数。

推荐使用：

```text
(hdr.sys, hdr.msg_type, slot)
```

作为解释键。

### 13.2 空字段与数值 0 无法区分

底层 `str2num()` 在字段无法解析时返回 `0.0`。

因此当前结构无法仅通过：

```c
value == 0.0
```

判断“参数真实为 0”还是“RINEX 字段为空/不存在”。

如果业务需要严格区分，应增加 presence/valid flag。

### 13.3 `uniqnav()` 的 RINEX 4 `msg_type` 唯一性

当前 `uniqeph()` 的去重主要依据：

```text
satellite + IODE
```

而 RINEX 4 同一颗卫星可以同时存在 LNAV/CNAV/CNV2 或 D1/D2/CNV1/CNV2/CNV3 等不同消息。

当前实现已经将 `hdr.msg_type` 纳入 RINEX 4 星历唯一键，以保留同一卫星的不同导航消息类型。

### 13.4 GPS CNAV/CNV2 的公共字段需要谨慎使用

`decode_rnx4_eph()` 先执行传统 GPS 公共字段赋值，再覆盖 CNAV/CNV2 专用字段。

因此对 CNAV/CNV2，不能默认 `iode/iodc/code/flag/fit` 等所有传统成员都与 LNAV 具有完全相同的语义。使用新导航消息时应优先依据 `hdr.msg_type` 和对应专用字段。

对 GPS/QZSS `CNAV`/`CNV2`，`data[23]` 是 `URAI-ED`，而不是传统
`eph_t.sva` 的米值；`data[21]`、`data[22]`、`data[26]` 是 raw
`URAI-NED` 分量。`decode_rnx4_eph()` 会在保存这些 raw 字段后把
`eph_t.sva` 设为负的 unknown sentinel，避免该分量进入旧的 metric/URA
方差路径。这里不定义 URAI 到米的转换，也不实现 composite accuracy
evaluator。BDS `CNV1`/`CNV2`/`CNV3` 的 SISAI、传统 GPS/QZSS LNAV、
GLONASS 和 SBAS 使用各自的字段契约，不能套用此例外。

### 13.5 STO、EOP 和 ION 的字段存在性

STO、EOP 和 ION 的 decoder 现在按固定宽度记录各字段的 presence 状态；
STO 使用 `nav.nsto` 独立计数，EOP 的 Y 分量从其规范中的备用字段之后读取，
而 BDS EOP transmission time 按 BDT 周转换后存储为 RTKLIB 的 GPST 时间。
空白数值字段仍不会被静默解释为“原始字段存在的 0”，调用方应检查对应
`present[]`。

### 13.6 “已经存储”不等于“定位模型已经使用”

RINEX 4 reader 已经保存 GPS ISC、Galileo BGD、BDS CNAV TGD/ISC 等字段，但 RTKLIB 原有 SPP/PPP 代码主要仍通过 `tgd[0]` 获取传统 TGD/BGD。

例如原有 `gettgd()` 只返回：

```c
CLIGHT * nav->eph[i].tgd[0]
```

因此如果使用本仓库做多频伪距误差分析，应在业务层按系统、信号和 message type 显式选择正确的 TGD/BGD/ISC，不应假设原有定位代码已经自动应用全部 RINEX 4 bias 参数。

## 14. 建议的业务层使用方式

如果上层程序需要统一处理卫星端 code bias，推荐不要直接传播裸数组下标：

```text
eph.tgd[0]
eph.tgd[1]
eph.isc[0]
...
```

而是在 RINEX reader 上方建立归一化层，例如：

```text
GPS_TGD
GPS_ISC_L1CA
GPS_ISC_L2C
GPS_ISC_L5I5
GPS_ISC_L5Q5
GPS_ISC_L1CD
GPS_ISC_L1CP

GAL_BGD_E5A_E1
GAL_BGD_E5B_E1

BDS_TGD_B1_B3
BDS_TGD_B2_B3
BDS_TGD_B1CP
BDS_TGD_B2AP
BDS_TGD_B2BI
BDS_ISC_B1CD
BDS_ISC_B2AD

GLO_DTAUN
```

推荐映射过程：

```text
RINEX NAV record
    -> nav_data_hdr_t(sys, msg_type, prn)
    -> eph_t/geph_t slot
    -> normalized code-bias type
    -> pseudorange correction model
```

这样可以避免未来扩展 RINEX 4.x、新导航消息或新频点时继续依赖含义不固定的数组下标。

## 15. 相关源码

主要实现文件：

- `src/rinex.c`
  - RINEX header/body 读取
  - RINEX 4 record/message type 解析
  - OBS/NAV/CLK 分流
  - RINEX 4 EPH/ION/EOP/STO 解析
  - TGD/BGD/ISC 字段赋值
- `src/rtklib.h`
  - `obsd_t`, `obs_t`
  - `eph_t`, `geph_t`, `seph_t`
  - `ion_t`, `eop_t`, `sto_t`
  - `nav_t`
- `src/rtkcmn.c`
  - `str2num()`
  - `uniqnav()` / `uniqeph()`
- `src/pntpos.c`
  - 原有 SPP 中的 `gettgd()` 和伪距 code-bias 处理
- `src/ppp.c`
  - PPP 中传统 TGD/DCB 使用方式
- `src/ephemeris.c`
  - 广播星历位置/钟差计算；卫星钟差本身不包含 TGD/BGD code bias

## 16. 参考

- RINEX 4.02 specification
- RTKLIB `src/rinex.c`
- RTKLIB `src/rtklib.h`

如修改 RINEX 4 NAV decoder、`eph_t` 的 TGD/ISC 槽位、`nav_data_hdr_t` 或 `uniqnav()`，应同步更新本文档。

## 17. Issue #1 NAV 字段级验证

仓库中的 `tests/rinex_nav_compare/` 提供三层结果：

1. `rtklib_nav_dump.c` 只通过公开 `readrnx()` API 导出 RTKLIB 存储结果；
2. `run_compare.py` 使用固定宽度 raw reader 建立可追溯的字段库存；
3. GeoRinex 在可支持的文件上作为独立 canonical exporter，按系统、PRN、
   epoch 和重复序号导出变量值。

可复现命令（从仓库父目录执行；GeoRinex 建议使用隔离 Python 环境）：

```text
/tmp/rtklib_nav_compare_venv/bin/python \
  RTKLIB/tests/rinex_nav_compare/run_compare.py \
  --fixtures nav_data --include-repo-test-nav \
  --rtklib-dump /tmp/rtklib_nav_dump
```

输出写入 `artifacts/rinex_nav_compare/`：

- `field_inventory.csv`：每个 raw 字段、RTKLIB 映射、GeoRinex 映射、原始文本、
  presence、nav-solutions/rinex 映射和值以及终态分类；每个 raw 字段一行；
- `differences.csv`：所有非 `MATCH` 字段；
- `unmatched_records.csv`：raw↔RTKLIB 的记录键缺口，包含 `reason` 和
  `mismatch_reason`；`reference_unmatched_records.csv` 单独记录
  nav-solutions/rinex 的记录覆盖缺口；
- `summary.json`：版本、系统、消息类型、分类计数、工具版本和 GeoRinex 失败原因；
- `report.md`：简要复现报告。

终态分类只允许 `MATCH`、`VALUE_MISMATCH`、`PRESENCE_MISMATCH`、
`COVERAGE_GAP_RTKLIB`、`COVERAGE_GAP_GEORINEX`、`SEMANTIC_MAPPING_GAP` 和
`COVERAGE_GAP_NAV_SOLUTIONS_RINEX`、`REFERENCE_UNRESOLVED`；未映射或不支持的
字段不能计入通过数量。`REFERENCE_UNRESOLVED` 仅用于已有对应字段但值/表示
仍需 raw/spec 裁决的情况。

RINEX 4 专项闭环还会在 `field_inventory.csv` 中保留每个消息类型的
TGD/BGD/ISC 语义：GPS/QZSS CNAV/CNV2 的 `iscL1Ca`、`iscL2C`、`iscL5I5`、
`iscL5Q5`、`iscL1Cd`、`iscL1Cp`，Galileo 的两个 BGD 字段，以及 BeiDou
CNV1/CNV2/CNV3 的 `iscB1Cd`/`iscB2ad` 和三组 TGD 字段。2026-08-23 的
BRD400 RINEX 4.02 专项运行覆盖 584,803 个 raw 字段，raw↔RTKLIB
`unmatched_record_count=0`、`unclassified_field_count=0`、
`value_mismatch_count=0`；GeoRinex 对该 RINEX 4.02 布局的限制被明确分类，
没有作为通过条件。

数值比较采用：整数/索引字段精确比较，时间字段绝对误差 `1e-6`，其余
浮点字段 `abs <= 1e-11 + 1e-9 * max(abs(a), abs(b))`。
