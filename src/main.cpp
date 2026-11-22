#include <SDL.h>

#include "app.h"

int main(int argc, char** argv) {
  App app;
  if (!app.Init(argc, argv)) return 1;
  app.Run();
  app.Shutdown();
  return 0;
}
