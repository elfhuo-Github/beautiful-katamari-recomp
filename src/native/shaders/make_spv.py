#!/usr/bin/env python3
"""Generate minimal valid SPIR-V shaders for Beautiful Katamari native Vulkan renderer."""

import struct

def u32(x):
    return struct.pack('<I', x & 0xFFFFFFFF)

# SPIR-V opcodes
OpCapability = 11
OpMemoryModel = 14
OpEntryPoint = 15
OpSource = 18
OpName = 19
OpDecorate = 39
OpTypeVoid = 60
OpTypeFunction = 61
OpTypeFloat = 64
OpTypeVector = 65
OpTypeInt = 70
OpTypePointer = 71
OpConstant = 72
OpVariable = 59
OpLabel = 80
OpStore = 88
OpReturn = 258
OpFunction = 75

Input = 1
UniformConstant = 0
Output = 5

Location = 37
BuiltIn = 43

Logical = 0
GLSL450 = 1

Vertex = 0
Fragment = 1

Position = 43

Shader = 0

class Builder:
    def __init__(self):
        self.insts = []
        self.next_id = 1
        self.bound = 1

    def new_id(self):
        self.next_id += 1
        self.bound = max(self.bound, self.next_id)
        return self.next_id

    def emit(self, opcode, *operands):
        self.insts.append(struct.pack('<I', (words << 16) | opcode))
        for o in operands:
            self.insts.append(u32(o))

    def emit_str(self, opcode, name_str, *fixed_ops):
        name_bytes = name_str.encode('utf-8') + b'\x00'
        pad = (4 - len(name_bytes) % 4) % 4
        name_bytes += b'\x00' * pad
        name_words = len(name_bytes) // 4
        words = 1 + len(fixed_ops) + name_words
        self.insts.append(struct.pack('<I', (words << 16) | opcode))
        self.insts += b''.join(u32(o) for o in fixed_ops)
        self.insts += name_bytes

    def capability(self, cap):
        self.emit(OpCapability, cap)

    def memory_model(self, addressing, model):
        self.emit(OpMemoryModel, addressing, model)

    def entry_point(self, exec_model, entry_id, name_str, *interfaces):
        self.insts.append(instr_entry_point(exec_model, entry_id, name_str, list(interfaces)))

    def source(self, lang, version):
        self.emit(OpSource, lang, version)

    def name(self, target, name_str):
        self.emit_str(OpName, name_str, target)

    def type_void(self):
        id_ = self.new_id()
        self.emit(OpTypeVoid, id_)
        return id_

    def type_function(self, ret_id, param_ids):
        id_ = self.new_id()
        self.emit(OpTypeFunction, id_, ret_id, *param_ids)
        return id_

    def type_float(self, width=32):
        id_ = self.new_id()
        self.emit(OpTypeFloat, id_, width)
        return id_

    def type_vector(self, component_id, count):
        id_ = self.new_id()
        self.emit(OpTypeVector, id_, component_id, count)
        return id_

    def type_int(self, width=32, signed=1):
        id_ = self.new_id()
        self.emit(OpTypeInt, id_, width, signed)
        return id_

    def type_pointer(self, storage_class, type_id):
        id_ = self.new_id()
        self.emit(OpTypePointer, id_, storage_class, type_id)
        return id_

    def type_struct(self, member_type_ids):
        id_ = self.new_id()
        self.emit(OpTypeStruct, id_, *member_type_ids)
        return id_

    def member_decorate(self, struct_type_id, member_index, decoration, *args):
        self.emit(OpMemberDecorate, struct_type_id, member_index, decoration, *args)

    def decorate(self, target, decoration, *args):
        self.emit(OpDecorate, target, decoration, *args)

    def constant(self, type_id, value):
        id_ = self.new_id()
        if isinstance(value, float):
            bits = struct.unpack('<I', struct.pack('<f', value))[0]
        else:
            bits = value
        self.emit(OpConstant, id_, type_id, bits)
        return id_

    def variable(self, type_id, storage_class):
        id_ = self.new_id()
        self.emit(OpVariable, id_, type_id, storage_class)
        return id_

    def function(self, type_id, func_id, control, function_type_id):
        self.emit(OpFunction, type_id, func_id, control, function_type_id)

    def function_end(self):
        self.emit(OpFunctionEnd)

    def label(self):
        id_ = self.new_id()
        self.emit(OpLabel, id_)
        return id_

    def load(self, type_id, ptr_id):
        id_ = self.new_id()
        self.emit(OpLoad, id_, type_id, ptr_id)
        return id_

    def store(self, ptr_id, value_id):
        self.emit(OpStore, ptr_id, value_id)

    def access_chain(self, type_id, base_ptr, *indexes):
        id_ = self.new_id()
        self.emit(OpAccessChain, id_, type_id, base_ptr, *indexes)
        return id_

    def composite_construct(self, type_id, *constituents):
        id_ = self.new_id()
        self.emit(OpCompositeConstruct, id_, type_id, *constituents)
        return id_

    def vector_times_matrix(self, type_id, vector, matrix):
        id_ = self.new_id()
        self.emit(OpVectorTimesMatrix, id_, type_id, vector, matrix)
        return id_

    def build(self):
        header = make_header(self.bound)
        return header + b''.join(self.insts)


