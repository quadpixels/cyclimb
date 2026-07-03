#version 450

layout(location=0) out vec4 outColor;

layout(binding=1, std430) readonly buffer NormalsBuf {
	float normaldata[];
};

vec3 GetNormal(uint nidx) {
    return vec3(
        normaldata[nidx*3],
        normaldata[nidx*3 + 1],
        normaldata[nidx*3 + 2]
    );
}

void main() {
    vec3 n = GetNormal(gl_PrimitiveID);
    vec3 c = vec3(1, 1, 1);
    float dp = dot(n, vec3(0, 1, 0));
    c = c * (dp * 0.5 + 0.5);
    outColor = vec4(c, 1);
}