#include "textrender.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <DirectXMath.h>

GLuint vao, vbo;
extern int WIN_W, WIN_H;
extern int g_font_size;
extern ID3D11Device *g_device11;
extern ID3D11DeviceContext *g_context11;
extern ID3D11VertexShader* g_vs_textrender;
extern ID3D11PixelShader* g_ps_textrender;
extern ID3DBlob *g_vs_textrender_blob;
extern void GlmMat4ToDirectXMatrix(DirectX::XMMATRIX* out, const glm::mat4& m);
extern GLuint g_programs[];

ID3D11InputLayout *input_layout11;

std::map<wchar_t, Character> g_characters;
std::map<wchar_t, Character_D3D11> g_characters_d3d11;
FT_Face g_face;
struct TextCbPerScene {
  DirectX::XMVECTOR screensize; // Assume alignment at float4 boundary
  DirectX::XMMATRIX transform;
  DirectX::XMMATRIX projection;
  DirectX::XMVECTOR textcolor;
};

static ID3D11Buffer* vertex_buffer11;
static ID3D11Buffer* textcb_perscene11;

extern ID3D11BlendState* g_blendstate11;

void do_InitCommon() {
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
      printf("Error: cannot load font file #%d=%s\n", i, ttfs[i]);
    }
    else break;
  }
  FT_Set_Pixel_Sizes(g_face, 0, g_font_size);
}

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

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  do_InitCommon();
}

void InitTextRender_D3D11() {
  // Declare a dynamic Vertex Buffer
  // 4 floats per vertex, 3 vertex per triangle, 2 triangles per character, so 6*4
  D3D11_BUFFER_DESC desc = { };
  desc.ByteWidth = sizeof(float) * 6 * 4;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
  desc.MiscFlags = 0;
  desc.StructureByteStride = sizeof(float) * 4;
  desc.Usage = D3D11_USAGE_DYNAMIC;

  assert(SUCCEEDED(g_device11->CreateBuffer(&desc, nullptr, &vertex_buffer11)));

  desc.ByteWidth = sizeof(TextCbPerScene);
  desc.StructureByteStride = sizeof(TextCbPerScene);
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  assert(SUCCEEDED(g_device11->CreateBuffer(&desc, nullptr, &textcb_perscene11)));

  // Create Input Layout
  D3D11_INPUT_ELEMENT_DESC inputdesc[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 }
  };
  assert(SUCCEEDED(g_device11->CreateInputLayout(inputdesc, 2, g_vs_textrender_blob->GetBufferPointer(),
    g_vs_textrender_blob->GetBufferSize(), &input_layout11)));

  do_InitCommon();
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

