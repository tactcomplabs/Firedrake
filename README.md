# Firedrake

Firedrake wraps Xilinx Vitis kernels in SST components. A generator reads a
kernel's metadata (`kernel.xml`, `.xo` or `.xclbin`) and produces an SST
element library holding one SubComponent for that kernel:

| Kernel interface | SST view |
|---|---|
| `m_axi` buffer argument | an SST **port** named after the argument, carrying `SST::Firedrake::DataEvent` (WRITE/READ, answered with ACK/RESP/NACK) |
| `s_axilite` scalars, raw registers, start/done | the **C++ API** `SST::Firedrake::FiredrakeBase` plus generated typed accessors (`set_<arg>()`, `get_<arg>()`) |
| run completion | the C++ `onDone()` callback and a `done` port carrying `SST::Firedrake::DoneEvent` |
| `axis` stream argument | no host port (XRT has no general host streaming on Alveo); keep stream connectivity inside the xclbin |

The host side runs on the Xilinx Runtime (XRT) native C++ API (`xrt::device`,
`xrt::kernel`/`xrt::run`, `xrt::bo`, `xrt::ip`) for the `sw_emu`, `hw_emu` and
`hw` targets. A `csim` backend calls the HLS C++ kernel directly, so a model
builds and runs without XRT or a device.

