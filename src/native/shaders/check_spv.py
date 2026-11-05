import struct

with open('basic.vert.spv', 'rb') as f:
    data = f.read()
words = struct.unpack(f'<{len(data)//4}I', data)
print(f"Vertex shader: {len(words)} words")
print(f"Header: magic=0x{words[0]:08x}, ver={words[1]:#x}, gen={words[2]}, bound={words[3]}, schema={words[4]}")

op_names = {
    11:'OpCapability', 14:'OpMemoryModel', 15:'OpEntryPoint', 18:'OpSource',
    19:'OpName', 39:'OpDecorate', 60:'OpTypeVoid', 61:'OpTypeFunction',
    64:'OpTypeFloat', 65:'OpTypeVector', 70:'OpTypeInt', 71:'OpTypePointer',
    72:'OpConstant', 73:'OpConstantComposite',
    59:'OpVariable', 80:'OpLabel', 88:'OpStore', 258:'OpReturn', 76:'OpFunctionEnd'
}

i = 5
while i < len(words):
    wc = (words[i] >> 16) & 0xFFFF
    opcode = words[i] & 0xFFFF
    if wc == 0:
        print(f"[{i:3d}] ERROR: wc=0")
        break
    if i + wc > len(words):
        print(f"[{i:3d}] ERROR: wc={wc} exceeds {len(words)}")
        break
    op_name = op_names.get(opcode, f'Unknown({opcode})')
    ops = words[i+1:i+wc-1] if i+wc-1 <= len(words) else []
    print(f"[{i:3d}] wc={wc} op={opcode:3d}({op_name:20s}) ops={[hex(o) for o in ops]}")
    i += wc

print(f"\nFinal: i={i}, total={len(words)}, match={i==len(words)}")
