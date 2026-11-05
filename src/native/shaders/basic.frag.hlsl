struct VS_INPUT {
    float4 color : COLOR;
};

float4 main(VS_INPUT input) : SV_TARGET {
    return input.color;
}
