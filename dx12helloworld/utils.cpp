#include "utils.hpp"

#include <stdio.h>
#include <stdexcept>
#include <Windows.h>

void CE(HRESULT x) {
  if (FAILED(x)) {
    printf("ERROR: %X\n", x);
    throw std::exception();
  }
}
