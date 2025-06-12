#version 450

layout(location=0) in vec3 fragColor;
layout(location=1) in flat uint clusterId;

layout(binding=0, std430) readonly buffer NormalsBuf {
	float normaldata[];
};

layout(binding=2, std430) readonly buffer NormalOffsetsBuf {
    uint normaloffsets[];
};

layout(location=0) out vec4 outColor;

vec3 GetNormal(uint nidx) {
    return vec3(
        normaldata[nidx*3],
        normaldata[nidx*3 + 1],
        normaldata[nidx*3 + 2]
    );
}

const vec3 PALETTE[] = {
    vec3(1.0, 1.0, 1.0),
    vec3(1.0, 0.5, 0.5),
    vec3(0.5, 1.0, 0.5),
    vec3(0.5, 0.5, 1.0),
    vec3(1.0, 1.0, 0.5),
    vec3(1.0, 0.5, 1.0),
    vec3(0.5, 1.0, 1.0)
};

void main() {
    uint normalOffset = normaloffsets[clusterId];

    vec3 n = GetNormal(normalOffset + gl_PrimitiveID);
    vec3 c = PALETTE[clusterId % 7];

    float dp = dot(normalize(n), normalize(vec3(0,1,0)));
    float x = dp * 0.5 + 0.5;
    outColor = vec4(c * x, 1.0);
}