def build_vertex_shader():
    b = Builder()

    b.capability(Shader)
    b.memory_model(Logical, GLSL450)

    void_t = b.type_void()
    float_t = b.type_float(32)
    vec3_t = b.type_vector(float_t, 3)
    vec4_t = b.type_vector(float_t, 4)
    mat4_t = b.type_matrix(vec4_t, 4)
    int_t = b.type_int(32, 1)

    ptr_input_v3 = b.type_pointer(Input, vec3_t)
    ptr_input_v4 = b.type_pointer(Input, vec4_t)
    ptr_output_v4 = b.type_pointer(Output, vec4_t)

    struct_uni = b.type_struct([mat4_t])
    b.member_decorate(struct_uni, 0, Offset, 0)
    b.decorate(struct_uni, Block)
    ptr_uniform_struct = b.type_pointer(UniformConstant, struct_uni)

    func_type = b.type_function(void_t, [])

    in_position = b.variable(ptr_input_v3, Input)
    b.decorate(in_position, Location, 0)
    b.name(in_position, "in_position")

    in_color = b.variable(ptr_input_v4, Input)
    b.decorate(in_color, Location, 1)
    b.name(in_color, "in_color")

    uniforms_var = b.variable(ptr_uniform_struct, UniformConstant)
    b.decorate(uniforms_var, DescriptorSet, 0)
    b.decorate(uniforms_var, Binding, 0)
    b.name(uniforms_var, "uniforms")

    gl_position = b.variable(ptr_output_v4, Output)
    b.decorate(gl_position, BuiltIn, Position)
    b.name(gl_position, "gl_Position")

    v_color = b.variable(ptr_output_v4, Output)
    b.decorate(v_color, Location, 0)
    b.name(v_color, "v_color")

    main_id = b.new_id()
    b.entry_point(Vertex, main_id, "main", in_position, in_color, gl_position, v_color, uniforms_var)

    b.source(GLSL450, 450)
    b.name(main_id, "main")

    b.function(void_t, main_id, 0, func_type)
    b.label()

    uniforms_ptr = b.load(ptr_uniform_struct, uniforms_var)
    view_proj_ptr = b.access_chain(mat4_t, uniforms_ptr, b.constant(int_t, 0))
    view_proj = b.load(mat4_t, view_proj_ptr)

    pos = b.load(vec3_t, in_position)
    color = b.load(vec4_t, in_color)

    float_1 = b.constant(float_t, 1.0)
    pos4 = b.composite_construct(vec4_t, pos, float_1)

    gl_pos = b.vector_times_matrix(vec4_t, pos4, view_proj)
    b.store(gl_position, gl_pos)

    b.store(v_color, color)

    b.return_()
    b.function_end()

    return b.build()


def build_fragment_shader():
    b = Builder()

    b.capability(Shader)
    b.memory_model(Logical, GLSL450)

    void_t = b.type_void()
    float_t = b.type_float(32)
    vec4_t = b.type_vector(float_t, 4)
    int_t = b.type_int(32, 1)

    ptr_input_v4 = b.type_pointer(Input, vec4_t)
    ptr_output_v4 = b.type_pointer(Output, vec4_t)

    func_type = b.type_function(void_t, [])

    v_color = b.variable(ptr_input_v4, Input)
    b.decorate(v_color, Location, 0)
    b.name(v_color, "v_color")

    out_color = b.variable(ptr_output_v4, Output)
    b.decorate(out_color, Location, 0)
    b.name(out_color, "out_color")

    main_id = b.new_id()
    b.entry_point(Fragment, main_id, "main", v_color, out_color)

    b.source(GLSL450, 450)
    b.name(main_id, "main")

    b.function(void_t, main_id, 0, func_type)
    b.label()

    color = b.load(vec4_t, v_color)
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
    print(f"Vertex shader: {len(vert_spv)} bytes ({len(vert_spv)//4} words)")
    print(f"Fragment shader: {len(frag_spv)} bytes ({len(frag_spv)//4} words)")
