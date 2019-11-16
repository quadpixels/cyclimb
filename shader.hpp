#ifndef _SHADER_HPP
#define _SHADER_HPP

#include <gl/glew.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <map>
#include <iostream>
#include <fstream>
// Copied from 
unsigned CreateProgram(const char *vertex_shader_path,
  const char *fragment_shader_path);
  
#endif
