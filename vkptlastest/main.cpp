#include "myframework_vk.h"

#include <stdio.h>

MyFrameworkVk* g_framework{};

const uint32_t WIN_W = 800, WIN_H = 600;

static bool should_exit = false;
void MainLoop() {
  while (!glfwWindowShouldClose(g_framework->GetWindow()) && !should_exit) {
    glfwPollEvents();
  }
  vkDeviceWaitIdle(g_framework->GetLogicalDevice());
}

int main() {
  g_framework = new MyFrameworkVk();
  g_framework->InitWindow("PTLASTest", WIN_W, WIN_H, nullptr);
  g_framework->InitDeviceAndCommandQ();
  g_framework->InitSwapchain();
  g_framework->InitRenderPassAndFramebuffers();
  MainLoop();
  printf("MyFrameworkVk is done.\n");
  exit(0);
}