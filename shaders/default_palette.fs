#version 330 core

in  vec3 vert_color;
out vec4 color;

void main()
{
    color = vec4(vert_color, 1.0f);
}
