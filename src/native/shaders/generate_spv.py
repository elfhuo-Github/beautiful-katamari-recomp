#!/usr/bin/env python3
"""Generate minimal SPIR-V shaders for the native renderer."""

import struct

def u32(x):
    return struct.pack('<I', x & 0xFFFFFFFF)

def make_header(magic=0x07230203, version=0x00010400, generator=0,
                bound=0, schema=0):
    """SPIR-V header: magic, version, generator, bound, schema."""
    return b''.join([u32(magic), u32(version), u32(generator),
                     u32(bound), u32(schema)])

def instr(opcode, word_count, *operands):
    """Single SPIR-V instruction."""
    return b''.join([u32((word_count << 16) | opcode),
                     b''.join(u32(o) for o in operands)])

# Opcode constants
OpCapability = 11
OpMemoryModel = 14
OpEntryPoint = 15
OpSource = 18
OpName = 19
OpMemberName = 20
OpMemberDecorate = 31
OpDecorate = 39
OpVariable = 59
OpTypeVoid = 60
OpTypeFunction = 61
OpTypeFloat = 64
OpTypeVector = 65
OpTypeMatrix = 66
OpTypeImage = 67
OpTypeSampledImage = 68
OpTypeInt = 70
OpConstant = 72
OpConstantComposite = 73
OpSampler = 76
OpTypeImage = 67
OpTypeSampledImage = 68
OpTypePointer = 71
OpConstantNull = 74
OpFunction = 75
OpFunctionEnd = 76
OpFunctionParameter = 77
OpLabel = 80
OpStore = 88
OpLoad = 89
OpAccessChain = 92
OpInBoundsAccessChain = 94
OpArrayLength = 98
OpVectorTimesMatrix = 118
OpMatrixTimesVector = 119
OpDot = 122
OpOuterProduct = 123
OpMatrixTimesMatrix = 124
OpConvertFToU = 134
OpConvertFToS = 135
OpConvertSToF = 136
OpConvertUToF = 137
OpConvertPtrToU = 138
OpConvertPtrToS = 139
OpBitcast = 140
OpSConvert = 141
OpUConvert = 142
OpFConvert = 143
OpQuantizeToF16 = 144
OpConvertFToF16 = 145
OpConvertU32ToF16 = 146
OpConvertF16ToU32 = 147
OpConvertF16ToU64 = 148
OpConvertU64ToF16 = 149
OpGenericConvertPtrToU = 152
OpGenericConvertPtrToS = 153
OpBitcast = 154
OpSNegate = 144
OpFNegate = 145
OpIAdd = 148
OpFAdd = 149
OpISub = 150
OpFSub = 151
OpIMul = 152
OpFMul = 153
OpUDiv = 154
OpSDiv = 155
OpFDiv = 156
OpUMod = 157
OpSRem = 158
OpSMod = 159
OpFRem = 160
OpFMod = 161
OpVectorTimesScalar = 162
OpMatrixTimesScalar = 163
OpVectorTimesMatrix = 164
OpMatrixTimesVector = 165
OpMatrixTimesMatrix = 166
OpOuterProduct = 167
OpDot = 168
OpIAddCarry = 169
OpISubBorrow = 170
OpUMulExtended = 171
OpSMulExtended = 172
OpAny = 174
OpAll = 175
OpIsNan = 176
OpIsInf = 177
OpIsFinite = 178
OpIsNormal = 179
OpSignBitSet = 180
OpLessOrGreater = 181
OpOrdered = 182
OpUnordered = 183
OpLogicalEqual = 184
OpLogicalNotEqual = 185
OpLogicalOr = 186
OpLogicalAnd = 187
OpLogicalNot = 188
OpSelect = 190
OpIEqual = 191
OpINotEqual = 192
OpUGreaterThan = 193
OpSGreaterThan = 194
OpUGreaterThanEqual = 195
OpSGreaterThanEqual = 196
OpULessThan = 197
OpSLessThan = 198
OpULessThanEqual = 199
OpSLessThanEqual = 200
OpFOrdEqual = 201
OpFUnordEqual = 202
OpFOrdNotEqual = 203
OpFUnordNotEqual = 204
OpFOrdLessThan = 205
OpFUnordLessThan = 206
OpFOrdGreaterThan = 207
OpFUnordGreaterThan = 208
OpFOrdGreaterThanEqual = 209
OpFUnordGreaterThanEqual = 210
OpFOrdLessThanEqual = 211
OpFUnordLessThanEqual = 212
OpShiftRightLogical = 214
OpShiftRightArithmetic = 215
OpShiftLeftLogical = 216
OpBitwiseOr = 217
OpBitwiseXor = 218
OpBitwiseAnd = 219
OpNot = 220
OpBitFieldInsert = 221
OpBitFieldSExtract = 222
OpBitFieldUExtract = 223
OpBitReverse = 224
OpBitCount = 225
OpDPdx = 227
OpDPdy = 228
OpFwidth = 229
OpEmitVertex = 243
OpEndPrimitive = 244
OpEmitStreamVertex = 245
OpEndStreamPrimitive = 246
OpControlBarrier = 252
OpMemoryBarrier = 253
OpReturn = 258
OpReturnValue = 259
OpUnreachable = 260
OpSampledImage = 273
OpImageSampleImplicitLod = 276
OpImageSampleExplicitLod = 278
OpImageSampleDrefImplicitLod = 280
OpImageSampleDrefExplicitLod = 282
OpImageSampleProjImplicitLod = 284
OpImageSampleProjExplicitLod = 286
OpImageSampleProjDrefImplicitLod = 288
OpImageSampleProjDrefExplicitLod = 290
OpImageFetch = 292
OpImageGather = 294
OpImageDrefGather = 296
OpImageRead = 298
OpImageWrite = 300
OpImage = 302
OpImageQueryFormat = 304
OpImageQueryOrder = 305
OpImageQuerySizeLod = 306
OpImageQuerySize = 307
OpImageQueryLod = 308
OpImageQueryLevels = 309
OpImageQuerySamples = 310
OpImageRead = 311
OpImageWrite = 312
OpImageSampleFootprintNV = 314
OpGroupDecorate = 315
OpGroupMemberDecorate = 316
OpSelectionMerge = 337
OpMerge = 338
OpSwitch = 339
OpSwitchDefault = 340
OpSelect = 341
OpOpImageTexelPointer = 346
OpSubgroupBallot = 348
OpSubgroupFirstInvocation = 349
OpSubgroupAll = 350
OpSubgroupAny = 351
OpSubgroupReadInvocation = 352
OpIAddCarry = 353
OpISubBorrow = 354
OpUMulExtended = 355
OpSMulExtended = 356
OpAny = 357
OpAll = 358
OpIsNan = 359
OpIsInf = 360
OpIsFinite = 361
OpIsNormal = 362
OpSignBitSet = 363
OpLessOrGreater = 364
OpOrdered = 365
OpUnordered = 366
OpLogicalEqual = 367
OpLogicalNotEqual = 368
OpLogicalOr = 369
OpLogicalAnd = 370
OpLogicalNot = 371
OpSelect = 372
OpIEqual = 373
OpINotEqual = 374
OpUGreaterThan = 375
OpSGreaterThan = 376
OpUGreaterThanEqual = 377
OpSGreaterThanEqual = 378
OpULessThan = 379
OpSLessThan = 380
OpULessThanEqual = 381
OpSLessThanEqual = 382
OpFOrdEqual = 383
OpFUnordEqual = 384
OpFOrdNotEqual = 385
OpFUnordNotEqual = 386
OpFOrdLessThan = 387
OpFUnordLessThan = 388
OpFOrdGreaterThan = 389
OpFUnordGreaterThan = 390
OpFOrdGreaterThanEqual = 391
OpFUnordGreaterThanEqual = 392
OpFOrdLessThanEqual = 393
OpFUnordLessThanEqual = 394
OpShiftRightLogical = 396
OpShiftRightArithmetic = 397
OpShiftLeftLogical = 398
OpBitwiseOr = 399
OpBitwiseXor = 400
OpBitwiseAnd = 401
OpNot = 402
OpBitFieldInsert = 403
OpBitFieldSExtract = 404
OpBitFieldUExtract = 405
OpBitReverse = 406
OpBitCount = 407
OpDPdxFine = 409
OpDPdyFine = 410
OpFwidthFine = 411
OpDPdxCoarse = 412
OpDPdyCoarse = 413
OpFwidthCoarse = 414
OpRuntimeDescriptorArray = 415
OpInputComposite = 416
OpDecorateString = 417
OpMemberDecorateString = 418