Firedrake follows the structure of
[VerilatorSST](https://github.com/tactcomplabs/verilator-sst): a fixed core,
plus a generator that turns one design into one element library.

## Architecture

```
SDL ──► controller component (yours, or FiredrakeComponent)
           │  slot "model": loadUserSubComponent<FiredrakeBase>
           ▼
        Firedrake<Kernel>          generated: ELI, ports, typed accessors, csim trampoline
           │  derives from
        FiredrakeKernel            libfiredrakecore: ports, timing, control logic
           │
        KernelBackend ── CsimBackend (native HLS C++)
                      └─ XrtBackend  (XRT: sw_emu | hw_emu | hw)
```

* **Control is C++.** The component that loads the kernel into one of its
  slots controls it directly: `setScalar()`, `writeReg()`/`readReg()`,
  `start()`, `isRunning()`, `isDone()` and `onDone()`. Writing `AP_START` to
  control register `0x00` is the same as calling `start()`.
* **Data moves over ports.** Connect each buffer port to any component that
  sends `DataEvent`s. In SDL the ports belong to the model SubComponent.
* **Timing is a deterministic model, the same on every backend.** Backends
  run immediately. Responses and completion are delivered at modeled times:
  * Kernel run: `startLatency + kernelCycles / kernelClock`.
  * Buffer transfer: all transfers share one host link. Each transfer
    occupies it for `bytes / hostBW` and completes `hostLatency` after it
    leaves the link.

  `kernelCycles` is a sum of products of constants and input scalar names,
  for example `"70 + 1*size"`.

## Requirements

* SST 16.0.0 or newer (`sst`, `sst-config` on `PATH`)
* CMake 3.19 or newer, a C++17 compiler, and Python 3.8 or newer
* For csim: the Vitis HLS headers (`$XILINX_HLS/include`), found automatically from `XILINX_HLS` or `XILINX_VITIS`
* For sw_emu, hw_emu or hw: XRT (`source /opt/xilinx/xrt/setup.sh`) and an Alveo platform on an x86 host. For hw you also need the card.

## Build and test

```
mkdir build && cd build
cmake ..                 # XRT backends are enabled automatically when XRT is found
make -j
ctest                    # csim tests; run anywhere
make install             # installs into ./install and runs sst-register
```

To also build the test xclbins with `v++` and run the XRT tests:

```
cmake -DFIREDRAKE_PLATFORM=xilinx_u250_gen3x16_xdma_4_1_202210_1 -DFIREDRAKE_TARGETS="sw_emu;hw_emu" ..
make -j && ctest -L sw_emu
```

Each test runs `tests/sdl/firedrake_test.py` and checks the results and the
modeled timing. The tests are labelled `csim`, `sw_emu`, `hw_emu`, `hw` and `u50`.

### Alveo U50 hardware test

The `u50` tests build the vadd kernel into a U50 bitstream and run the vadd
scenarios on the card. Requirements:

* XRT, and the U50 deployment and development platform packages
  (`xilinx_u50_gen3x16_xdma_5_202210_1` by default)
* A card running the matching base shell (`xilinx_u50_gen3x16_xdma_base_5`)
* A Vitis release that supports the platform (2026.1 works). Select a different `v++` with `-DVPP=/path/to/v++`.
* `PLATFORM_REPO_PATHS=/opt/xilinx/platforms` in the environment when you run ctest, so `v++` finds the platform

```
source /opt/xilinx/xrt/setup.sh
export PLATFORM_REPO_PATHS=/opt/xilinx/platforms
cmake -DFIREDRAKE_U50_TEST=ON [-DFIREDRAKE_U50_PLATFORM=<platform>] ..
make -j
ctest -L u50 --output-on-failure
```

`make` does not build the bitstream. ctest runs it as fixtures, in this order:

1. `vadd_u50_device` checks for the card with `xrt-smi examine`.
2. `vadd_u50_xclbin` runs `v++` to build `tests/vadd.hw.u50/vadd.xclbin`. This takes about two hours, and the timeout is 4 h.
3. `vadd_direct_u50`, `vadd_direct_runs_u50` and `vadd_links_u50` run serially on the card.

The buffers are mapped to HBM by `tests/kernels/vadd/vadd.u50.cfg`.

## Retargeting to your kernel

**Option 1: configure Firedrake against your kernel.** This builds the element library `firedrake<kernel>`:

```
cmake -DFIREDRAKE_KERNEL_NAME=mykernel \
      -DFIREDRAKE_KERNEL_METADATA=/path/to/mykernel.xo \
      -DFIREDRAKE_HLS_SOURCES="/path/to/mykernel.cpp" \
      -DFIREDRAKE_KERNEL_OVERRIDES=/path/to/mykernel.firedrake.json ..
```

* `FIREDRAKE_KERNEL_METADATA` can be a `kernel.xml`, an `.xo` or an `.xclbin`
  (read with `xclbinutil`), or a Firedrake JSON kernel model (the
  `<kernel>.json` the generator writes).
* `FIREDRAKE_HLS_SOURCES` is optional. Without it, only the XRT backends are
  available.

**Option 2: call the CMake function** from inside this tree, the same way `tests/CMakeLists.txt` does:

```cmake
firedrake_generate_component(NAME mykernel
  METADATA ${CMAKE_CURRENT_SOURCE_DIR}/mykernel/kernel.xml
  OVERRIDES ${CMAKE_CURRENT_SOURCE_DIR}/mykernel/mykernel.firedrake.json
  HLS_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/mykernel/mykernel.cpp)

firedrake_add_vitis_kernel(NAME mykernel SOURCES mykernel/mykernel.cpp
  TARGET hw_emu PLATFORM ${FIREDRAKE_PLATFORM})   # sets mykernel_XCLBIN, mykernel_EMCONFIG_DIR
```

**Option 3: run the generator directly.**

```
scripts/firedrake-gen.py --metadata mykernel.xo --out gen/ [--csim] [--overrides o.json] [--kernel name]
```

The overrides file is optional:

```json
{
  "kernelCycles": "70 + 1*size",
  "bufferSizes": { "in1": 4096 },
  "argTypes": { "in1": "const unsigned int*", "size": "int" },
  "csimInclude": "mykernel.h",
  "csimLinkage": "c"
}
```

* `kernelCycles` is the default cycle model.
* `bufferSizes` gives default buffer allocations.
* `argTypes` restores source-level argument types. Vitis writes every m_axi
  pointer and output register into `kernel.xml` as `void*`, and some signed
  scalars as `unsigned int`. These types drive the typed accessors and the
  csim declaration, and the test kernels show the pattern.
* `csimInclude` is needed when arguments use `ap_uint<>`, `hls::vector<>` or
  other C++ types. The csim trampoline then includes that header instead of
  declaring the kernel from its `kernel.xml` types.
* `csimLinkage` is `c` (the default, for `extern "C"` kernels) or `c++`.

The generator writes these files:

* `Firedrake<Kernel>.h` and `Firedrake<Kernel>.cc`: the SubComponent.
* `<kernel>.json`: the normalized kernel model.

A controller that wants the typed accessors includes the generated header
and `dynamic_cast`s the `FiredrakeBase*` it loaded.

## Using a kernel in SST

Direct control from your own component (see `tests/elements/FiredrakeHost.cc`):

```cpp
model = loadUserSubComponent<SST::Firedrake::FiredrakeBase>("model");
model->onDone([this](uint64_t run, uint64_t cycles) { /* read results over the out port */ });
auto* vadd = dynamic_cast<SST::Firedrake::FiredrakeVadd*>(model);
vadd->set_size(1024);            // s_axilite scalar
// ... send DataEvent::makeWrite() on the links to in1/in2, then:
model->start();
```

```python
host  = sst.Component("host", "mylib.MyHost")
model = host.setSubComponent("model", "firedrakevadd.FiredrakeVadd")
model.addParams({"backend": "hw_emu", "xclbin": "vadd.xclbin", "emconfigDir": ".",
                 "bufferSize.in1": 4096, "bufferSize.in2": 4096, "bufferSize.out": 4096})
sst.Link("l_in1").connect((host, "port0", "1ns"), (model, "in1", "1ns"))
```

If you don't need a C++ controller, use `firedrake.FiredrakeComponent`. It
takes scalars from the model's `arg.<name>` params and starts the kernel once
every buffer listed in `startAfter` has been written.

### Model parameters (every kernel)

| Parameter | Default | Meaning |
|---|---|---|
| `backend` | `csim` | `csim`, `sw_emu`, `hw_emu` or `hw` |
| `xclbin`, `emconfigDir`, `deviceIndex`, `instance` | | Where the XRT backends find the kernel. `instance` selects a compute unit, for example `vadd:{vadd_1}`. |
| `kernelClock` | `300MHz` | Kernel clock |
| `kernelCycles` | from overrides | Cycle model |
| `startLatency` | `0ns` | Delay between `start()` and the first kernel cycle |
| `hostBW`, `hostLatency` | `12GB/s`, `1us` | Host link model |
| `bufferSize.<arg>` | from overrides | Bytes to allocate for buffer `<arg>`. A buffer must be allocated before it is used; accesses outside the allocation get a NACK. |
| `arg.<arg>` | `0` | Initial value of input scalar `<arg>` |

Statistics: `buffer_writes`, `buffer_reads`, `bytes_written`, `bytes_read`,
`reads_while_running`, `kernel_runs` and `kernel_cycles`.

## Notes and limitations

* **Emulation mode.** `XCL_EMULATION_MODE` is process wide, so every
  Firedrake kernel in one SST process (or MPI rank) must use the same XRT
  target.
* **Device sharing.** Kernels that share a device and an xclbin share one
  `xrt::device` and one xclbin load (`DeviceRegistry`). All XRT calls are
  serialized through one process-wide lock.
* **Control protocols.**
  * `ap_ctrl_hs` and `ap_ctrl_chain` kernels use `xrt::kernel` and `xrt::run`,
    opened with exclusive CU access so that raw register access works.
  * `ap_ctrl_none` and user-managed kernels use `xrt::ip`. In csim, a
    free-running kernel is re-evaluated after every input write.
* **Overlapping access.** Buffer reads that arrive while the kernel is running
  return the backend's current contents and are counted in
  `reads_while_running`.
* **Checkpointing.** Device state cannot be checkpointed, so the elements are
  not checkpointable.
* **XRT backend coverage.** The XRT backend has been run on hardware: the
  `u50` tests (XRT 2.19, Vitis 2026.1, an Alveo U50). The `sw_emu` and
  `hw_emu` targets have not yet been run.

## Acknowledgements

This work was supported by the U.S. Department of Energy, Office of Science, Advanced Scientific Computing Research program under project 84245 — Democratization of Co-design for Energy-Efficient Heterogeneous Computing (DeCoDe) at Pacific Northwest National Laboratory (PNNL). PNNL is a multi-program national laboratory operated for the U.S. Department of Energy (DOE) by Battelle Memorial Institute under Contract No. DE-AC05-76RL01830.
