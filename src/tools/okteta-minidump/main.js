// Copyright 2023 Google LLC
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google LLC nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

function init() {

 function getKeyFromValue(object, value) {
    for (var key in object) {
      if (object.hasOwnProperty(key)) {
        if (object[key] == value) {
          return key;
        }
      }
    }
  }

  // The Kasten pointer datatype has a difficult time with nullability.
  // The updateFunc could update the target, but does not have the value.
  // The interpretFunc has the value, but cannot update the target.
  // The pointer type must be one of the uint* datatypes.
  // This could be worked around by having the pointee datatype look at its
  // pointer parent, notice it was pointing at zero, and set itself to nothing.
  // However, this is very intrusive and difficult to express generally.
  // Instead, use the interpretFunc to map 0 to uint32 max which should read off
  // the end of the file. This won't work as expected if inspecting minidumps
  // >4GB, but it is unlikely that this structure editor will work or be useful
  // with such large files anyway.
  var point_at_uint32max_if_zero = function(){
    return this.value ? this.value : 0xFFFFFFFF;
  };

  // To avoid excessive nesting, forcing the user to toggle excessive numbers
  // of twiddles, and to simplify typing the MINIDUMP_LOCATION_DESCRIPTOR is
  // inlined as <name>DataSize and <name>Rva. Many of the streams only use
  // location descriptors when the target exists so there is often no need for a
  // NULL or "does not exist" value. However, the Crashpad defined streams do
  // require special handling of pointing at location 0 (the header).
  // However, location 0 is perfectly fine target. Handle all the edge cases by
  // changing the pointer target type to a zero sized datatype if the DataSize
  // is zero.
  var point_at_nothing_if_size_is_zero = function(){
    // Remove "Rva", add "DataSize".
    const dataSizeName = this.name.slice(0, -3) + "DataSize";
    if (this.parent[dataSizeName].value == 0) {
      // An empty struct produces a warning.
      this.target = array(uint8(), 0);
    }
  };

  // The Kasten string datatype does not provide toString.
  // However, one can index the character strings and accumulate.
  function minidump_string_toString(minidump_string) {
    var s = "";
    const buffer = minidump_string.Buffer;
    for (var i = 0; i < buffer.charCount; ++i) {
      const charDesc = buffer[i];
      // Matching against "(<unit>)|(<leading><trailing>) \(U+<hexdigits>\)"
      if (charDesc.length > 5 && charDesc[1] == " "
                              && charDesc[2] == "("
                              && charDesc[3] == "U"
                              && charDesc[4] == "+")
      {
        s += charDesc[0];
      } else if (charDesc.length > 6 && charDesc[2] == " "
                                     && charDesc[3] == "("
                                     && charDesc[4] == "U"
                                     && charDesc[5] == "+")
      {
        s += charDesc[0];
        s += charDesc[1];
      } else {
        s += "\uFFFD";
      }
    }
    return s;
  };

  var minidump_utf8_string = struct({
    Length: uint32(),
    Buffer: string("utf8").set({
      // UTF-8 is not zero terminated (U+0000 is a valid code point).
      // However, the documentation describes MODULE_STRING.Buffer as "The
      // null-terminated string". The Length is described as the length of
      // Buffer without the null-terminating character". Crashpad appears to
      // write out the extraneous U+0000 code point at the end and also include
      // it in Length. This makes copy-paste difficult. So allow strings to be
      // U+0000 terminated.
      terminatedBy: 0,
      updateFunc: function(){this.maxByteCount = this.parent.Length.value;},
    }),
  }).set({
    typeName: "MINIDUMP_UTF8_STRING",
    // The Kasten string datatype does not provide toString.
    toStringFunc: function(){ return minidump_string_toString(this); },
  });

  var minidump_string = struct({
    Length: uint32(),
    Buffer: string("utf16").set({
      // UTF-16 is not zero terminated (U+0000 is a valid code point).
      // However, the documentation describes MODULE_STRING.Buffer as "The
      // null-terminated string". The Length is described as the length of
      // Buffer without the null-terminating character". Crashpad appears to
      // write out the extraneous U+0000 code point at the end and also include
      // it in Length. This makes copy-paste difficult. So allow strings to be
      // U+0000 terminated.
      terminatedBy: 0,
      updateFunc: function(){this.maxByteCount = this.parent.Length.value;},
    }),
  }).set({
    typeName: "MINIDUMP_STRING",
    // The Kasten string datatype does not provide toString.
    toStringFunc: function(){return minidump_string_toString(this);},
  });

  var uint128_struct = struct({
    low: uint64(),
    high: uint64(),
  }).set({
    typeName: "uint128",
  });

  minidump_guid = struct({
    data1: uint32(),
    data2: uint16(),
    data3: uint16(),
    data4: array(uint8(), 8),
  }).set({
    typeName: "GUID",
    toStringFunc: function(){
      return ("00000000"+this.data1   .value.toString(16)).slice(-8) + "-"
           + (    "0000"+this.data2   .value.toString(16)).slice(-4) + "-"
           + (    "0000"+this.data3   .value.toString(16)).slice(-4) + "-"
           + (      "00"+this.data4[0].value.toString(16)).slice(-2)
           + (      "00"+this.data4[1].value.toString(16)).slice(-2) + "-"
           + (      "00"+this.data4[2].value.toString(16)).slice(-2)
           + (      "00"+this.data4[3].value.toString(16)).slice(-2)
           + (      "00"+this.data4[4].value.toString(16)).slice(-2)
           + (      "00"+this.data4[5].value.toString(16)).slice(-2)
           + (      "00"+this.data4[6].value.toString(16)).slice(-2)
           + (      "00"+this.data4[7].value.toString(16)).slice(-2);
    },
  });

  // MINIDUMP_THREAD_CONTEXT_X86 --------------------------------------------X86
  var minidump_thread_context_x87_long_double = struct({
    value: array(uint8(), 10),
  }).set({
    typeName: "long double",
  });
  var minidump_thread_context_x86_floating_save_area = struct({
    control_word: uint32(),
    status_word: uint32(),
    tag_word: uint32(),
    error_offset: uint32(),
    error_selector: uint32(),
    data_offset: uint32(),
    data_selector: uint32(),

    // %st0 (%mm0) through %st7 (%mm7).
    register_area: array(minidump_thread_context_x87_long_double, 8),
    cr0_npx_state: uint32(),
  }).set({
    typeName: "FLOATING_SAVE_AREA",
  });
  var minidump_thread_context_x86_fxsave = struct({
    FCW: uint16(),
    FSW: uint16(),
    AbridgedFTW: uint8(),
    Reserved1: uint8(),
    FOP: uint16(),
    FIP: uint32(),
    FCS: uint16(),
    Reserved2: uint16(),
    FDP: uint32(),
    FDS: uint32(),
    MXCSR: uint32(),
    MXCSR_MASK: uint32(),
    ST0_MM0: minidump_thread_context_x87_long_double,
    ReservedR0: array(uint8(), 6),
    ST1_MM1: minidump_thread_context_x87_long_double,
    ReservedR1: array(uint8(), 6),
    ST2_MM2: minidump_thread_context_x87_long_double,
    ReservedR2: array(uint8(), 6),
    ST3_MM3: minidump_thread_context_x87_long_double,
    ReservedR3: array(uint8(), 6),
    ST4_MM4: minidump_thread_context_x87_long_double,
    ReservedR4: array(uint8(), 6),
    ST5_MM5: minidump_thread_context_x87_long_double,
    ReservedR5: array(uint8(), 6),
    ST6_MM6: minidump_thread_context_x87_long_double,
    ReservedR6: array(uint8(), 6),
    ST7_MM7: minidump_thread_context_x87_long_double,
    ReservedR7: array(uint8(), 6),
    XMM0: array(uint8(), 16),
    XMM1: array(uint8(), 16),
    XMM2: array(uint8(), 16),
    XMM3: array(uint8(), 16),
    XMM4: array(uint8(), 16),
    XMM5: array(uint8(), 16),
    XMM6: array(uint8(), 16),
    XMM7: array(uint8(), 16),
    Reserved3: array(uint8(), 16),
    Reserved4: array(uint8(), 160),
    Available: array(uint8(), 48),
  }).set({
    typeName: "FXSAVE",
  });
  var minidump_thread_context_x86_eflags = flags("EFLAGS", uint32(), {
    CF:   0x00000001, // Carry Flag: Carry to or borrow from a dest.
    R1:   0x00000002, // Reserved, always 1.
    PF:   0x00000004, // Parity flag: The dest LSB has an even number of 1's.
    R2:   0x00000008, // Reserved.
    AF:   0x00000010, // Auxiliary Carry Flag: Used for BCD arithmetic.
    R3:   0x00000020, // Reserved.
    ZF:   0x00000040, // Zero Flag: The result an operation is binary zero.
    SF:   0x00000080, // Sign Flag: The most significant bit of the result.
    TF:   0x00000100, // Trap Flag: Fault after the next instruction.
    IF:   0x00000200, // Interrupt Enable Flag: Enable interrupts.
    DF:   0x00000400, // Direction Flag: String operation direction (0 is up).
    OF:   0x00000800, // Overflow Flag: The result did not fit in the dest.
    IOPL: 0x00003000, // I/O Privilege Level: Protected mode ring level.
    NT:   0x00004000, // Nested Task Flag: A system task used CALL (not JMP).
    MD:   0x00008000, // Mode Flag: Always 1 (80186/8080 mode).
    RF:   0x00010000, // Resume Flag: See DR6, DR7. Disables some exceptions.
    VM:   0x00020000, // Virtual 8086 Mode flag: Makes 80386+ run like 8086.
    AC:   0x00040000, // Alignment Check / SMAP Access Check
    VIF:  0x00080000, // Virtual Interrupt Flag
    VIP:  0x00100000, // Virtual Interrupt Pending
    ID:   0x00200000, // Able to use CPUID instruction
  });
  var minidump_thread_context_x86 = struct({
    /* Indicates which parts of this structure are valid. */
    context_flags: flags("Context Flags", uint32(), {
      X86: 0x00010000,
      CONTROL: 0x00000001,
      INTEGER: 0x00000002,
      SEGMENTS: 0x00000004,
      FLOATING_POINT: 0x00000008,
      DEBUG_REGISTERS: 0x00000010,
      EXTENDED_REGISTERS: 0x00000020,
      XSTATE: 0x00000040,
      FULL: 0x00000001  // CONTROL
          | 0x00000002  // INTEGER
          | 0x00000004  // SEGMENTS
          ,
      ALL: 0x00000001  // CONTROL
         | 0x00000002  // INTEGER
         | 0x00000004  // SEGMENTS
         | 0x00000008  // FLOATING_POINT
         | 0x00000010  // DEBUG_REGISTERS
         | 0x00000020  // EXTENDED_REGISTERS
         ,
    }),

    /* DEBUG_REGISTERS */
    dr0: uint32(),
    dr1: uint32(),
    dr2: uint32(),
    dr3: uint32(),
    dr6: uint32(),
    dr7: uint32(),

    /* FLOATING_POINT */
    float_save: minidump_thread_context_x86_floating_save_area,

    /* SEGMENTS */
    gs: uint32(),
    fs: uint32(),
    es: uint32(),
    ds: uint32(),
    /* INTEGER */
    edi: uint32(),
    esi: uint32(),
    ebx: uint32(),
    edx: uint32(),
    ecx: uint32(),
    eax: uint32(),

    /* CONTROL */
    ebp: uint32(),
    eip: uint32(),
    cs: uint32(),
    eflags: minidump_thread_context_x86_eflags,
    esp: uint32(),
    ss: uint32(),

    /* EXTENDED_REGISTERS */
    extended_registers: minidump_thread_context_x86_fxsave,
  }).set({
    typeName: "MINIDUMP_THREAD_CONTEXT_X86",
  });

  // MINIDUMP_THREAD_CONTEXT_AMD64 ----------------------------------------AMD64
  var MDXmmSaveArea32AMD64 = struct({
    control_word: uint16(),
    status_word: uint16(),
    tag_word: uint8(),
    reserved1: uint8(),
    error_opcode: uint16(),
    error_offset: uint32(),
    error_selector: uint16(),
    reserved2: uint16(),
    data_offset: uint32(),
    data_selector: uint16(),
    reserved3: uint16(),
    mx_csr: uint32(),
    mx_csr_mask: uint32(),
    float_registers: array(uint128_struct, 8),
    xmm_registers: array(uint128_struct, 16),
    reserved4: array(uint8(), 96),
  });

  var minidump_thread_context_amd64 = struct({
    p1_home: uint64(),
    p2_home: uint64(),
    p3_home: uint64(),
    p4_home: uint64(),
    p5_home: uint64(),
    p6_home: uint64(),

    /* Indicates which parts of this structure are valid. */
    context_flags: flags("Context Flags", uint32(), {
      AMD64: 0x00100000,
      CONTROL: 0x00000001,
      INTEGER: 0x00000002,
      SEGMENTS: 0x00000004,
      FLOATING_POINT: 0x00000008,
      DEBUG_REGISTERS: 0x00000010,
      XSTATE: 0x00000040,
      FULL: 0x00000001 // CONTROL
          | 0x00000002 // INTEGER
          | 0x00000008 // FLOATING_POINT
          ,
      ALL: 0x00000001 // CONTROL
         | 0x00000002 // INTEGER
         | 0x00000008 // FLOATING_POINT
         | 0x00000004 // SEGMENTS
         | 0x00000010 // DEBUG_REGISTERS
         ,
    }),
    mx_csr: flags("SSE Control Status", uint32(), {
      IE:    0x0001, // Invalid Operation Flag
      DE:    0x0002, // Denormal Flag
      ZE:    0x0004, // Divide-by-Zero Flag
      OE:    0x0008, // Overflow Flag
      UE:    0x0010, // Underflow Flag
      PE:    0x0020, // Precision Flag
      DAZ:   0x0040, // Denormals Are Zeros
      IM:    0x0080, // Invalid Operation Mask
      DM:    0x0100, // Denormal Operation Mask
      ZM:    0x0200, // Divide-by-Zero Mask
      OM:    0x0400, // Overflow Mask
      UM:    0x0800, // Underflow Mask
      PM:    0x1000, // Precision Mask
      ALL_M: 0x1F80, // All Masks
      RC_N:  0x0000, // Rounding Control: toward Nearest
      RC_NI: 0x2000, // Rounding Control: toward Negative Infinity
      RC_PI: 0x4000, // Rounding Control: toward Positive Infinity
      RC_Z:  0x6000, // Rounding Control: toward Zero
      FZ:    0x8000, // Flush to Zero
    }),

    /* CONTROL */
    cs: uint16(),

    /* SEGMENTS */
    ds: uint16(),
    es: uint16(),
    fs: uint16(),
    gs: uint16(),

    /* CONTROL */
    ss: uint16(),
    eflags: minidump_thread_context_x86_eflags,

    /* DEBUG_REGISTERS */
    dr0: uint64(),
    dr1: uint64(),
    dr2: uint64(),
    dr3: uint64(),
    dr6: uint64(),
    dr7: uint64(),

    /* INTEGER */
    rax: uint64(),
    rcx: uint64(),
    rdx: uint64(),
    rbx: uint64(),

    /* CONTROL */
    rsp: uint64(),

    /* INTEGER */
    rbp: uint64(),
    rsi: uint64(),
    rdi: uint64(),
    r8: uint64(),
    r9: uint64(),
    r10: uint64(),
    r11: uint64(),
    r12: uint64(),
    r13: uint64(),
    r14: uint64(),
    r15: uint64(),

    /* CONTROL */
    rip: uint64(),

    /* FLOATING_POINT */
    floating_point: union({
      flt_save: MDXmmSaveArea32AMD64,
      sse_registers: struct({
        header: array(uint128_struct, 2),
        legacy: array(uint128_struct, 8),
        xmm0: uint128_struct,
        xmm1: uint128_struct,
        xmm2: uint128_struct,
        xmm3: uint128_struct,
        xmm4: uint128_struct,
        xmm5: uint128_struct,
        xmm6: uint128_struct,
        xmm7: uint128_struct,
        xmm8: uint128_struct,
        xmm9: uint128_struct,
        xmm10: uint128_struct,
        xmm11: uint128_struct,
        xmm12: uint128_struct,
        xmm13: uint128_struct,
        xmm14: uint128_struct,
        xmm15: uint128_struct,
      }),
    }),
    vector_register: array(uint128_struct, 26 /* AMD64_VR_COUNT */),
    vector_control: uint64(),

    /* DEBUG_REGISTERS */
    debug_control: uint64(),
    last_branch_to_rip: uint64(),
    last_branch_from_rip: uint64(),
    last_exception_to_rip: uint64(),
    last_exception_from_rip: uint64(),
  }).set({
    typeName: "MINIDUMP_THREAD_CONTEXT_AMD64",
  });

  // MINIDUMP_THREAD_CONTEXT_ARM --------------------------------------------ARM
  var minidump_thread_context_arm_cpsr = flags(
    "Current Program Status Register", uint32(),
  {
    M_USR: 0x00000000,  // User mode
    M_FIQ: 0x00000001,  // Fast or high priority interrupt mode
    M_IRQ: 0x00000002,  // Normal or low priority interrupt mode
    M_SVC: 0x00000003,  // Supervisor mode (software interrupt handler)
    M_MON: 0x00000006,  // Monitor mode (Secure mode / SMC)
    M_ABT: 0x00000007,  // Abort mode (memory access violation handler)
    M_HYP: 0x0000000a,  // Hypervisor mode (Virtualization Extensions)
    M_UND: 0x0000000b,  // Undef mode (undefined instruction handler)
    M_SYS: 0x0000000f,  // System mode (privileged with user registers)
    // Reserved 4
    T    : 0x00000020, // Thumb
    F    : 0x00000040, // FIQ mask
    I    : 0x00000080, // IRQ mask
    A    : 0x00000100, // Asynchronous/SError interrupt mask
    E    : 0x00000200, // Big endian
    IT_2 : 0x00000400, // If Then 2 (Thumb-2)
    IT_3 : 0x00000800, // If Then 3 (Thumb-2)
    IT_4 : 0x00001000, // If Then 4 (Thumb-2)
    IT_5 : 0x00002000, // If Then 5 (Thumb-2)
    IT_6 : 0x00004000, // If Then 6 (Thumb-2)
    IT_7 : 0x00008000, // If Then 7 (Thumb-2)
    GE_1 : 0x00010000, // Greater than or Equal 1 (SIMD)
    GE_2 : 0x00020000, // Greater than or Equal 2 (SIMD)
    GE_3 : 0x00040000, // Greater than or Equal 3 (SIMD)
    GE_4 : 0x00080000, // Greater than or Equal 4 (SIMD)
    // Reserved 20
    DIT  : 0x00200000, // Data Independent Timing
    PAN  : 0x00400000, // Privileged Access Never
    SSBS : 0x00800000, // Speculative Store Bypass Safe
    J    : 0x01000000, // Java / Jazelle
    IT_0 : 0x02000000, // If Then 0 (Thumb-2)
    IT_1 : 0x04000000, // If Then 1 (Thumb-2)
    Q    : 0x08000000, // Cumulative saturation
    V    : 0x10000000, // Overflow condition
    C    : 0x20000000, // Carry condition
    Z    : 0x40000000, // Zero condition
    N    : 0x80000000, // Negative condition
  });

  var minidump_thread_context_arm = struct({
    /* Indicates which parts of this structure are valid. */
    context_flags: flags("Context Flags", uint32(), {
      ARM: 0x40000000,
      INTEGER: 0x00000002,
      FLOATING_POINT: 0x00000004,
      FULL: 0x00000002 // INTEGER
          | 0x00000004 // FLOATING_POINT
          ,
      ALL: 0x00000002 // INTEGER
         | 0x00000004 // FLOATING_POINT
         ,
    }),

    /* INTEGER */
    // r0 to r15
    regs: array(uint32(), 13),
    sp: uint32(),
    lr: uint32(),
    pc: uint32(),
    cpsr: minidump_thread_context_arm_cpsr,

    /* FLOATING_POINT */
    // The fpscr register is 32 bit.
    // Breakpad declares this as 64 bit to avoid padding in the struct.
    // Crashpad declares this as 32 bit and has padding.
    // This works out due to everything in practice being little endian.
    // Go with crashpad since it is writing most of these.
    fpscr: flags("Floating-Point Status and Control Register", uint32(), {
      IOC: 0x00000001,  // Invalid Operation Cumulative
      DZC: 0x00000002,  // Divide by Zero Cumulative
      OFC: 0x00000004,  // OverFlow Cumulative
      UFC: 0x00000008,  // UnderFlow Cumulative
      IXC: 0x00000010,  // IneXact Cumulative
      // Reserved 5-6
      IDC: 0x00000080,  // Input Denormal Cumulative
      IOE: 0x00000100,  // Invalid Operations trap Enabled
      DZE: 0x00000200,  // Divide-by-Zero trap Enabled
      OFE: 0x00000400,  // OverFlow trap Enabled
      UFE: 0x00000800,  // UnderFlow trap Enabled
      IXE: 0x00001000,  // IneXact trap Enabled
      // Reserved 13-14
      IDE: 0x00008000,  // Input Denormal trap Enabled
      LEN_1: 0x00000000,  // Number of registers used by each vector
      LEN_2: 0x00010000,
      LEN_3: 0x00020000,
      LEN_4: 0x00030000,
      LEN_5: 0x00040000,
      LEN_6: 0x00050000,
      LEN_7: 0x00060000,
      LEN_8: 0x00070000,
      // Reserved 19
      STRIDE_1: 0x00000000,  // Distance between successive values in a vector
      STRIDE_2: 0x00300000,
      RN : 0x00000000,  // Round to Nearest (half to even)
      RP : 0x00400000,  // Round to Plus Infinity
      RM : 0x00800000,  // Round to Minus Infinity
      RZ : 0x00c00000,  // Round to Zero
      FZ : 0x01000000,  // Flush denormalized to Zero
      DN : 0x02000000,  // Default NaN on NaN propagation
      AHP: 0x04000000,  // Alternative Half-Precision
      QC : 0x08000000,  // VFP Cumulative saturation
      V  : 0x10000000,  // VFP oVerflow condition
      C  : 0x20000000,  // VFP Carry condition
      Z  : 0x40000000,  // VFP Zero condition
      N  : 0x80000000,  // VFP Negative condition
    }),
    reserved: uint32(),
    vfp: array(uint64(), 32),  // d0 to d31
    extra: array(uint32(), 8),  // Miscellaneous control words
  }).set({
    typeName: "MINIDUMP_THREAD_CONTEXT_ARM",
  });

  // MINIDUMP_THREAD_CONTEXT_ARM64 ----------------------------------------ARM64
  // ARM64_NT_CONTEXT
  var minidump_thread_context_arm64 = struct({
    /* Indicates which parts of this structure are valid. */
    context_flags: flags("Context Flags", uint32(), {
      ARM64: 0x00400000,
      CONTROL: 0x00000001,
      INTEGER: 0x00000002,
      FLOATING_POINT: 0x00000004,
      DEBUG: 0x00000008,
      FULL: 0x00000001 // CONTROL
          | 0x00000002 // INTEGER
          | 0x00000004 // FLOATING_POINT
          ,
      ALL: 0x00000001 // CONTROL
         | 0x00000002 // INTEGER
         | 0x00000004 // FLOATING_POINT
         | 0x00000008 // DEBUG
         ,
    }),
    cpsr: minidump_thread_context_arm_cpsr,
    iregs: array(uint64(), 29),
    fp: uint64(),
    lr: uint64(),
    sp: uint64(),
    pc: uint64(),
    float_regs: array(uint128_struct, 32),
    fpcr: flags("Floating-Point Control Register", uint32(), {
      FIZ: 0x00000001,  // Flush denormalized Inputs to Zero
      AH : 0x00000002,  // Alternate Handling (of denormalized floating point)
      NEP: 0x00000004,  // Numeric Extended Precision (Scalar operations affect higher elements in vector registers)
      IOE: 0x00000100,  // Invalid Operations trap Enabled
      DZE: 0x00000200,  // Divide-by-Zero trap Enabled
      OFE: 0x00000400,  // OverFlow trap Enabled
      UFE: 0x00000800,  // UnderFlow trap Enabled
      IXE: 0x00001000,  // IneXact trap Enabled
      IDE: 0x00004000,  // Input Denormal trap Enabled
      // Len 16-18
      FZ16:0x00080000,  // Flush denormalized to Zero with 16 bit floats
      // Stride 20-21
      RN : 0x00000000,  // Round to Nearest (half to even)
      RP : 0x00400000,  // Round to Plus Infinity
      RM : 0x00800000,  // Round to Minus Infinity
      RZ : 0x00c00000,  // Round to Zero
      FZ : 0x01000000,  // Flush denormalized to Zero
      DN : 0x02000000,  // Default NaN on NaN propagation
      AHP: 0x04000000,  // Alternative Half-Precision
    }),
    fpsr: flags("Floating-Point Status Register", uint32(), {
      IOC: 0x00000001,  // Invalid Operation Cumulative
      DZC: 0x00000002,  // Divide by Zero Cumulative
      OFC: 0x00000004,  // OverFlow Cumulative
      UFC: 0x00000008,  // UnderFlow Cumulative
      IXC: 0x00000010,  // IneXact Cumulative
      // Reserved 5-6
      IDC: 0x00000080,  // Input Denormal Cumulative
      // Reserved 8-26
      QC : 0x08000000,  // Cumulative saturation
      V  : 0x10000000,  // AArch32 oVerflow condition
      C  : 0x20000000,  // AArch32 Carry condition
      Z  : 0x40000000,  // AArch32 Zero condition
      N  : 0x80000000,  // AArch32 Negative condition
    }),
    bcr: array(uint32(), 8),
    bvr: array(uint64(), 8),
    wcr: array(uint32(), 2),
    wvr: array(uint64(), 2),
  }).set({
    typeName: "MINIDUMP_THREAD_CONTEXT_ARM64",
  });

  var minidump_location_descriptor_thread_context = taggedUnion(
    { DataSize: uint32(), },
    [
      // DataSize sniffing because SystemInfoStream may not have yet been read.
      alternative(
        // It is possible for this to be larger with XSTATE?
        function(){return this.DataSize.value == 716;},
        { Rva: pointer(uint32(), minidump_thread_context_x86), },
        "ThreadContextX64Entry"),
      alternative(
        // It is possible for this to be larger with XSTATE?
        function(){return this.DataSize.value == 1232;},
        { Rva: pointer(uint32(), minidump_thread_context_amd64), },
        "ThreadContextAmd64Entry"),
      alternative(
        function(){return this.DataSize.value == 368;},
        { Rva: pointer(uint32(), minidump_thread_context_arm), },
        "ThreadContextArm64Entry"),
      alternative(
        function(){return this.DataSize.value == 912;},
        { Rva: pointer(uint32(), minidump_thread_context_arm64), },
        "ThreadContextArm64Entry"),
    ],
    {
      Rva: pointer(
        uint32(),
        array(uint8(), function(){return this.parent.parent.DataSize.value;})
      ),
    }
  ).set({
    typeName: "struct MINIDUMP_THREAD_CONTEXT",
  });

  var module_stream_type = {
    UnusedStream: 0,
    ReservedStream0: 1,
    ReservedStream1: 2,
    ThreadListStream: 3,  // Done (X86, AMD64, ARM, ARM64)
    ModuleListStream: 4,  // Done
    MemoryListStream: 5,  // Done
    ExceptionStream: 6,  // Done (X86, AMD64, ARM, ARM64)
    SystemInfoStream: 7,  // Done
    ThreadExListStream: 8,
    Memory64ListStream: 9,
    CommentStreamA: 10,
    CommentStreamW: 11,
    HandleDataStream: 12,  // Done
    FunctionTableStream: 13,
    UnloadedModuleListStream: 14,  // Done
    MiscInfoStream: 15,  // Done
    MemoryInfoListStream: 16,  // Done
    ThreadInfoListStream: 17,
    HandleOperationListStream: 18,
    TokenStream: 19,
    JavaScriptDataStream: 20,
    SystemMemoryInfoStream: 21,
    ProcessVmCountersStream: 22,
    IptTraceStream: 23,
    ThreadNamesStream: 24,  // Done
    ceStreamNull: 0x8000,
    ceStreamSystemInfo: 0x8001,
    ceStreamException: 0x8002,
    ceStreamModuleList: 0x8003,
    ceStreamProcessList: 0x8004,
    ceStreamThreadList: 0x8005,
    ceStreamThreadContextList: 0x8006,
    ceStreamThreadCallStackList: 0x8007,
    ceStreamMemoryVirtualList: 0x8008,
    ceStreamMemoryPhysicalList: 0x8009,
    ceStreamBucketParameters: 0x800A,
    ceStreamProcessModuleMap: 0x800B,
    ceStreamDiagnosisList: 0x800C,
    LastReservedStream: 0xffff,
    /* Breakpad extension types.  0x4767 = "Gg" */
    BreakpadInfoStream: 0x47670001,  // MDRawBreakpadInfo
    BreakpadAssertionInfoStream: 0x47670002,  // MDRawAssertionInfo
    BreakpadLinuxCpuInfo: 0x47670003,  // /proc/cpuinfo
    BreakpadLinuxProcStatus: 0x47670004,  // /proc/$x/status
    BreakpadLinuxLsbRelease: 0x47670005,  // /etc/lsb-release
    BreakpadLinuxCmdLine: 0x47670006,  // /proc/$x/cmdline
    BreakpadLinuxEnviron: 0x47670007,  // /proc/$x/environ
    BreakpadLinuxAuxV: 0x47670008,  // /proc/$x/auxv
    BreakpadLinuxMaps: 0x47670009,  // /proc/$x/maps
    BreakpadLinuxDsoDebug: 0x4767000A,  // MDRawDebug{32,64}
    /* Crashpad extension types. 0x4350 = "CP"
     * See Crashpad's minidump/minidump_extensions.h. */
    CrashpadInfoStream: 0x43500001,  /* MDRawCrashpadInfo  */  // Done

    /* Crashpad allows user streams, see crashpad::UserStreamDataSource.
     * These are Chromium specific protobuf streams. 0x4B6B = "Kk" */
    ChromiumStabilityReport: 0x4B6B0002, // components/stability_report/stability_report.proto
    ChromiumSystemProfile: 0x4B6B0003, // third_party/metrics_proto/system_profile.proto
    ChromiumGwpAsanCrash: 0x4B6B0004, // components/gwp_asan/crash_handler/crash.proto
  };


  // ThreadListStream: 3-------------------------------------------------------3
  var minidump_thread = struct({
    ThreadId: uint32(),
    SuspendCount: uint32(),
    PriorityClass: uint32(),
    Priority: uint32(),
    Teb: uint64(),
    StackStartOfMemoryRange: uint64(),
    StackMemoryDataSize: uint32(),
    StackMemoryRva: pointer(
      uint32(),
      array(uint8(),
            function(){return this.parent.parent.StackMemoryDataSize.value;})),
    ThreadContext: minidump_location_descriptor_thread_context,
  }).set({
    typeName: "MINIDUMP_THREAD",
    toStringFunc: function(){ return "0x" + this.ThreadId.value.toString(16); },
  });

  var minidump_thread_list = struct({
    NumberOfThreads: uint32(),
    Threads: array(minidump_thread,
                   function(){return this.parent.NumberOfThreads.value;}),
  }).set({
    typeName: "MINIDUMP_THREAD_LIST"
  });


  // ModuleListStream: 4-------------------------------------------------------4
  var vs_fixedfileinfo = struct({
    Signature: uint32(),
    StrucVersion: uint32(),
    FileVersionMS: uint32(),
    FileVersionLS: uint32(),
    ProductVersionMS: uint32(),
    ProductVersionLS: uint32(),
    FileFlagsMask: uint32(),
    FileFlags: uint32(),
    FileOS: uint32(),
    FileType: uint32(),
    FileSubtype: uint32(),
    FileDateMS: uint32(),
    FileDateLS: uint32(),
  }).set({
    typeName: "VS_FIXEDFILEINFO"
  });

  var minidump_CVInfo = taggedUnion(
    {
      CVSignature: uint32(),
    },
    [
      alternative(
        function(){return this.CVSignature.value == 0x3031424e;/*01BN*/},
        {
          CVOffset: uint32(),  // Offset to debug data (0)
          Signature: uint32(),  // Symbol file id
          Age: uint32(),
          PDBFileName: string("utf-8").set({  // Symbol file (UTF-8?)
            terminatedBy: 0,
            updateFunc: function(){
              const structSize = this.parent.parent.parent.CvRecordDataSize.value;
              this.maxByteCount = structSize - (4+4+4+4);
            },
          }),
        },
        "CVInfoPDB20"),
      alternative(
        function(){return this.CVSignature.value == 0x53445352;/*SDSR*/},
        {
          Signature: minidump_guid,  // Symbol file id
          Age: uint32(),  // Symbol file version
          PDBFileName: string("utf-8").set({  // Symbol file (UTF-8?)
            terminatedBy: 0,
            updateFunc: function(){
              const structSize = this.parent.parent.parent.CvRecordDataSize.value;
              this.maxByteCount = structSize - (4+16+4);
            },
          }),
        },
        "CVInfoPDB70"),
      alternative(
        function(){return this.CVSignature.value == 0x4270454c;/*BpEL*/},
        {
          Signature: minidump_guid,  // Symbol file id
        },
        "CVInfoELF"),
    ],
    {}
  );

  var minidump_image_debug_misc = struct({
    DataType: uint32(), // IMAGE_DEBUG_TYPE_*
    Length: uint32(), // Length of entire ImageDebugMisc structure
    Unicode: bool8(), // Data is multibyte if true
    Reserved: array(uint8(), 3),
    Data: string("utf-8").set({
      terminatedBy: 0,
      updateFunc: function(){
        const structSize = this.parent.parent.parent.MiscRecordDataSize.value;
        this.maxByteCount = structSize - (4+4+1+3);
        if (this.parent.Unicode.value) {
          this.encoding = "utf-16";
        }
      },
    }),
  }).set({
    typeName: "ImageDebugMisc",
  });

  var minidump_module = struct({
    BaseOfImage: uint64(),
    SizeOfImage: uint32(),
    CheckSum: uint32(),
    TimeDateStamp: uint32().set({
      typeName: "time_t",
      toStringFunc: function(){return new Date(this.value*1000);},
    }),
    ModuleNameRva: pointer(uint32(), minidump_string),
    VersionInfo: vs_fixedfileinfo,
    CvRecordDataSize: uint32(),
    CvRecordRva: pointer(uint32(), minidump_CVInfo).set({
      updateFunc: point_at_nothing_if_size_is_zero,
    }),
    MiscRecordDataSize: uint32(),
    MiscRecordRva: pointer(uint32(), minidump_image_debug_misc).set({
      updateFunc: point_at_nothing_if_size_is_zero,
    }),
    Reserved0: uint64(),
    Reserved1: uint64(),
  }).set({
    typeName: "MINIDUMP_MODULE",
    toStringFunc: function(){
      return this.ModuleNameRva.target.toStringFunc()
           + " -o 0x"
           + this.BaseOfImage.uint64high32.toString(16)
           + ("00000000"+this.BaseOfImage.uint64low32.toString(16)).slice(-8);
    },
  });

  var minidump_module_list = struct({
    NumberOfModules: uint32(),
    Modules: array(minidump_module,
                   function(){ return this.parent.NumberOfModules.value; }),
  }).set({
    typeName: "MINIDUMP_MODULE_LIST"
  });


  //MemoryListStream: 5
  var minidump_memory_descriptor = struct({
    StartOfMemoryRange: uint64(),
    MemoryDataSize: uint32(),
    MemoryRva: pointer(
      uint32(),
      array(uint8(), function(){return this.parent.parent.MemoryDataSize.value;})),
  }).set({
    typeName: "MINIDUMP_MEMORY_DESCRIPTOR",
    toStringFunc: function(){
      return "0x"
           + this.StartOfMemoryRange.uint64high32.toString(16)
           + ("00000000"+this.StartOfMemoryRange.uint64low32.toString(16)).slice(-8)
           + "[0x"
           + this.MemoryDataSize.value.toString(16)
           + "]";
    },
  });
  var minidump_memory_list = struct({
    NumberOfMemoryRanges: uint32(),
    MemoryRanges: array(
      minidump_memory_descriptor,
      function(){return this.parent.NumberOfMemoryRanges.value;}),
  }).set({
    typeName: "MINIDUMP_MEMORY_LIST",
  });


  //ExceptionStream: 6---------------------------------------------------------6
  var minidump_exception_linux_code = {
    SIGHUP: 1,      /* Hangup (POSIX) */
    SIGINT: 2,      /* Interrupt (ANSI) */
    SIGQUIT: 3,     /* Quit (POSIX) */
    SIGILL: 4,      /* Illegal instruction (ANSI) */
    SIGTRAP: 5,     /* Trace trap (POSIX) */
    SIGABRT: 6,     /* Abort (ANSI) */
    SIGBUS: 7,      /* BUS error (4.2 BSD) */
    SIGFPE: 8,      /* Floating-point exception (ANSI) */
    SIGKILL: 9,     /* Kill, unblockable (POSIX) */
    SIGUSR1: 10,    /* User-defined signal 1 (POSIX).  */
    SIGSEGV: 11,    /* Segmentation violation (ANSI) */
    SIGUSR2: 12,    /* User-defined signal 2 (POSIX) */
    SIGPIPE: 13,    /* Broken pipe (POSIX) */
    SIGALRM: 14,    /* Alarm clock (POSIX) */
    SIGTERM: 15,    /* Termination (ANSI) */
    SIGSTKFLT: 16,  /* Stack faultd */
    SIGCHLD: 17,    /* Child status has changed (POSIX) */
    SIGCONT: 18,    /* Continue (POSIX) */
    SIGSTOP: 19,    /* Stop, unblockable (POSIX) */
    SIGTSTP: 20,    /* Keyboard stop (POSIX) */
    SIGTTIN: 21,    /* Background read from tty (POSIX) */
    SIGTTOU: 22,    /* Background write to tty (POSIX) */
    SIGURG: 23,     /* Urgent condition on socket (4.2 BSD) */
    SIGXCPU: 24,    /* CPU limit exceeded (4.2 BSD) */
    SIGXFSZ: 25,    /* File size limit exceeded (4.2 BSD) */
    SIGVTALRM: 26,  /* Virtual alarm clock (4.2 BSD) */
    SIGPROF: 27,    /* Profiling alarm clock (4.2 BSD) */
    SIGWINCH: 28,   /* Window size change (4.3 BSD, Sun) */
    SIGIO: 29,      /* I/O now possible (4.2 BSD) */
    SIGPWR: 30,     /* Power failure restart (System V) */
    SIGSYS: 31,     /* Bad system call */
    DUMP_REQUESTED: 0xFFFFFFFF /* No exception, dump requested. */
  };
  var minidump_exception_linux_flags_ill = enumeration("SIGILL", uint32(), {
    ILLOPC: 1,
    ILLOPN: 2,
    ILLADR: 3,
    ILLTRP: 4,
    PRVOPC: 5,
    PRVREG: 6,
    COPROC: 7,
    BADSTK: 8,
  });
  var minidump_exception_linux_flags_fpe = enumeration("SIGFPE", uint32(), {
    INTDIV: 1,
    INTOVF: 2,
    FLTDIV: 3,
    FLTOVF: 4,
    FLTUND: 5,
    FLTRES: 6,
    FLTINV: 7,
    FLTSUB: 8,
  });
  var minidump_exception_linux_flags_segv = enumeration("SIGSEGV", uint32(), {
    MAPERR: 1,
    ACCERR: 2,
    BNDERR: 3,
    PKUERR: 4,
  });
  var minidump_exception_linux_flags_bus = enumeration("SIGBUS", uint32(), {
    ADRALN: 1,
    ADRERR: 2,
    OBJERR: 3,
    MCEERR_AR: 4,
    MCEERR_AO: 5,
  });
  var minidump_exception_linux_flags = {};
  minidump_exception_linux_flags[minidump_exception_linux_code.SIGILL] = minidump_exception_linux_flags_ill;
  minidump_exception_linux_flags[minidump_exception_linux_code.SIGFPE] = minidump_exception_linux_flags_fpe;
  minidump_exception_linux_flags[minidump_exception_linux_code.SIGSEGV] = minidump_exception_linux_flags_segv;
  minidump_exception_linux_flags[minidump_exception_linux_code.SIGBUS] = minidump_exception_linux_flags_bus;

  var minidump_exception_stream = struct({
    ThreadId: uint32(),
    __alignment: uint32(),
    // ExceptionRecord: MINIDUMP_EXCEPTION inlined for ease of use.

    // The toStringFunc can only read bytes that have already been read.
    // At this point no bytes have been read to indicate the platform.
    // The SystemInfoStream may not have been read.
    // The parent.parent.ThreadContext has not been read (size guess).
    // Therefore the ExceptionCode must show all possibilities.
    ExceptionCode: union({
      Linux: enumeration("ExceptionCode_Linux", uint32(), minidump_exception_linux_code),
      Raw: uint32(),
    }).set({
      toStringFunc: function(){return "0x" + this.Raw.value.toString(16);},
    }),
    ExceptionFlags: union({
      Raw: uint32(),
    }).set({
      updateFunc: function(){
        const linux_code = this.parent.ExceptionCode.Linux.value;
        var children = {};
        if (minidump_exception_linux_flags[linux_code]) {
          children["Linux"] = minidump_exception_linux_flags[linux_code];
        }
        children["Raw"] = uint32();
        this.fields = children;
      },
      toStringFunc: function(){return "0x" + this.Raw.value.toString(16);},
    }),
    ExceptionRecord: uint64(),
    ExceptionAddress: uint64(),
    NumberParameters: uint32(),
    __unusedAlignment: uint32(),
    ExceptionInformation: array(uint64(), 15 /*EXCEPTION_MAXIMUM_PARAMETERS*/),

    ThreadContext: minidump_location_descriptor_thread_context,
  }).set({
    typeName: "MINIDUMP_EXCEPTION_STREAM",
  });


  // SystemInfoStream: 7-------------------------------------------------------7
  var processor_architecture = {
    INTEL_X86: 0,
    MIPS: 1,
    ALPHA: 2,
    PPC: 3,
    SHX: 4,
    ARM: 5,
    IA64: 6,
    ALPHA64: 7,
    MSIL: 8,
    AMD64: 9,
    IA32_ON_WIN64: 10,
    ARM64: 12,
    /* Breakpad-defined */
    SPARC: 0x8001,
    PPC64: 0x8002,
    ARM64_OLD: 0x8003,
    MIPS64: 0x8004,
    RISCV: 0x8005,
    RISCV64: 0x8006,
    /* Unknown */
    UNKNOWN: 0xffff,
  };
  var platform_id = {
    WIN32S: 0,  // Windows 3.1
    WINDOWS: 1,  // Windows 95-98-Me
    WIN32_NT: 2,  // Windows NT, 2000+
    WIN32_CE: 3,  // CE, Mobile, Handheld

    // Breakpad-defined
    UNIX: 0x8000,
    MAC_OS_X: 0x8101,
    IOS: 0x8102,
    LINUX: 0x8201,
    SOLARIS: 0x8202,
    ANDROID: 0x8203,
    PS3: 0x8204,
    NACL: 0x8205,
    FUCHSIA: 0x8206
  };

  var minidump_system_info = struct({
    ProcessorArchitecture: enumeration("ProcessorArchitecture", uint16(), processor_architecture),
    ProcessorLevel: uint16(),
    ProcessorRevision: uint16(),
    NumberOfProcessors: uint8(),
    ProductType: uint8(),
    MajorVersion: uint32(),
    MinorVersion: uint32(),
    BuildNumber: uint32(),
    PlatformId: enumeration("PlatformId", uint32(), platform_id),
    CSDVersionRva: pointer(uint32(), minidump_string),
    SuiteMask: uint16(),
    Reserved2: uint16(),
    Cpu: array(uint8(), 24), //CPU_INFORMATION
  }).set({
    typeName: "MINIDUMP_SYSTEM_INFO",
  });


  //HandleDataStream: 12------------------------------------------------------12
  var minidump_handle_descriptor_toStringFunc = function(){
    var s = "0x";
    if (this.Handle.uint64high32 != 0) {
      s += this.Handle.uint64high32.toString(16)
          + ("00000000"+this.Handle.uint64low32.toString(16)).slice(-8);
    } else {
      s += this.Handle.uint64low32.toString(16);
    }
    if (this.TypeNameRva.value) {
      s += " " + this.TypeNameRva.target.toStringFunc();
    }
    if (this.ObjectNameRva.value) {
      s += " " + this.ObjectNameRva.target.toStringFunc();
    }
    return s;
  };
  var minidump_handle_descriptor = struct({
    Handle: uint64(),
    TypeNameRva: pointer(uint32(), minidump_string).set({
      interpretFunc: point_at_uint32max_if_zero,
    }),
    ObjectNameRva: pointer(uint32(), minidump_string).set({
      interpretFunc: point_at_uint32max_if_zero,
    }),
    Attributes: uint32(),
    GrantedAccess: uint32(),
    HandleCount: uint32(),
    PointerCount: uint32(),
  }).set({
    typeName: "MINIDUMP_HANDLE_DESCRIPTOR",
    toStringFunc: minidump_handle_descriptor_toStringFunc,
  });
  var minidump_handle_descriptor_2 = struct({
    Handle: uint64(),
    TypeNameRva: pointer(uint32(), minidump_string).set({
      interpretFunc: point_at_uint32max_if_zero,
    }),
    ObjectNameRva: pointer(uint32(), minidump_string).set({
      interpretFunc: point_at_uint32max_if_zero,
    }),
    Attributes: uint32(),
    GrantedAccess: uint32(),
    HandleCount: uint32(),
    PointerCount: uint32(),
    ObjectInfoRva: uint32(), // Nullable MINIDUMP_HANDLE_OBJECT_INFORMATION RVA
    Reserved0: uint32(),
  }).set({
    typeName: "MINIDUMP_HANDLE_DESCRIPTOR_2",
    toStringFunc: minidump_handle_descriptor_toStringFunc,
  });
  var minidump_handle_data_stream = taggedUnion(
    {
      SizeOfHeader: uint32(),
      SizeOfDescriptor: uint32(),
      NumberOfDescriptors: uint32(),
      Reserved: uint32(),
    },
    [
      alternative(
        function(){return this.SizeOfHeader.value != 4+4+4+4},
        { Descriptors: array(uint8(), 0), },
        "MINIDUMP_HANDLE_DESCRIPTOR_UNKNOWN"),
      alternative(
        function(){return this.SizeOfDescriptor.value == 8+4+4+4+4+4+4},
        { Descriptors: array(minidump_handle_descriptor,
            function(){return this.parent.NumberOfDescriptors.value}), },
        "MINIDUMP_HANDLE_DESCRIPTOR"),
      alternative(
        function(){return this.SizeOfDescriptor.value == 8+4+4+4+4+4+4+4+4},
        { Descriptors: array(minidump_handle_descriptor_2,
            function(){return this.parent.NumberOfDescriptors.value}), },
        "MINIDUMP_HANDLE_DESCRIPTOR_2"),
    ],
    {
      Descriptors: array(
        array(uint8(),
              function(){return this.parent.parent.SizeOfDescriptor.value;}),
        function(){return this.parent.NumberOfDescriptors.value}),
    }
  ).set({
    typeName: "MINIDUMP_HANDLE_DATA_STREAM",
  });


  //UnloadedModuleListStream: 14----------------------------------------------14
  var minidump_unloaded_module = struct({
    BaseOfImage: uint64(),
    SizeOfImage: uint32(),
    CheckSum: uint32(),
    TimeDateStamp: uint32().set({
      typeName: "time_t",
      toStringFunc: function(){return new Date(this.value*1000);},
    }),
    ModuleNameRva: pointer(uint32(), minidump_string).set({
      interpretFunc: point_at_uint32max_if_zero,
    }),
  }).set({
    typeName: "MINIDUMP_UNLOADED_MODULE",
    toStringFunc: function(){
      return this.ModuleNameRva.target.toStringFunc()
           + " -o 0x"
           + this.BaseOfImage.uint64high32.toString(16)
           + ("00000000"+this.BaseOfImage.uint64low32.toString(16)).slice(-8);
    },
  });
  var minidump_unloaded_module_list = struct({
    SizeOfHeader: uint32(), // usually sizeof(MINIDUMP_UNLOADED_MODULE_LIST)
    SizeOfEntry: uint32(),  // usually sizeof(MINIDUMP_UNLOADED_MODULE)
    NumberOfEntries: uint32(),
    Entries: array(
      minidump_unloaded_module,
      function(){
        if (this.parent.SizeOfHeader.value != 4+4+4 ||
            this.parent.SizeOfEntry.value != 8+4+4+4+4)
        {
          return 0;
        }
        return this.parent.NumberOfEntries.value;
      }),
  }).set({
    typeName: "MINIDUMP_UNLOADED_MODULE_LIST",
  });


  //MiscInfoStream: 15--------------------------------------------------------15
  var systemtime = struct({
    Year: uint16(),
    Month: uint16(),
    DayOfWeek: uint16(),
    Day: uint16(),
    Hour: uint16(),
    Minute: uint16(),
    Second: uint16(),
    Milliseconds: uint16(),
  }).set({
    typeName: "SYSTEMTIME",
  });
  var time_zone_information = struct({
    Bias: int32(),
    StandardName: string("utf-16").set({maxByteCount: 32*2, terminatedBy: null,}),
    StandardDate: systemtime,
    StandardBias: int32(),
    DaylightName: string("utf-16").set({maxByteCount: 32*2, terminatedBy: null,}),
    DaylightDate: systemtime,
    DaylightBias: int32(),
  }).set({
    typeName: "TIME_ZONE_INFORMATION",
  });

  var minidump_xstate_feature = struct({
    Offset: uint32(),
    Size: uint32(),
  }).set({
    typeName: "XSTATE_FEATURE",
  });
  var minidump_xstate_config_feature_msc_info = struct({
    SizeOfInfo: uint32(),
    ContextSize: uint32(),
    enabled_features: flags("XSTATE_FEATURE_FLAG", uint64(), {
      LEGACY_FLOATING_POINT: 0,
      LEGACY_SSE: 1,
      GSSE: 2,
      AVX: 2,
      MPX_BNDREGS: 3,
      MPX_BNDCSR: 4,
      AVX512_KMASK: 5,
      AVX512_ZMM_H: 6,
      AVX512_ZMM: 7,
      IPT: 8,
      LWP: 62,
    }),
    features: array(minidump_xstate_feature, 64),
  }).set({
    typeName: "XSTATE_CONFIG_FEATURE_MSC_INFO",
  });

  var minidump_misc_info_fields = [
    { name: "SizeOfInfo", type: uint32(), size: 4 },
    { name: "Flags1",
      type: flags("Flags", uint32(), {
        PROCESS_ID: 0x00000001,
        PROCESS_TIMES: 0x00000002,
        PROCESSOR_POWER_INFO: 0x00000004,
        PROCESS_INTEGRITY: 0x00000010,
        PROCESS_EXECUTE_FLAGS: 0x00000020,
        TIMEZONE: 0x00000040,
        PROTECTED_PROCESS: 0x00000080,
        BUILDSTRING: 0x00000100,
        PROCESS_COOKIE: 0x00000200,
      }),
      size: 4
    },
    /* PROCESS_ID */
    { name: "ProcessId", type: uint32(), size: 4 },
    /* PROCESS_TIMES */
    { name: "ProcessCreateTime",
      type: uint32().set({
        typeName: "time_t",
        toStringFunc: function(){return new Date(this.value*1000);},
      }),
      size: 4 },
    { name: "ProcessUserTime", type: uint32(), size: 4 },
    { name: "ProcessKernelTime", type: uint32(), size: 4,
      structName: "MINIDUMP_MISC_INFO" },

    /* PROCESSOR_POWER_INFO */
    { name: "ProcessorMaxMhz", type: uint32(), size: 4,
      structName: "MINIDUMP_MISC_INFO+"  },
    { name: "ProcessorCurrentMhz", type: uint32(), size: 4 },
    { name: "ProcessorMhzLimit", type: uint32(), size: 4 },
    { name: "ProcessorMaxIdleState", type: uint32(), size: 4 },
    { name: "ProcessorCurrentIdleState", type: uint32(), size: 4,
      structName: "MINIDUMP_MISC_INFO_2" },

    /* PROCESS_INTEGRITY */
    { name: "ProcessIntegrityLevel", type: uint32(), size: 4,
      structName: "MINIDUMP_MISC_INFO_2+" },
    /* PROCESS_EXECUTE_FLAGS */
    { name: "ProcessExecuteFlags", type: uint32(), size: 4 },
    /* PROTECTED_PROCESS */
    { name: "ProtectedProcess", type: uint32(), size: 4 },
    /* TIMEZONE */
    { name: "TimeZoneId", type: uint32(), size: 4 },
    { name: "TimeZone", type: time_zone_information, size: 172,
      structName: "MINIDUMP_MISC_INFO_3" },

    /* BUILDSTRING */
    { name: "BuildString",
      type: string("utf-16").set({maxByteCount: 260*2, terminatedBy: null,}),
      size: 260*2,
      structName: "MINIDUMP_MISC_INFO_3+" },
    { name: "DbgBldStr",
      type: string("utf-16").set({maxByteCount: 40*2, terminatedBy: null,}),
      size: 40*2,
      structName: "MINIDUMP_MISC_INFO_4" },

    { name: "XStateData",
      type: minidump_xstate_config_feature_msc_info,
      size: 544,
      structName: "MINIDUMP_MISC_INFO_4+" },
    /* PROCESS_COOKIE */
    { name: "ProcessCookie", type: uint32(), size: 4,
      structName: "MINIDUMP_MISC_INFO_5" },
  ];

  var minidump_misc_info = struct({
    SizeOfInfo: uint32(),
  }).set({
    updateFunc: function(){
      var children = {};
      var childEnd = 0;
      for (var i = 0; i < minidump_misc_info_fields.length; ++i) {
        const child = minidump_misc_info_fields[i];
        childEnd += child.size;
        if (this.parent.parent.DataSize.value < childEnd) { break; }
        children[child.name] = child.type;
        if (child.structName) { this.typeName = child.structName; }
      }
      this.fields = children;
    },
  });


  //MemoryInfoListStream: 16--------------------------------------------------16
  var minudump_memory_info_protection =  flags("Memory Protection", uint32(), {
    PAGE_NOACCESS: 0x01,
    PAGE_READONLY: 0x02,
    PAGE_READWRITE: 0x04,
    PAGE_WRITECOPY: 0x08,
    PAGE_EXECUTE: 0x10,
    PAGE_EXECUTE_READ: 0x20,
    PAGE_EXECUTE_READWRITE: 0x40,
    PAGE_EXECUTE_WRITECOPY: 0x80,
    PAGE_GUARD: 0x100,
    PAGE_NOCACHE: 0x200,
    PAGE_WRITECOMBINE: 0x400,
    PAGE_TARGETS_INVALID: 0x40000000,
    PAGE_TARGETS_NO_UPDATE: 0x40000000,
  });
  // Minidump version of MEMORY_BASIC_INFORMATION
  var minidump_memory_info = struct({
    BaseAddress: uint64(),
    AllocationBase: uint64(),
    AllocationProtect: minudump_memory_info_protection,
    __alignment1: uint32(),
    RegionSize: uint64(),
    State: enumeration("State", uint32(), {
      MEM_COMMIT     : 0x00001000,
      MEM_RESERVE    : 0x00002000,
      MEM_DECOMMIT   : 0x00004000,
      MEM_RELEASE    : 0x00008000,
      MEM_FREE       : 0x00010000,
      //MEM_RESET      : 0x00080000,
      //MEM_TOP_DOWN   : 0x00100000,
      //MEM_WRITE_WATCH: 0x00200000,
      //MEM_PHYSICAL   : 0x00400000,
      //MEM_RESET_UNDO : 0x01000000,
      //MEM_LARGE_PAGES: 0x20000000,
    }),
    Protect: minudump_memory_info_protection,
    Type: enumeration("Type", uint32(), {
      MEM_PRIVATE    : 0x00020000,
      MEM_MAPPED     : 0x00040000,
      MEM_IMAGE      : 0x01000000,
    }),
    __alignment2: uint32(),
  }).set({
    typeName: "MINIDUMP_MEMORY_INFO",
    toStringFunc: function(){
      const state = getKeyFromValue(this.State.enumValues, this.State.value);
      return "0x"
           + this.BaseAddress.uint64high32.toString(16)
           + ("00000000"+this.BaseAddress.uint64low32.toString(16)).slice(-8)
           + "[0x"
           + this.RegionSize.uint64high32.toString(16)
           + ("00000000"+this.RegionSize.uint64low32.toString(16)).slice(-8)
           + "] "
           + (state ? state : "");
    },
  });
  var minidump_memory_info_list = struct({
    SizeOfHeader: uint32(), // usually sizeof(MINIDUMP_MEMORY_INFO_LIST)
    SizeOfEntry: uint32(),  // usually sizeof(MINIDUMP_MEMORY_INFO)
    NumberOfEntries: uint64(),
    Entries: array(
      minidump_memory_info,
      function(){
        if (this.parent.SizeOfHeader.value != 4+4+8 ||
            this.parent.SizeOfEntry.value != 8+8+4+4+8+4+4+4+4)
        {
          return 0;
        }
        return this.parent.NumberOfEntries.value;
      }),
  }).set({
    typeName: "MINIDUMP_MEMORY_INFO_LIST",
  });


  //ThreadNamesStream: 24-----------------------------------------------------24
  var minidump_thread_name = struct({
    ThreadId: uint32(),
    ThreadNameRva: pointer(uint64(), minidump_string).set({
      interpretFunc: point_at_uint32max_if_zero,
    }),
  }).set({
    typeName: "MINIDUMP_THREAD_NAME",
    toStringFunc: function(){
      return "0x"
           + this.ThreadId.value.toString(16)
           + " "
           + this.ThreadNameRva.target.toStringFunc();
    },
  });
  var minidump_thread_name_list = struct({
    NumberOfThreadNames: uint32(),
    Names: array(minidump_thread_name,
                 function(){return this.parent.NumberOfThreadNames.value;}),
  }).set({
    typeName: "MINIDUMP_THREAD_NAMES_LIST",
  });


  //CrashpadInfoStream: 0x43500001------------------------------------0x43500001
  minidump_crashpad_string_list = struct({
    StringCount: uint32(),
    Strings: array(
      pointer(uint32(), minidump_utf8_string).set({
        interpretFunc: point_at_uint32max_if_zero,
      }),
      function(){return this.parent.StringCount.value;}),
  }).set({
    typeName: "StringList",
  });
  minidump_crashpad_simple_string_dictionary_entry = struct({
    Key: pointer(uint32(), minidump_utf8_string),
    Value: pointer(uint32(), minidump_utf8_string),
  }).set({
    typeName: "SimpleStringDictionaryEntry",
    toStringFunc: function(){
      return this.Key.target.toStringFunc() + ": "
           + this.Value.target.toStringFunc();
    }
  });
  minidump_crashpad_simple_string_dictionary = struct({
    EntryCount: uint32(),
    Entries: array(minidump_crashpad_simple_string_dictionary_entry,
                   function(){return this.parent.EntryCount.value;}),
  }).set({
    typeName: "SimpleStringDictionary",
  });
  var minidump_crashpad_annotation = struct({
    Name: pointer(uint32(), minidump_utf8_string),
    Type: uint16(),
    Reserved: uint16(),
    Value: pointer(uint32(), minidump_utf8_string),
  }).set({
    typeName: "CrashpadAnnotation",
    toStringFunc: function(){
      return this.Name.target.toStringFunc() + ": "
           + this.Value.target.toStringFunc();
    }
  });
  var minidump_crashpad_annotation_list = struct({
    AnnotationCount: uint32(),
    Annotations: array(minidump_crashpad_annotation,
                       function(){return this.parent.AnnotationCount.value;}),
  }).set({
    typeName: "CrashpadAnnotationList",
  });

  var minidump_crashpad_module_info = struct({
    Version: uint32(),
    ListAnnotationsDataSize: uint32(),
    ListAnnotationsRva: pointer(
      uint32(),
      minidump_crashpad_string_list).set({
        updateFunc: point_at_nothing_if_size_is_zero,
      }),
    SimpleAnnotationsDataSize: uint32(),
    SimpleAnnotationsRva: pointer(
      uint32(),
      minidump_crashpad_simple_string_dictionary).set({
        updateFunc: point_at_nothing_if_size_is_zero,
      }),
    AnnotationObjectsDataSize: uint32(),
    AnnotationObjectsRva: pointer(
      uint32(),
      minidump_crashpad_annotation_list).set({
        updateFunc: point_at_nothing_if_size_is_zero,
      }),
  }).set({
    typeName: "CrashpadInfo",
  });
  var minidump_crashpad_module_info_link = struct({
    ModuleListIndex: uint32(),
    ModuleInfoDataSize: uint32(),
    ModuleInfoRva: pointer(uint32(), minidump_crashpad_module_info).set({
      updateFunc: point_at_nothing_if_size_is_zero,
    }),
  }).set({
    typeName: "CrashpadInfoLink",
  });
  var minidump_crashpad_module_info_list = struct({
    ModuleInfoCount: uint32(),
    ModuleInfos: array(minidump_crashpad_module_info_link,
                       function(){return this.parent.ModuleInfoCount.value;}),
  }).set({
    typeName: "CrashpadInfoList",
  });

  var minidump_crashpad_info_stream = struct({
    Version: int32(),
    ReportId: minidump_guid,
    ClientId: minidump_guid,
    SimpleAnnotationsDataSize: uint32(),
    SimpleAnnotationsRva: pointer(
      uint32(),
      minidump_crashpad_simple_string_dictionary).set({
        updateFunc: point_at_nothing_if_size_is_zero,
      }),
    ModuleListDataSize: uint32(),
    ModuleListRva: pointer(uint32(), minidump_crashpad_module_info_list).set({
      updateFunc: point_at_nothing_if_size_is_zero,
    }),
    Reserved: uint32(),
    AddressMask: uint64(),
  }).set({
    typeName: "CrashpadInfo",
  });


  // Header
  var minidump_directory_entry = taggedUnion(
    {
      StreamType: enumeration("MINIDUMP_STREAM_TYPE", uint32(), module_stream_type),
      DataSize: uint32(),
    },
    [
      alternative(  //ThreadListStream: 3
        function(){return this.StreamType.value == module_stream_type.ThreadListStream;},
        { Rva: pointer(uint32(), minidump_thread_list), },
        "ThreadListStreamEntry"),
      alternative(  //ModuleListStream: 4
        function(){return this.StreamType.value == module_stream_type.ModuleListStream;},
        { Rva: pointer(uint32(), minidump_module_list), },
        "ModuleListStreamEntry"),
      alternative(  //MemoryListStream: 5
        function(){return this.StreamType.value == module_stream_type.MemoryListStream;},
        { Rva: pointer(uint32(), minidump_memory_list), },
        "MemoryListStreamEntry"),
      alternative(  //ExceptionStream: 6
        function(){return this.StreamType.value == module_stream_type.ExceptionStream;},
        { Rva: pointer(uint32(), minidump_exception_stream), },
        "ExceptionStreamEntry"),
      alternative(  //SystemInfoStream: 7
        function(){return this.StreamType.value == module_stream_type.SystemInfoStream;},
        { Rva: pointer(uint32(), minidump_system_info), },
        "SystemInfoStreamEntry"),
      alternative(  //HandleDataStream: 12
        function(){return this.StreamType.value == module_stream_type.HandleDataStream;},
        { Rva: pointer(uint32(), minidump_handle_data_stream), },
        "HandleDataStreamEntry"),
      alternative(  //UnloadedModuleListStream: 14
        function(){return this.StreamType.value == module_stream_type.UnloadedModuleListStream;},
        { Rva: pointer(uint32(), minidump_unloaded_module_list), },
        "UnloadedModuleListStreamEntry"),
      alternative(  //MiscInfoStream: 15
        function(){return this.StreamType.value == module_stream_type.MiscInfoStream;},
        { Rva: pointer(uint32(), minidump_misc_info), },
        "MiscInfoStreamEntry"),
      alternative(  //MemoryInfoListStream: 16
        function(){return this.StreamType.value == module_stream_type.MemoryInfoListStream;},
        { Rva: pointer(uint32(), minidump_memory_info_list), },
        "MemoryInfoListStreamEntry"),
      alternative(  //ThreadNamesStream: 24
        function(){return this.StreamType.value == module_stream_type.ThreadNamesStream;},
        { Rva: pointer(uint32(), minidump_thread_name_list), },
        "ThreadNamesListStreamEntry"),
      alternative(  //CrashpadInfoStream: 0x43500001
        function(){return this.StreamType.value == module_stream_type.CrashpadInfoStream;},
        { Rva: pointer(uint32(), minidump_crashpad_info_stream), },
        "CrashpadInfoStreamEntry"),
    ],
    { Rva: uint32(), }
  ).set({
    typeName: "MINIDUMP_DIRECTORY",
    toStringFunc: function(){
      const s = getKeyFromValue(this.StreamType.enumValues, this.StreamType.value);
      return s ? s : "0x" + this.StreamType.value.toString(16);
    },
  });

  var minidump_header = struct({
    Signature: array(char(), 4).set({
      toStringFunc: function(){
        return this[0].value + this[1].value + this[2].value + this[3].value;
      },
    }),
    Version: uint32(),
    NumberOfStreams: uint32(),
    StreamDirectoryRva: pointer(
      uint32(),
      array(minidump_directory_entry,
            function(root){return root.NumberOfStreams.value})),
    CheckSum: uint32(),
    TimeDateStamp: uint32().set({
      typeName: "time_t",
      toStringFunc: function(){return new Date(this.value*1000);},
    }),
    Flags: flags("MINIDUMP_TYPE", uint64(), {
      NORMAL                            : 0x00000000,
      WITH_DATA_SEGS                    : 0x00000001,
      WITH_FULL_MEMORY                  : 0x00000002,
      WITH_HANDLE_DATA                  : 0x00000004,
      FILTER_MEMORY                     : 0x00000008,
      SCAN_MEMORY                       : 0x00000010,
      WITH_UNLOADED_MODULES             : 0x00000020,
      WITH_INDIRECTLY_REFERENCED_MEMORY : 0x00000040,
      FILTER_MODULE_PATHS               : 0x00000080,
      WITH_PROCESS_THREAD_DATA          : 0x00000100,
      WITH_PRIVATE_READ_WRITE_MEMORY    : 0x00000200,
      WITHOUT_OPTIONAL_DATA             : 0x00000400,
      WITH_FULL_MEMORY_INFO             : 0x00000800,
      WITH_THREAD_INFO                  : 0x00001000,
      WITH_CODE_SEGS                    : 0x00002000,
      WITHOUT_AUXILLIARY_SEGS           : 0x00004000,
      WITH_FULL_AUXILLIARY_STATE        : 0x00008000,
      WITH_PRIVATE_WRITE_COPY_MEMORY    : 0x00010000,
      IGNORE_INACCESSIBLE_MEMORY        : 0x00020000,
      WITH_TOKEN_INFORMATION            : 0x00040000,
    }),
  }).set({
    name: "Header",
    typeName: "MINIDUMP_HEADER",
    byteOrder: "little-endian",
    defaultLockOffset: 0,
  });

  return minidump_header;
}