# Addressing Model
Logical = 0
# Memory Model
GLSL450 = 1
# Execution Model
Vertex = 0
Fragment = 1
# Storage Class
UniformConstant = 0
Input = 1
Uniform = 4
Output = 5
# Decoration
RelaxedPrecision = 1
Location = 37
DescriptorSet = 38
Binding = 39
Block = 40
# BuiltIn
Position = 43
Face = 44
# Dim
Dim1D = 0
Dim2D = 1
Dim3D = 2
DimCube = 3
# Image Format
Unknown = 0
# Image Channel Order
Rgba = 0
# Image Channel Data Type
Float = 1
# Sampler Addressing Mode
None_ = 0
ClampToEdge = 1
Clamp = 2
Repeat = 3
RepeatMirrored = 4
# Sampler Filter mode
Nearest = 0
Linear = 1
# Sampler reduction mode
WeightedAverage = 0
Min = 1
Max = 2
# Memory semantics
AcquireRelease = 2
# Memory access
Aligned = 12
# OpTypeImage sampled
Two = 2


class Builder:
    def __init__(self, bound=1000):
        self.bound = bound
        self.next_id = 1
        self.insts = []
        self.strings = {}

    def id(self):
        self.next_id += 1
        return self.next_id

    def type_void(self):
        id_ = self.id()
        self.insts.append(instr(OpTypeVoid, 2, id_))
        return id_

    def type_function(self, ret_type, param_types):
        id_ = self.id()
        self.insts.append(instr(OpTypeFunction, 3 + len(param_types),
                                id_, ret_type, *param_types))
        return id_

    def type_float(self, width=32):
        id_ = self.id()
        self.insts.append(instr(OpTypeFloat, 3, id_, width))
        return id_

    def type_vector(self, component_type, count):
        id_ = self.id()
        self.insts.append(instr(OpTypeVector, 4, id_, component_type, count))
        return id_

    def type_matrix(self, column_type, count):
        id_ = self.id()
        self.insts.append(instr(OpTypeMatrix, 4, id_, column_type, count))
        return id_

    def type_pointer(self, storage_class, type_id):
        id_ = self.id()
        self.insts.append(instr(OpTypePointer, 4, id_, storage_class, type_id))
        return id_

    def type_int(self, width=32, signed=1):
        id_ = self.id()
        self.insts.append(instr(OpTypeInt, 4, id_, width, signed))
        return id_

    def constant(self, type_id, value):
        id_ = self.id()
        if isinstance(value, float):
            self.insts.append(instr(OpConstant, 4, id_, type_id,
                                    struct.unpack('<I', struct.pack('<f', value))[0]))
        else:
            self.insts.append(instr(OpConstant, 4, id_, type_id, value))
        return id_

    def constant_composite(self, type_id, constituents):
        id_ = self.id()
        words = 3 + len(constituents)
        self.insts.append(instr(OpConstantComposite, words, id_, type_id,
                                *constituents))
        return id_

    def constant_null(self, type_id):
        id_ = self.id()
        self.insts.append(instr(OpConstantNull, 3, id_, type_id))
        return id_

    def sampler(self, addressing_mode, filter_mode):
        id_ = self.id()
        self.insts.append(instr(OpSampler, 4, id_, addressing_mode, filter_mode))
        return id_

    def image(self, type_id, dim, depth, arrayed, ms, sampled, image_format):
        id_ = self.id()
        self.insts.append(instr(OpTypeImage, 10, id_, type_id, dim, depth,
                                arrayed, ms, sampled, image_format, 0, 0))
        return id_

    def sampled_image(self, image_type_id):
        id_ = self.id()
        self.insts.append(instr(OpTypeSampledImage, 3, id_, image_type_id))
        return id_

    def variable(self, type_id, storage_class, initializer=None):
        id_ = self.id()
        if initializer is not None:
            self.insts.append(instr(OpVariable, 5, id_, type_id, storage_class,
                                    initializer))
        else:
            self.insts.append(instr(OpVariable, 4, id_, type_id, storage_class))
        return id_

    def decorate(self, target, decoration, *args):
        words = 3 + len(args)
        self.insts.append(instr(OpDecorate, words, target, decoration, *args))

    def member_decorate(self, struct_type, member, decoration, *args):
        words = 4 + len(args)
        self.insts.append(instr(OpMemberDecorate, words, struct_type, member,
                                decoration, *args))

    def name(self, target, name_str):
        # Store string in string table, return string id
        str_id = self.id()
        self.strings[str_id] = name_str.encode('utf-8') + b'\x00'
        self.insts.append(instr(OpName, 2 + len(self.strings[str_id]) // 4 +
                                (1 if len(self.strings[str_id]) % 4 else 0),
                                str_id))
        # Pad string to 4-byte boundary
        padded = self.strings[str_id] + b'\x00' * ((4 - len(self.strings[str_id]) % 4) % 4)
        # Actually OpName takes the string directly, not as separate data
        # Let me redo this properly
        return str_id

    def capability(self, cap):
        self.insts.append(instr(OpCapability, 2, cap))

    def memory_model(self, addressing, memory):
        self.insts.append(instr(OpMemoryModel, 3, addressing, memory))

    def entry_point(self, exec_model, entry_id, name, *interfaces):
        name_id = self.id()
        self.strings[name_id] = name.encode('utf-8') + b'\x00'
        words = 3 + len(interfaces) + 1  # +1 for name_id
        self.insts.append(instr(OpEntryPoint, words, exec_model, entry_id,
                                name_id, *interfaces))
        return name_id

    def source(self, source_lang, version):
        self.insts.append(instr(OpSource, 3, source_lang, version))

    def function(self, type_id, func_id, control, function_type):
        self.insts.append(instr(OpFunction, 5, type_id, func_id, control,
                                function_type))

    def function_end(self):
        self.insts.append(instr(OpFunctionEnd, 1))

    def function_parameter(self, type_id):
        id_ = self.id()
        self.insts.append(instr(OpFunctionParameter, 3, id_, type_id))
        return id_

    def label(self):
        id_ = self.id()
        self.insts.append(instr(OpLabel, 2, id_))
        return id_

    def return_(self):
        self.insts.append(instr(OpReturn, 1))

    def return_value(self, value_id):
        self.insts.append(instr(OpReturnValue, 2, value_id))

    def load(self, type_id, ptr_id):
        id_ = self.id()
        self.insts.append(instr(OpLoad, 4, id_, type_id, ptr_id))
        return id_

    def store(self, ptr_id, value_id):
        self.insts.append(instr(OpStore, 3, ptr_id, value_id))

    def access_chain(self, type_id, base_ptr, *indexes):
        id_ = self.id()
        words = 4 + len(indexes)
        self.insts.append(instr(OpAccessChain, words, id_, type_id, base_ptr,
                                *indexes))
        return id_

    def composite_extract(self, type_id, composite, *indexes):
        id_ = self.id()
        words = 4 + len(indexes)
        self.insts.append(instr(OpCompositeExtract, words, id_, type_id, composite,
                                *indexes))
        return id_

    def composite_construct(self, type_id, *constituents):
        id_ = self.id()
        words = 3 + len(constituents)
        self.insts.append(instr(OpCompositeConstruct, words, id_, type_id,
                                *constituents))
        return id_

    def vector_times_matrix(self, type_id, vector, matrix):
        id_ = self.id()
        self.insts.append(instr(OpVectorTimesMatrix, 5, id_, type_id, vector, matrix))
        return id_

    def fadd(self, type_id, a, b):
        id_ = self.id()
        self.insts.append(instr(OpFAdd, 5, id_, type_id, a, b))
        return id_

    def fmul(self, type_id, a, b):
        id_ = self.id()
        self.insts.append(instr(OpFMul, 5, id_, type_id, a, b))
        return id_

    def fsub(self, type_id, a, b):
        id_ = self.id()
        self.insts.append(instr(OpFSub, 5, id_, type_id, a, b))
        return id_

    def convert_s_to_f(self, type_id, value):
        id_ = self.id()
        self.insts.append(instr(OpConvertSToF, 4, id_, type_id, value))
        return id_

    def convert_u_to_f(self, type_id, value):
        id_ = self.id()
        self.insts.append(instr(OpConvertUToF, 4, id_, type_id, value))
        return id_

    def dot(self, type_id, a, b):
        id_ = self.id()
        self.insts.append(instr(OpDot, 5, id_, type_id, a, b))
        return id_

    def sampler_image(self, image_id, sampler_id):
        id_ = self.id()
        self.insts.append(instr(OpSampledImage, 4, id_, image_id, sampler_id))
        return id_

    def image_sample_implicit_lod(self, type_id, sampled_image, coords,
                                  *extra):
        id_ = self.id()
        words = 5 + len(extra)
        self.insts.append(instr(OpImageSampleImplicitLod, words, id_, type_id,
                                sampled_image, coords, *extra))
        return id_

    def build(self):
        # Fix up bound in header
        result = make_header(bound=self.bound)
        for inst in self.insts:
            result += inst
        # Append strings as separate instructions? No, OpName/OpMemberName take strings inline
        # Actually, I need to redo OpName to include the string inline
        return result


