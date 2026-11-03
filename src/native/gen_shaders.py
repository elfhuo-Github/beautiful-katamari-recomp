#!/usr/bin/env python3
"""SPIR-V assembler for native renderer shaders.

Assembles a simple text-based SPIR-V assembly into binary SPIR-V.
Supports the subset of instructions needed for simple vertex/fragment shaders.
"""

import struct
import sys

# ---------------------------------------------------------------- opcodes --
OpNop = 0
OpUndef = 1
OpSourceContinued = 2
OpSource = 3
OpSourceExtension = 4
OpName = 5
OpMemberName = 6
OpString = 7
OpLine = 8
OpExtension = 9
OpExtInstImport = 10
OpExtInst = 11
OpMemoryModel = 15
OpEntryPoint = 16
OpExecutionPoint = 18
OpSourceHLSL = 150
OpCapability = 14

# Types
OpTypeVoid = 19
OpTypeBool = 20
OpTypeInt = 21
OpTypeFloat = 22
OpTypeVector = 23
OpTypeMatrix = 24
OpTypeImage = 25
OpTypeSampler = 26
OpTypeSampledImage = 27
OpTypeArray = 28
OpTypeRuntimeArray = 29
OpTypeStruct = 30
OpTypeOpaque = 31
OpTypePointer = 32
OpTypeFunction = 33
OpTypeEvent = 34
OpTypeDeviceEvent = 35
OpTypeReserveId = 36
OpTypeQueue = 37
OpTypePipe = 38
OpTypeForwardPointer = 38

# Constants
OpConstantTrue = 41
OpConstantFalse = 42
OpConstant = 43
OpConstantComposite = 44
OpConstantSampler = 45
OpConstantNull = 46

# Functions
OpFunction = 54
OpFunctionParameter = 55
OpFunctionEnd = 56
OpFunctionCall = 57

# Variables and memory
OpVariable = 59
OpLoad = 61
OpStore = 62
OpCopyMemory = 63
OpCopyMemorySized = 64
OpAccessChain = 65
OpInBoundsAccessChain = 66
OpPtrAccessChain = 67
OpArrayLength = 68
OpGenericPtrMemSemantics = 69
OpInBoundsPtrAccessChain = 70

# Decorations
OpDecorate = 71
OpMemberDecorate = 72

# Composite
OpVectorExtractDynamic = 77
OpVectorInsertDynamic = 78
OpVectorShuffle = 79
OpCompositeConstruct = 80
OpCompositeExtract = 81
OpCompositeInsert = 82
OpCopyObject = 83
OpTranspose = 84

# Sampled image
OpSampledImage = 86
OpImageSampleImplicitLod = 87
OpImageSampleExplicitLod = 88
OpImageSampleDrefImplicitLod = 89
OpImageSampleDrefExplicitLod = 90
OpImageSampleProjImplicitLod = 91
OpImageSampleProjExplicitLod = 92
OpImageSampleProjDrefImplicitLod = 93
OpImageSampleProjDrefExplicitLod = 94
OpImageFetch = 95
OpImageGather = 96
OpImageDrefGather = 97
OpImageRead = 98
OpImageWrite = 99

# Conversions
OpConvertFToU = 109
OpConvertFToS = 110
OpConvertSToF = 111
OpConvertUToF = 112
OpUConvert = 113
OpSConvert = 114
OpFConvert = 115
OpConvertUToU = 116
OpConvertSToS = 117
OpConvertFToF = 118
OpConvertSat = 119

# Control flow
OpReturn = 53
OpKill = 58
OpLabel = 245

# Decorations
DecorationRelaxedPrecision = 0
DecorationSpecId = 1
DecorationBlock = 2
DecorationBufferBlock = 3
DecorationRowMajor = 4
DecorationColMajor = 5
DecorationArrayStride = 6
DecorationMatrixStride = 7
DecorationGLSLShared = 8
DecorationGLSLPacked = 9
DecorationNoPacked = 10
DecorationNoSPIRV = 11
DecorationNoContraction = 12
DecorationNoRestrict = 13
DecorationNoAlias = 14
DecorationNoWrite = 15
DecorationNoReadWrite = 16
DecorationLocation = 17
DecorationComponent = 18
DecorationIndex = 19
DecorationBinding = 20
DecorationDescriptorSet = 21
DecorationOffset = 22
DecorationXfbBuffer = 23
DecorationXfbStride = 24
DecorationNonReadable = 25
DecorationNonWritable = 26
DecorationUniform = 27
DecorationUniformId = 28
DecorationStream = 29
DecorationPosition = 44

# Storage classes
Input = 1
Output = 2
UniformConstant = 3
Uniform = 4
PushConstant = 5
StorageBuffer = 6

# Addressing models
Logical = 0
Physical32 = 1
Physical64 = 2

# Memory models
Simple = 0
GLSL450 = 1
OpenCL = 2

