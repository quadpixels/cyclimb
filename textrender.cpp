#include "textrender.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

GLuint vao, vbo;
extern int WIN_W, WIN_H;
extern int g_font_size;

std::map<wchar_t, Character> g_characters;
FT_Face g_face;

void InitTextRender() {
	// VAO
	glGenVertexArrays(1, &vao);
	glGenBuffers(1, &vbo);

	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	glBufferData(GL_ARRAY_BUFFER, sizeof(float)*6*4, nullptr, GL_DYNAMIC_DRAW);
	glEnableVertexAttribArray(0);
	//                index size  type  normalized  stride        pointer
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	// Face
	FT_Library ft;
	if (FT_Init_FreeType(&ft)) {
		printf("Error: cannot init FreeType library\n");
	}
  const char* ttfs[] = {
    "/usr/share/fonts/truetype/arphic/uming.ttc",
    "C:\\Windows\\Fonts\\simsun.ttc",
  };
  for (int i = 0; i < 2; i++) {
    if (FT_New_Face(ft,
      ttfs[i],
      0, &g_face)) {
      printf("Error: cannot load font file\n");
    } else break;
  }
	FT_Set_Pixel_Sizes(g_face, 0, g_font_size);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
}

Character GetCharacter(wchar_t ch) {
	if (g_characters.find(ch) == g_characters.end()) {
		if (FT_Load_Char(g_face, ch, FT_LOAD_RENDER)) {
			printf("Oh! Could not load character for rendering.\n");
		}
		GLuint texture;
		glGenTextures(1, &texture);
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexImage2D(
			GL_TEXTURE_2D,
			0,
			GL_RED,
			g_face->glyph->bitmap.width,
			g_face->glyph->bitmap.rows,
			0,
			GL_RED,
			GL_UNSIGNED_BYTE,
			g_face->glyph->bitmap.buffer
		);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        Character character = {
			texture,
			glm::ivec2(g_face->glyph->bitmap.width, g_face->glyph->bitmap.rows),
			glm::ivec2(g_face->glyph->bitmap_left, g_face->glyph->bitmap_top),
			(GLuint)(g_face->glyph->advance.x)
		};
		g_characters[ch] = character;

		glBindTexture(GL_TEXTURE_2D, 0);
	}
	return g_characters[ch];
}

void MeasureTextWidth(std::wstring text, float *w) {
  float ret = 0.0f;
  for (std::wstring::const_iterator itr = text.begin();
      itr != text.end(); itr++) {
    Character ch = GetCharacter(*itr);
    ret = ret + ch.advance / 64.0f;
  }
  *w = ret;
}

// https://learnopengl.com/code_viewer.php?code=in-practice/text_rendering
void RenderText(GLuint program, std::wstring text, GLfloat x, GLfloat y, GLfloat scale, glm::vec3 color,
    glm::mat4 transform) {
	glUseProgram(program);
	glUniform3f(glGetUniformLocation(program, "textColor"), color.x, color.y, color.z);
	glm::vec2 screensize(WIN_W, WIN_H);
	glUniform2fv(glGetUniformLocation(program, "screensize"), 1, glm::value_ptr(screensize));
	glUniformMatrix4fv(glGetUniformLocation(program, "transform"), 1, GL_FALSE, glm::value_ptr(transform));
  glm::mat4 proj = glm::perspective(60.0f*3.14159f/180.0f, WIN_W*1.0f/WIN_H, 0.1f, 499.0f);
  glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, glm::value_ptr(proj));
	glActiveTexture(GL_TEXTURE0);
	glBindVertexArray(vao);

	int idx = 0;
	for (std::wstring::const_iterator itr = text.begin();
			itr != text.end(); itr ++) {
		Character ch = GetCharacter(*itr);
		GLfloat xpos = x + ch.bearing.x * scale;
		GLfloat ypos = y - ch.bearing.y * scale;

		GLfloat w = ch.size.x * scale;
		GLfloat h = ch.size.y * scale;
		// Update VBO for each character
		GLfloat vertices[6][4] = {
			{ xpos,     ypos + h,   0.0, 1.0 }, //  +-------> +X
			{ xpos + w, ypos,       1.0, 0.0 }, //  |
			{ xpos,     ypos,       0.0, 0.0 }, //  |
			{ xpos,     ypos + h,   0.0, 1.0 }, //  V
			{ xpos + w, ypos + h,   1.0, 1.0 }, //
			{ xpos + w, ypos,       1.0, 0.0 }, //  +Y
		};
		// Render glyph texture over quad
		glBindTexture(GL_TEXTURE_2D, ch.textureID);
		// Update content of VBO memory
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices); // Be sure to use glBufferSubData and not glBufferData

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		// Render quad
		glDrawArrays(GL_TRIANGLES, 0, 6);
		// Now advance cursors for next glyph (note that advance is number of 1/64 pixels)
		x += (ch.advance >> 6) * scale; // Bitshift by 6 to get value in pixels (2^6 = 64 (divide amount of 1/64th pixels by 64 to get amount of pixels))
		idx ++;
	}
	glBindVertexArray(0);
	glBindTexture(GL_TEXTURE_2D, 0);
}