def build_vertex_shader():
    """Build a minimal vertex shader SPIR-V."""
    b = Builder()

    # Capabilities
    b.capability(Shader := 11)  # OpCapability Shader
    b.memory_model(Logical, GLSL450)

    # Entry point
    void_type = b.type_void()
    float_type = b.type_float(32)
    vec3_type = b.type_vector(float_type, 3)
    vec4_type = b.type_vector(float_type, 4)
    mat4_type = b.type_matrix(vec4_type, 4)
    int_type = b.type_int(32, 1)

    # Pointer types
    ptr_input_vec3 = b.type_pointer(Input, vec3_type)
    ptr_input_vec4 = b.type_pointer(Input, vec4_type)
    ptr_output_vec4 = b.type_pointer(Output, vec4_type)
    ptr_uniform_mat4 = b.type_pointer(UniformConstant, mat4_type)

    # Function type
    func_type = b.type_function(void_type, [])

    # Entry point
    main_id = b.id()
    entry_name_id = b.entry_point(Vertex, main_id, "main")

    # Source
    b.source(GLSL450, 450)

    # Interface variables
    in_position = b.variable(ptr_input_vec3, Input)
    b.decorate(in_position, Location, 0)
    b.name(in_position, "in_position")

    in_color = b.variable(ptr_input_vec4, Input)
    b.decorate(in_color, Location, 1)
    b.name(in_color, "in_color")

    # Uniform block
    uniforms_struct = b.type_struct([mat4_type])
    b.member_decorate(uniforms_struct, 0, Offset, 0)
    b.decorate(uniforms_struct, Block)

    uniforms_ptr = b.type_pointer(UniformConstant, uniforms_struct)
    uniforms_var = b.variable(uniforms_ptr, UniformConstant)
    b.decorate(uniforms_var, DescriptorSet, 0)
    b.decorate(uniforms_var, Binding, 0)
    b.name(uniforms_var, "uniforms")

    # Output
    v_color = b.variable(ptr_output_vec4, Output)
    b.decorate(v_color, Location, 0)
    b.name(v_color, "v_color")

    # gl_Position
    gl_position = b.variable(ptr_output_vec4, Output)
    b.decorate(gl_position, BuiltIn, Position)
    b.name(gl_position, "gl_Position")

    # Main function
    b.function(void_type, main_id, 0, func_type)
    entry_label = b.label()

    # Load uniforms
    uniforms_ptr_load = b.load(uniforms_ptr, uniforms_var)
    view_proj_ptr = b.access_chain(mat4_type, uniforms_ptr_load, b.constant(int_type, 0))
    view_proj = b.load(mat4_type, view_proj_ptr)

    # Load inputs
    pos = b.load(vec3_type, in_position)
    color = b.load(vec4_type, in_color)

    // Convert vec3 to vec4
    pos_w = b.constant(float_type, 1.0)
    pos4 = b.composite_construct(vec4_type, pos, pos_w)

    // gl_Position = view_proj * pos4
    gl_pos = b.vector_times_matrix(vec4_type, pos4, view_proj)
    b.store(gl_position, gl_pos)

    // v_color = color
    b.store(v_color, color)

    b.return_()
    b.function_end()

    return b.build()