# Execution models
Vertex = 0
TessellationControl = 1
TessellationEvaluation = 2
Geometry = 3
Fragment = 4
GLCompute = 5
Kernel = 6


class Assembler:
    """Simple SPIR-V assembler."""

    def __init__(self):
        self.words = []
        self.bound = 1  # ID 0 is reserved
        self.labels = {}
        self.id_map = {}  # name -> id

    def emit(self, opcode, *operands):
        """Emit an instruction."""
        wc = len(operands) + 1
        self.words.append((wc << 16) | opcode)
        for op in operands:
            if isinstance(op, str):
                # String operand - encode as UTF-8 with null terminator
                encoded = op.encode('utf-8') + b'\x00'
                # Pad to 4-byte boundary
                padded = encoded + b'\x00' * ((4 - len(encoded) % 4) % 4)
                for i in range(0, len(padded), 4):
                    self.words.append(struct.unpack('<I', padded[i:i + 4])[0])
            else:
                self.words.append(op & 0xFFFFFFFF)

    def new_id(self):
        """Allocate a new result ID."""
        self.bound += 1
        return self.bound - 1

    def id(self, name):
        """Get or create an ID for a named symbol."""
        if name not in self.id_map:
            self.id_map[name] = self.new_id()
        return self.id_map[name]

    def header(self):
        """Emit SPIR-V header."""
        self.words.append(0x07230203)  # Magic number
        self.words.append(0x00010000)  # Version 1.0
        self.words.append(0)            # Generator
        self.words.append(0)            # Bound (filled later)
        self.words.append(0)            # Schema

    def capability(self, cap):
        self.emit(OpCapability, cap)

    def ext_inst_import(self, name):
        """Import an extended instruction set (e.g. GLSL.std.450)."""
        str_id = self.new_id()
        self.emit(OpString, str_id, name)
        self.emit(OpExtInstImport, str_id, str_id)

    def memory_model(self, addressing, memory):
        self.emit(OpMemoryModel, addressing, memory)

    def entry_point(self, exec_model, entry_id, name, *interfaces):
        self.emit(OpEntryPoint, exec_model, entry_id, name, *interfaces)

    def execution_point(self, entry_id, mode):
        self.emit(OpExecutionPoint, entry_id, mode)

    def name(self, id_or_name, name_str):
        if isinstance(id_or_name, str):
            id_or_name = self.id(id_or_name)
        self.emit(OpName, id_or_name, name_str)

    def decorate(self, id_or_name, decoration, *args):
        if isinstance(id_or_name, str):
            id_or_name = self.id(id_or_name)
        self.emit(OpDecorate, id_or_name, decoration, *args)

    def type_void(self):
        vid = self.new_id()
        self.emit(OpTypeVoid, vid)
        return vid

    def type_float(self, width=32):
        vid = self.new_id()
        self.emit(OpTypeFloat, vid, width)
        return vid

    def type_vector(self, component_type, component_count):
        vid = self.new_id()
        self.emit(OpTypeVector, vid, component_type, component_count)
        return vid

    def type_pointer(self, storage_class, type_id):
        vid = self.new_id()
        self.emit(OpTypePointer, vid, storage_class, type_id)
        return vid

    def type_function(self, return_type, param_types):
        vid = self.new_id()
        self.emit(OpTypeFunction, vid, return_type, *param_types)
        return vid

    def constant_float(self, type_id, value):
        vid = self.new_id()
        self.emit(OpConstant, type_id, vid,
                  struct.unpack('<I', struct.pack('<f', value))[0])
        return vid

    def constant_composite(self, type_id, *constituents):
        vid = self.new_id()
        self.emit(OpConstantComposite, type_id, vid, *constituents)
        return vid

    def variable(self, type_id, storage_class, initializer=None):
        vid = self.new_id()
        if initializer is not None:
            self.emit(OpVariable, type_id, vid, storage_class, initializer)
        else:
            self.emit(OpVariable, type_id, vid, storage_class)
        return vid

    def function(self, return_type, func_id, func_type_id, *params):
        self.emit(OpFunction, return_type, func_id, func_type_id, *params)

    def function_end(self):
        self.emit(OpFunctionEnd)

    def label(self):
        lid = self.new_id()
        self.emit(OpLabel, lid)
        return lid

    def load(self, type_id, pointer_id):
        vid = self.new_id()
        self.emit(OpLoad, type_id, vid, pointer_id)
        return vid

    def store(self, pointer_id, object_id):
        self.emit(OpStore, pointer_id, object_id)

    def composite_construct(self, type_id, *constituents):
        vid = self.new_id()
        self.emit(OpCompositeConstruct, type_id, vid, *constituents)
        return vid

    def return_(self):
        self.emit(OpReturn)

    def fix_bound(self):
        """Fill in the bound in the header."""
        self.words[3] = self.bound

    def assemble(self):
        self.fix_bound()
        return struct.pack(f'<{len(self.words)}I', *self.words)


