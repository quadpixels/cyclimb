#include <map>
#include <string>
#include <gl/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <d3d11.h>
#include "util.hpp"

struct Character {
	GLuint textureID;
	glm::ivec2 size, bearing;
	GLuint advance;
};

struct Character_D3D11 {
  ID3D11Texture2D *texture;
  ID3D11ShaderResourceView *srv;
  glm::ivec2 size, bearing;
  GLuint advance;
};

void InitTextRender();
void InitTextRender_D3D11();
void RenderText(GraphicsAPI api, std::wstring text, GLfloat x, GLfloat y, GLfloat scale, glm::vec3 color, glm::mat4 transform);
void MeasureTextWidth(std::wstring text, float *w);
