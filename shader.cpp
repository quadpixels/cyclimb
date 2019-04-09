#include "shader.hpp"
#include <GL/freeglut.h>

unsigned CreateProgram(const char *vertex_shader_path,
  const char *fragment_shader_path) {
  std::string verShaderCode("");
  std::ifstream verShaderStream(vertex_shader_path);
  if( verShaderStream.is_open() ){
    std::string line("");
    while( getline(verShaderStream, line) )
      verShaderCode += line + "\n";
    verShaderStream.close();
  } else{
    std::cerr << "Error: Cann't open \"" << vertex_shader_path << "\" file." << std::endl;
    return 0;
  }

  GLuint verShader = glCreateShader(GL_VERTEX_SHADER);
  const char *verShaderSource = verShaderCode.c_str();
  glShaderSource(verShader, 1, &verShaderSource, NULL);
  glCompileShader(verShader);

  GLint result;
  int infoLogLength;
  glGetShaderiv(verShader, GL_COMPILE_STATUS, &result);
  if( result == GL_FALSE ){
    glGetShaderiv(verShader, GL_INFO_LOG_LENGTH, &infoLogLength);
    GLchar *errMessage = new GLchar[infoLogLength + 1];
    glGetShaderInfoLog(verShader, infoLogLength, NULL, errMessage);
    errMessage[infoLogLength] = '\0';
    std::cerr << "Error in \"" << vertex_shader_path << "\":" << std::endl;
    std::cerr << errMessage << std::endl;
    delete [] errMessage;
    return 0;
  }

  std::string fragShaderCode("");
  std::ifstream fragShaderStream(fragment_shader_path);
  if( fragShaderStream.is_open() ){
    std::string line("");
    while( getline(fragShaderStream, line) )
      fragShaderCode += line + "\n";
    fragShaderStream.close();
  } else{
    std::cerr << "Error: Cann't open \"" << fragment_shader_path << "\" file." << std::endl;
    return 0;
  }

  GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
  const char *fragShaderSource = fragShaderCode.c_str();
  glShaderSource(fragShader, 1, &fragShaderSource, NULL);
  glCompileShader(fragShader);

  glGetShaderiv(fragShader, GL_COMPILE_STATUS, &result);
  if( result == GL_FALSE ){
    glGetShaderiv(fragShader, GL_INFO_LOG_LENGTH, &infoLogLength);
    GLchar *errMessage = new GLchar[infoLogLength + 1];
    glGetShaderInfoLog(fragShader, infoLogLength, NULL, errMessage);
    errMessage[infoLogLength] = '\0';
    std::cerr << "Error in \"" << fragment_shader_path << "\":" << std::endl;
    std::cerr << errMessage << std::endl;
    delete [] errMessage;
    return 0;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, verShader);
  glAttachShader(program, fragShader);
  glLinkProgram(program);

  glGetProgramiv(program, GL_LINK_STATUS, &result);
  if( result == GL_FALSE ){
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
    GLchar *errMessage = new GLchar[infoLogLength + 1];
    glGetProgramInfoLog(fragShader, infoLogLength, NULL, errMessage);
    errMessage[infoLogLength] = '\0';
    std::cerr << "Error in \"" << fragment_shader_path << "\":" << std::endl;
    std::cerr << errMessage << std::endl;
    delete [] errMessage;
    return 0;
  }

  glDeleteShader(verShader);
  glDeleteShader(fragShader);
  return program;
}