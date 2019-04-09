#include <GL/glew.h>
#include <GL/freeglut.h>
#include <map>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

struct Character {
	GLuint textureID;
	glm::ivec2 size, bearing;
	GLuint advance;
};

void InitTextRender();
void RenderText(GLuint program, std::wstring text, GLfloat x, GLfloat y, GLfloat scale, glm::vec3 color,
    glm::mat4 transform);
void MeasureTextWidth(std::wstring text, float *w);