# ===========================================================================
# Vertex shader: takes position (vec2) and color (vec4), outputs position
# (vec4) and color (vec4) for the fragment shader.
# ===========================================================================
def build_vertex_shader():
    a = Assembler()
    a.header()
    a.capability(OpCapability)

    a.ext_inst_import("GLSL.std.450")

    a.memory_model(Logical, GLSL450)

    # Types
    void_id = a.type_void()
    float_id = a.type_float(32)
    v2float_id = a.type_vector(float_id, 2)
    v4float_id = a.type_vector(float_id, 4)

    # Pointer types
    ptr_input_v2float = a.type_pointer(Input, v2float_id)
    ptr_input_v4float = a.type_pointer(Input, v4float_id)
    ptr_output_v4float = a.type_pointer(Output, v4float_id)

    # Function type: void()
    void_fn_type = a.type_function(void_id, [])

    # Entry point: void @main()
    main_id = a.new_id()
    a.entry_point(Vertex, main_id, "main",
                  a.id("in_pos"), a.id("in_col"),
                  a.id("out_pos"), a.id("out_col"))

    a.execution_point(main_id, 0)  # OriginUpperLeft

    # Names
    a.name(main_id, "main")
    a.decorate("in_pos", DecorationLocation, 0)
    a.decorate("in_col", DecorationLocation, 1)
    a.decorate("out_pos", DecorationLocation, 0)
    a.decorate("out_col", DecorationLocation, 1)

    # Variables
    in_pos = a.variable(ptr_input_v2float, Input)
    in_col = a.variable(ptr_input_v4float, Input)
    out_pos = a.variable(ptr_output_v4float, Output)
    out_col = a.variable(ptr_output_v4float, Output)

    a.name("in_pos", "in_pos")
    a.name("in_col", "in_col")
    a.name("out_pos", "out_pos")
    a.name("out_col", "out_col")

    # Constants
    float_zero = a.constant_float(float_id, 0.0)
    float_one = a.constant_float(float_id, 1.0)

    # Function
    a.function(void_id, main_id, void_fn_type)
    entry = a.label()

    # Body
    pos_val = a.load(v2float_id, in_pos)
    col_val = a.load(v4float_id, in_col)

    # frag_position = vec4(position.x, position.y, 0.0, 1.0)
    pos4 = a.composite_construct(v4float_id, pos_val, float_zero, float_one)

    a.store(out_pos, pos4)
    a.store(out_col, col_val)

    a.return_()
    a.function_end()

    return a.assemble()


# ===========================================================================
# Fragment shader: takes position (vec4) and color (vec4), outputs color.
# ===========================================================================
def build_fragment_shader():
    a = Assembler()
    a.header()
    a.capability(OpCapability)

    a.ext_inst_import("GLSL.std.450")

    a.memory_model(Logical, GLSL450)

    # Types
    void_id = a.type_void()
    float_id = a.type_float(32)
    v4float_id = a.type_vector(float_id, 4)

    # Pointer types
    ptr_input_v4float = a.type_pointer(Input, v4float_id)
    ptr_output_v4float = a.type_pointer(Output, v4float_id)

    # Function type: void()
    void_fn_type = a.type_function(void_id, [])

    # Entry point: void @main()
    main_id = a.new_id()
    a.entry_point(Fragment, main_id, "main",
                  a.id("in_pos"), a.id("in_col"),
                  a.id("out_col"))

    a.execution_point(main_id, 0)

    # Names
    a.name(main_id, "main")
    a.decorate("in_pos", DecorationLocation, 0)
    a.decorate("in_col", DecorationLocation, 1)
    a.decorate("out_col", DecorationLocation, 0)

    # Variables
    in_pos = a.variable(ptr_input_v4float, Input)
    in_col = a.variable(ptr_input_v4float, Input)
    out_col = a.variable(ptr_output_v4float, Output)

    a.name("in_pos", "in_pos")
    a.name("in_col", "in_col")
    a.name("out_col", "out_col")

    # Function
    a.function(void_id, main_id, void_fn_type)
    entry = a.label()

    # Body
    col_val = a.load(v4float_id, in_col)
    a.store(out_col, col_val)

    a.return_()
    a.function_end()

    return a.assemble()


if __name__ == '__main__':
    vert_spirv = build_vertex_shader()
    frag_spirv = build_fragment_shader()

    print(f"Vertex shader: {len(vert_spirv)} bytes")
    print(f"Fragment shader: {len(frag_spirv)} bytes")

    # Output as C++ hex arrays
    def format_array(name, data):
        lines = [f"const uint32_t {name}[] = {{"]
        for i in range(0, len(data), 4):
            word = struct.unpack('<I', data[i:i + 4])[0]
            lines.append(f"    0x{word:08X},")
        lines.append("};")
        return "\n".join(lines)

    print()
    print(format_array("kVertexShaderSpirv", vert_spirv))
    print()
    print(format_array("kFragmentShaderSpirv", frag_spirv))
