struct VS_INPUT {
    float4 pos : POSITION;
    float4 color : COLOR;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

cbuffer Constants : register(b0) {
    float4x4 view_proj;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = mul(view_proj, input.pos);
    output.color = input.color;
    return output;
}