def build_fragment_shader():
    """Build a minimal fragment shader SPIR-V."""
    b = Builder()

    b.capability(11)  # OpCapability Shader
    b.memory_model(Logical, GLSL450)

    void_type = b.type_void()
    float_type = b.type_float(32)
    vec4_type = b.type_vector(float_type, 4)
    int_type = b.type_int(32, 1)

    ptr_input_vec4 = b.type_pointer(Input, vec4_type)
    ptr_output_vec4 = b.type_pointer(Output, vec4_type)

    func_type = b.type_function(void_type, [])

    main_id = b.id()
    entry_name_id = b.entry_point(Fragment, main_id, "main")
    b.source(GLSL450, 450)

    v_color = b.variable(ptr_input_vec4, Input)
    b.decorate(v_color, Location, 0)
    b.name(v_color, "v_color")

    out_color = b.variable(ptr_output_vec4, Output)
    b.decorate(out_color, Location, 0)
    b.name(out_color, "out_color")

    b.function(void_type, main_id, 0, func_type)
    entry_label = b.label()

    color = b.load(vec4_type, v_color)
    b.store(out_color, color)

    b.return_()
    b.function_end()

    return b.build()


if __name__ == '__main__':
    vert_spv = build_vertex_shader()
    frag_spv = build_fragment_shader()

    with open('basic.vert.spv', 'wb') as f:
        f.write(vert_spv)
    with open('basic.frag.spv', 'wb') as f:
        f.write(frag_spv)

    print(f"Vertex shader: {len(vert_spv)} bytes")
    print(f"Fragment shader: {len(frag_spv)} bytes")
