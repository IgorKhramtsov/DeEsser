//#include <Windows.h>
#include <nanogui/nanogui.h>


int main(int argc, char** argv) {

  nanogui::init();
  {
    using namespace nanogui;

    Screen *screen = new Screen(Vector2i(600, 480), "DeEsser");
    // Window *window = new Window(screen, "Button demo");
    // window->set_size(Vector2i(200, 100));

    auto b = new Button(screen, "but");

    screen->set_visible(true);
    screen->perform_layout();
    // window->center();

    mainloop(1 / 60.f * 1000);
  }
  nanogui::shutdown();


  return -1;
}