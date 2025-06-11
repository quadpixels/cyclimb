#version 450

layout(location=0) in vec3 fragColor;

layout(binding=0, std430) readonly buffer NormalsBuf {
	float normaldata[];
};

layout(location=0) out vec4 outColor;

vec3 GetNormal(int nidx) {
    return vec3(
        normaldata[nidx*3],
        normaldata[nidx*3 + 1],
        normaldata[nidx*3 + 2]
    );
}

void main() {
    vec3 n = GetNormal(gl_PrimitiveID);

    float dp = dot(normalize(n), normalize(vec3(0,1,0)));
    float x = dp * 0.5 + 0.5;
    outColor = vec4(x, x, x, 1.0);
}