Character_D3D11 GetCharacter_D3D11(wchar_t ch) {
  if (g_characters_d3d11.find(ch) == g_characters_d3d11.end()) {
    if (FT_Load_Char(g_face, ch, FT_LOAD_RENDER)) {
      printf("Oh! Could not load character for rendering.\n");
    }

    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView *srv = nullptr;
    int W = 0, H = 0;

    if (g_face->glyph->bitmap.buffer) {
      W = g_face->glyph->bitmap.width;
      H = g_face->glyph->bitmap.rows;

      D3D11_TEXTURE2D_DESC d2d = { };
      d2d.MipLevels = 1;
      d2d.Format = DXGI_FORMAT_R8_UNORM;
      d2d.Width = W;
      d2d.Height = H;
      d2d.ArraySize = 1;
      d2d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      d2d.SampleDesc.Count = 1;
      d2d.SampleDesc.Quality = 0;

      D3D11_SUBRESOURCE_DATA sd = { };
      sd.pSysMem = g_face->glyph->bitmap.buffer;
      sd.SysMemPitch = W; // Line distance

      assert(SUCCEEDED(g_device11->CreateTexture2D(&d2d, &sd, &tex)));

      D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = { };
      srv_desc.Format = DXGI_FORMAT_R8_UNORM;
      srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
      srv_desc.Texture2D.MipLevels = 1;
      assert(SUCCEEDED(g_device11->CreateShaderResourceView(tex, &srv_desc, &srv)));
    }

    // texture, srv, size, bearing, advance
    Character_D3D11 character_d3d11 = {
      tex, srv,
      glm::ivec2(W, H),
      glm::ivec2(g_face->glyph->bitmap_left, g_face->glyph->bitmap_top),
      (GLuint)(g_face->glyph->advance.x)
    };
    g_characters_d3d11[ch] = character_d3d11;
  }

  return g_characters_d3d11[ch];
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
void do_RenderText(GLuint program, std::wstring text, GLfloat x, GLfloat y, GLfloat scale, glm::vec3 color, glm::mat4 transform) {
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

void do_RenderText_D3D11(std::wstring text, float x, float y, float scale, glm::vec3 color, glm::mat4 transform) {
  g_context11->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  g_context11->IASetInputLayout(input_layout11);
  g_context11->VSSetShader(g_vs_textrender, nullptr, 0);
  g_context11->PSSetShader(g_ps_textrender, nullptr, 0);
  unsigned stride = sizeof(float) * 4;
  unsigned zero = 0;
  g_context11->IASetVertexBuffers(0, 1, &vertex_buffer11, &stride, &zero);
  float blend_factor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
  g_context11->OMSetBlendState(g_blendstate11, blend_factor, 0xFFFFFFFF);
  g_context11->VSSetConstantBuffers(0, 1, &textcb_perscene11);
  g_context11->PSSetConstantBuffers(0, 1, &textcb_perscene11);

  TextCbPerScene cb_perscene = { };
  cb_perscene.screensize.m128_f32[0] = WIN_W;
  cb_perscene.screensize.m128_f32[1] = WIN_H;
  GlmMat4ToDirectXMatrix(&cb_perscene.transform, transform);
  glm::mat4 proj = glm::perspective(60.0f*3.14159f / 180.0f, WIN_W*1.0f / WIN_H, 0.1f, 499.0f);
  GlmMat4ToDirectXMatrix(&cb_perscene.projection, proj);
  cb_perscene.textcolor.m128_f32[0] = color.x;
  cb_perscene.textcolor.m128_f32[1] = color.y;
  cb_perscene.textcolor.m128_f32[2] = color.z;

  D3D11_MAPPED_SUBRESOURCE mapped;
  assert(SUCCEEDED(g_context11->Map(textcb_perscene11, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
  memcpy(mapped.pData, &cb_perscene, sizeof(cb_perscene));
  g_context11->Unmap(textcb_perscene11, 0);

  for (int i = 0; i<int(text.size()); i++) {
    Character_D3D11 ch = GetCharacter_D3D11(text[i]);

    if (ch.srv != nullptr) {

      float xpos = x + ch.bearing.x * scale;
      float ypos = y - ch.bearing.y * scale;
      float w = ch.size.x * scale;
      float h = ch.size.y * scale;

      float vertices[6][4] = {
        { xpos,     ypos + h,   0.0, 1.0 }, //  +-------> +X
        { xpos,     ypos,       0.0, 0.0 }, //  |
        { xpos + w, ypos,       1.0, 0.0 }, //  |
        { xpos,     ypos + h,   0.0, 1.0 }, //  V
        { xpos + w, ypos,       1.0, 0.0 }, //  
        { xpos + w, ypos + h,   1.0, 1.0 }, //  +Y
      };
      assert(SUCCEEDED(g_context11->Map(vertex_buffer11, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)));
      memcpy(mapped.pData, vertices, sizeof(vertices));
      g_context11->Unmap(vertex_buffer11, 0);
      g_context11->PSSetShaderResources(0, 1, &ch.srv);
      g_context11->Draw(6, 0);
    }

    x += (ch.advance >> 6) * scale;
  }
}

void RenderText(GraphicsAPI api, std::wstring text, GLfloat x, GLfloat y, GLfloat scale, glm::vec3 color, glm::mat4 transform) {
  switch (api) {
    case ClimbD3D11: do_RenderText_D3D11(text, x, y, scale, color, transform); break;
    case ClimbOpenGL: do_RenderText(g_programs[6], text, x, y, scale, color, transform); break;
  }
}